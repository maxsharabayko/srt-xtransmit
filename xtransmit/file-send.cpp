#if ENABLE_FILE_TRANSFER
#include <iostream>
#include <iterator>
// https://en.cppreference.com/w/cpp/compiler_support
#include <filesystem>	// Requires C++17
#include <functional>
#include <string>
#include <vector>
#include <deque>
#include <chrono>
#include <thread>

#include "file-send.hpp"
#include "srt_socket.hpp"


using namespace std;
using namespace std::chrono;
using namespace xtransmit;
using namespace xtransmit::file::send;
namespace fs = std::filesystem;


using shared_srt = std::shared_ptr<socket::srt>;


/// Send one file in the messaging mode.
/// @return true on success, false if an error happened during transmission
bool send_file(const string &filename, const string &upload_name, socket::srt &dst
	, vector<char> &buf, const atomic_bool& force_break)
{
	ifstream ifile(filename, ios::binary);
	if (!ifile)
	{
		cerr << "Error opening file : " << filename << endl;
		return false;
	}

	const chrono::steady_clock::time_point time_start = chrono::steady_clock::now();
	size_t file_size = 0;

	cerr << "Transmitting '" << filename << " to " << upload_name << endl;

	/*   1 byte      string    1 byte
	 * ------------------------------------------------
	 * | ......EF | Filename | 0     | Payload
	 * ------------------------------------------------
	 * E - enf of file flag
	 * F - frist sefment of a file (flag)
	 * We add +2 to include the first byte and the \0-character
	 */
	int hdr_size = snprintf(buf.data() + 1, buf.size(), "%s", upload_name.c_str()) + 2;

	while (!force_break)
	{
		const int n = (int)ifile.read(buf.data() + hdr_size, streamsize(buf.size() - hdr_size)).gcount();
		const bool is_eof = ifile.eof();
		const bool is_start = hdr_size > 1;
		buf[0] = (is_eof ? 2 : 0) | (is_start ? 1 : 0);

		size_t shift = 0;
		while (n > 0)
		{
			const int st = dst.write(const_buffer(buf.data() + shift, n + hdr_size));
			if (st == 0)
				continue;

			file_size += n;

			if (st == SRT_ERROR)
			{
				cerr << "Upload: SRT error: " << srt_getlasterror_str() << endl;
				return false;
			}
			if (st != n + hdr_size)
			{
				cerr << "Upload error: not full delivery" << endl;
				return false;
			}

			shift += st - hdr_size;
			hdr_size = 1;
			break;
		}

		if (is_eof)
			break;

		if (!ifile.good())
		{
			cerr << "ERROR while reading from file\n";
			return false;
		}
	}

	const chrono::steady_clock::time_point time_end = chrono::steady_clock::now();
	const auto delta_us = chrono::duration_cast<chrono::microseconds>(time_end - time_start).count();

	const size_t rate_kbps = (file_size * 1000) / (delta_us) * 8;
	cerr << "--> done (" << file_size / 1024 << " kbytes transfered at " << rate_kbps << " kbps, took "
		<< chrono::duration_cast<chrono::seconds>(time_end - time_start).count() << " s" << endl;

	return true;
}


/// Enumerate files in the folder and subfolders. Or return file if path is file.
/// With NRVO no copy of the vector being returned will be made
/// @param path    a path to a file or folder to enumerate
/// @return        a list of filenames found in the path
const std::vector<string> read_directory(const string& path)
{
	vector<string> filenames;
	deque<string> subdirs = { path };

	while (!subdirs.empty())
	{
		fs::path p(subdirs.front());
		subdirs.pop_front();

		if (!fs::is_directory(p))
		{
			filenames.push_back(p.string());
			continue;
		}

		for (const auto& entry : filesystem::directory_iterator(p))
		{
			if (entry.is_directory())
				subdirs.push_back(entry.path().string());
			else
				filenames.push_back(entry.path().string());
		}
	}

	return filenames;
}


/// Get file path relative to root directory (upload name).
/// Transmission preserves only relative dir structure.
///
/// @return    filename if filepath matches dirpath (dirpath is a file)
///            relative file path if dirpath is a folder
const string relative_path(const string& filepath, const string &dirpath)
{
	const fs::path dir(dirpath);
	const fs::path file(filepath);
	if (dir == file)
		return file.filename().string();

	const size_t pos = file.string().find(dir.string());
	if (pos != 0)
	{
		cerr << "Failed to find substring" << endl;
		return string();
	}

	return file.generic_string().erase(pos, dir.generic_string().size());
}


