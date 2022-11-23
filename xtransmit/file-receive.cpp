#if ENABLE_FILE_TRANSFER
#include <iostream>
#include <iterator>
#include <filesystem>	// Requires C++17
#include <functional>
#include <string>
#include <vector>
#include <deque>
#include <chrono>
#include <thread>

#include "file-receive.hpp"
#include "srt_socket.hpp"


using namespace std;
using namespace std::chrono;
using namespace xtransmit;
using namespace xtransmit::file::receive;
namespace fs = std::filesystem;


using shared_srt = std::shared_ptr<socket::srt>;


// Returns true on success, false on error
bool create_folder(const string& path)
{
	// If the function fails because p resolves to an existing directory, no error is reported.
	if (fs::create_directory(path))
	{
		cerr << "Directory '" << path << "' was successfully created" << endl;
		return true;
	}

	cerr << "Directory '" << path << "' failed to be created" << endl;
	return false;
}


// Returns true on success, false on error
// TODO: avoid multiple serial delimiters
bool create_subfolders(const string& path)
{
	size_t found = path.find("./");
	if (found == std::string::npos)
	{
		found = path.find(".\\");
	}

	size_t pos = found != std::string::npos ? (found + 2) : 0;
	const size_t last_delim = path.find_last_of("/\\");
	if (last_delim == string::npos || last_delim < pos)
	{
		cerr << "No folders to create\n";
		return true;
	}

	while (pos != std::string::npos && pos != last_delim)
	{
		pos = path.find_first_of("\\/", pos + 1);
		cerr << "Creating folder " << path.substr(0, pos) << "\n";
		if (!create_folder(path.substr(0, pos).c_str()))
			return false;
	};

	return true;
}



bool receive_files(socket::srt& src, const string& dstpath
	, vector<char>& buf, const atomic_bool& force_break)
{
	cerr << "Downloading to '" << dstpath << endl;

	chrono::steady_clock::time_point time_start;
	size_t file_size = 0;

	ofstream ofile;
	while (!force_break)
	{
		const size_t bytes = src.read(mutable_buffer(buf.data(), buf.size()), -1);
		if (bytes == 0)
			continue;

		int hdr_size = 1;
		const bool is_first = (buf[0] & 0x01) != 0;
		const bool is_eof = (buf[0] & 0x02) != 0;

		if (is_first)
		{
			ofile.close();
			// extranct the filename from the received buffer
			string filename = string(buf.data() + 1);
			hdr_size += filename.size() + 1;    // 1 for null character

			create_subfolders(filename);

			ofile.open(filename.c_str(), ios::out | ios::trunc | ios::binary);
			if (!ofile) {
				cerr << "Download: error opening file " << filename << endl;
				break;
			}

			cerr << "Downloading: --> " << filename;
			time_start = chrono::steady_clock::now();
			file_size = 0;
		}

		if (!ofile)
		{
			cerr << "Download: file is closed while data is received: first packet missed?\n";
			continue;
		}

		ofile.write(buf.data() + hdr_size, bytes - hdr_size);
		file_size += bytes - hdr_size;

		if (is_eof)
		{
			ofile.close();
			const chrono::steady_clock::time_point time_end = chrono::steady_clock::now();
			const auto delta_us = chrono::duration_cast<chrono::microseconds>(time_end - time_start).count();

			const size_t rate_kbps = (file_size * 1000) / (delta_us ? delta_us : 1) * 8;
#if ENABLE_JSON_INPUT
			const auto delta_ms = chrono::duration_cast<chrono::milliseconds>(time_end - time_start).count();
			cerr << "--> done (" << file_size / 1024 << " kbytes transfered at " << rate_kbps << " kbps, took "
				<< delta_ms / 1000.0 << " sec)";
#else
			cerr << "--> done (" << file_size / 1024 << " kbytes transfered at " << rate_kbps << " kbps, took "
				<< chrono::duration_cast<chrono::minutes>(time_end - time_start).count() << " minute(s))";
#endif
		}
	}

	return true;
}



void start_filereceiver(future<shared_srt> connection, const config& cfg,
	const atomic_bool& force_break)
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

	vector<char> buf(cfg.segment_size);
	receive_files(*sock.get(), cfg.dst_path, buf, force_break);

	local_break = true;
	stats_logger.wait();
}


void xtransmit::file::receive::run(const string& src_url, const config& cfg, const atomic_bool& force_break)
{
	UriParser ut(src_url);
	ut["transtype"] = string("file");
	ut["messageapi"] = string("true");
	if (!ut["rcvbuf"].exists())
		ut["rcvbuf"] = to_string(cfg.segment_size * 10);

	shared_srt socket = make_shared<socket::srt>(ut);
	const bool accept = socket->mode() == socket::srt::LISTENER;
	try
	{
		start_filereceiver(accept ? socket->async_accept() : socket->async_connect()
			, cfg, force_break);
	}
	catch (const socket::exception & e)
	{
		cerr << e.what() << endl;
		return;
	}
}


CLI::App* xtransmit::file::receive::add_subcommand(CLI::App& app, config& cfg, string& src_url)
{
	const map<string, int> to_ms{ {"s", 1'000}, {"ms", 1} };

	CLI::App* sc_file_recv = app.add_subcommand("receive", "Receive file or folder")->fallthrough();
	sc_file_recv->add_option("src", src_url, "Source URI");
	sc_file_recv->add_option("dst", cfg.dst_path, "Destination path to file/folder");
	sc_file_recv->add_option("--segment", cfg.segment_size, "Size of the transmission segment");
	sc_file_recv->add_option("--statsfile", cfg.stats_file, "output stats report filename");
  sc_file_recv->add_option("--statsformat", cfg.stats_format, "output stats report format(json, csv)");
	sc_file_recv->add_option("--statsfreq", cfg.stats_freq_ms, "output stats report frequency (ms)")
		->transform(CLI::AsNumberWithUnit(to_ms, CLI::AsNumberWithUnit::CASE_SENSITIVE));

	return sc_file_recv;
}

#endif // ENABLE_FILE_TRANSFER