void start_filesender(future<shared_srt> connection, const config& cfg,
	const vector<string> &filenames, const atomic_bool& force_break)
{
	if (!connection.valid())
	{
		cerr << "Error: Unexpected socket creation failure!" << endl;
		return;
	}

	const shared_srt sock = connection.get();
	if (!sock)
	{
		cerr << "Error: Unexpected socket connection failure!" << endl;
		return;
	}

	atomic_bool local_break(false);

	auto stats_func = [&cfg, &force_break, &local_break](shared_srt sock) {
		if (cfg.stats_freq_ms == 0)
			return;
		if (cfg.stats_file.empty())
			return;

		ofstream logfile_stats(cfg.stats_file.c_str());
		if (!logfile_stats)
		{
			cerr << "ERROR: Can't open '" << cfg.stats_file << "' for writing stats. No output.\n";
			return;
		}

		bool               print_header = true;
		const milliseconds interval(cfg.stats_freq_ms);
		while (!force_break && !local_break)
		{
			this_thread::sleep_for(interval);

			logfile_stats << sock->get_statistics(cfg.stats_format, print_header) << flush;
			print_header = false;
		}
	};
	auto stats_logger = async(launch::async, stats_func, sock);


	socket::srt dst_sock = *sock.get();

	vector<char> buf(cfg.segment_size);
	for (const string& fname : filenames)
	{
		const bool transmit_res = send_file(fname, relative_path(fname, cfg.src_path),
			dst_sock, buf, force_break);

		if (!transmit_res)
			break;

		if (force_break)
			break;
	}

	// We have to check if the sending buffer is empty.
	// Or we will loose this data, because SRT is not waiting
	// for all the data to be sent in a general live streaming use case,
	// as it might be not something it is expected to do, and may lead to
	// to unnesessary waits on destroy.
	// srt_getsndbuffer() is designed to handle such cases.
	size_t blocks = 0;
	do
	{
		if (SRT_ERROR == srt_getsndbuffer(dst_sock.id(), &blocks, nullptr))
			break;

		if (blocks)
			this_thread::sleep_for(chrono::milliseconds(5));
	} while (blocks != 0);

	local_break = true;
	stats_logger.wait();
}


void xtransmit::file::send::run(const string& dst_url, const config& cfg, const atomic_bool& force_break)
{
	const vector<string> filenames = read_directory(cfg.src_path);

	if (filenames.empty())
	{
		cerr << "Found no files to transmit (path " << cfg.src_path << ")" << endl;
		return;
	}

	if (cfg.only_print)
	{
		cout << "Files found in " << cfg.src_path << endl;

		for_each(filenames.begin(), filenames.end(),
			[&dirpath = std::as_const(cfg.src_path)](const string& fname) {
				cout << fname << endl;
				cout << "RELATIVE: " << relative_path(fname, dirpath) << endl;
			});
		//copy(filenames.begin(), filenames.end(),
		//	ostream_iterator<string>(cout, "\n"));
		return;
	}

	UriParser ut(dst_url);
	ut["transtype"]  = string("file");
	ut["messageapi"] = string("true");
	// Non-blocking mode will not notify when there is enough space in the sender buffer
	// to write a message. Therefore a message can be sent properly only in the blocking mode.
	ut["blocking"] = string("true");
	if (!ut["sndbuf"].exists())
		ut["sndbuf"] = to_string(cfg.segment_size * 10);

	shared_srt socket = make_shared<socket::srt>(ut);
	const bool        accept = socket->mode() == socket::srt::LISTENER;
	try
	{
		start_filesender(accept ? socket->async_accept() : socket->async_connect()
			, cfg, filenames, force_break);
	}
	catch (const socket::exception & e)
	{
		cerr << e.what() << endl;
		return;
	}
}

CLI::App* xtransmit::file::send::add_subcommand(CLI::App& app, config& cfg, string& dst_url)
{
	const map<string, int> to_ms{ {"s", 1'000}, {"ms", 1} };

	CLI::App* sc_file_send = app.add_subcommand("send", "Send file or folder")->fallthrough();
	sc_file_send->add_option("src", cfg.src_path, "Source path to file/folder");
	sc_file_send->add_option("dst", dst_url, "Destination URI");
	sc_file_send->add_flag("--printout", cfg.only_print, "Print files found in a folder ad subfolders. No transfer.");
	sc_file_send->add_option("--segment", cfg.segment_size, "Size of the transmission segment");
	sc_file_send->add_option("--statsfile", cfg.stats_file, "output stats report filename");
	sc_file_send->add_option("--statsformat", cfg.stats_format, "output stats report format (json, csv)");
	sc_file_send->add_option("--statsfreq", cfg.stats_freq_ms, "output stats report frequency (ms)")
		->transform(CLI::AsNumberWithUnit(to_ms, CLI::AsNumberWithUnit::CASE_SENSITIVE));

	return sc_file_send;
}

#endif // ENABLE_FILE_TRANSFER
