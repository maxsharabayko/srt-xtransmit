#ifdef ENABLE_FILE
#include <iostream>
#include <iterator>
#include <filesystem>	// Requires C++17
#include <string>
#include <vector>

#include "sendfile.h"
#include "srt_socket.hpp"


using namespace std;
using namespace xtransmit;
using namespace xtransmit::file;
namespace fs = std::filesystem;


using shared_srt_socket = std::shared_ptr<srt::socket>;


bool send_file(const string &filename, const string &upload_name, srt::socket &dst, const config &cfg, vector<char> &buf)
{
	ifstream ifile(filename, ios::binary);
	if (!ifile)
	{
		cerr << "Error opening file: '" << filename << "'\n";
		return true;
	}

	const chrono::steady_clock::time_point time_start = chrono::steady_clock::now();
	size_t file_size = 0;

	cerr << "Transmitting '" << filename << " to " << upload_name;

	/*   1 byte      string    1 byte
	 * ------------------------------------------------
	 * | ......EF | Filename | 0     | Payload
	 * ------------------------------------------------
	 * E - enf of file flag
	 * F - frist sefment of a file (flag)
	 * We add +2 to include the first byte and the \0-character
	 */
	int hdr_size = snprintf(buf.data() + 1, buf.size(), "%s", upload_name.c_str()) + 2;

	for (;;)
	{
		const int n = (int)ifile.read(buf.data() + hdr_size, streamsize(buf.size() - hdr_size)).gcount();
		const bool is_eof = ifile.eof();
		const bool is_start = hdr_size > 1;
		buf[0] = (is_eof ? 2 : 0) | (is_start ? 1 : 0);

		size_t shift = 0;
		if (n > 0)
		{
			const int st = dst.write(const_buffer(buf.data() + shift, n + hdr_size));
			file_size += n;

			if (st == SRT_ERROR)
			{
				cerr << "Upload: SRT error: " << srt_getlasterror_str() << endl;
				return false;
			}
			if (st != n + hdr_size) {
				cerr << "Upload error: not full delivery" << endl;
				return false;
			}

			shift += st - hdr_size;
			hdr_size = 1;
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


struct path_leaf_string
{
	string operator()(const filesystem::directory_entry& entry) const
	{
		entry.is_directory();
		return entry.path().string();
	}
};

// TODO: Read subfolders
void read_directory(const string& name, vector<string>& v)
{
	deque<string> subdirs = { name };

	while (!subdirs.empty())
	{
		fs::path p(subdirs.front());
		subdirs.pop_front();

		if (!fs::is_directory(p))
		{
			v.push_back(p.string());
			continue;
		}

		for (const auto& entry : filesystem::directory_iterator(p))
		{
			if (entry.is_directory())
				subdirs.push_back(entry.path().string());
			else
				v.push_back(entry.path().string());
		}
	}
}

//
//bool send_folder(UriParser& ut, string path)
//{
//	std::list<pair<dirent*, string>> processing_list;
//
//	auto get_files = [&processing_list](const std::string& path)
//	{
//		struct dirent** files;
//		const int n = scandir(path.c_str(), &files, NULL, alphasort);
//		if (n < 0)
//		{
//			cerr << "No files found in the directory: '" << path << "'";
//			return false;
//		}
//
//		for (int i = 0; i < n; ++i)
//		{
//			if (0 == strcmp(files[i]->d_name, ".") || 0 == strcmp(files[i]->d_name, ".."))
//			{
//				free(files[i]);
//				continue;
//			}
//
//			processing_list.push_back(pair<dirent*, string>(files[i], path + "/"));
//		}
//
//		free(files);
//		return true;
//	};
//
//	/* Initial scan for files in the directory */
//	get_files(path);
//
//	// Use a manual loop for reading from SRT
//	vector<char> buf(::g_buffer_size);
//
//	while (!processing_list.empty())
//	{
//		dirent* ent = processing_list.front().first;
//		string dir = processing_list.front().second;
//		processing_list.pop_front();
//
//		if (ent->d_type == DT_DIR)
//		{
//			get_files(dir + ent->d_name);
//			free(ent);
//			continue;
//		}
//
//		if (ent->d_type != DT_REG)
//		{
//			free(ent);
//			continue;
//		}
//
//		cerr << "File: '" << dir << ent->d_name << "'\n";
//		const bool transmit_res = send_file(dir + ent->d_name, dir + ent->d_name,
//			m.Socket(), buf);
//		free(ent);
//
//		if (!transmit_res)
//			break;
//	};
//
//	while (!processing_list.empty())
//	{
//		dirent* ent = processing_list.front().first;
//		processing_list.pop_front();
//		free(ent);
//	};
//
//	// We have to check if the sending buffer is empty.
//	// Or we will loose this data, because SRT is not waiting
//	// for all the data to be sent in a general live streaming use case,
//	// as it might be not something it is expected to do, and may lead to
//	// to unnesessary waits on destroy.
//	// srt_getsndbuffer() is designed to handle such cases.
//	const SRTSOCKET sock = m.Socket();
//	size_t blocks = 0;
//	do
//	{
//		if (SRT_ERROR == srt_getsndbuffer(sock, &blocks, nullptr))
//			break;
//
//		if (blocks)
//			this_thread::sleep_for(chrono::milliseconds(5));
//	} while (blocks != 0);
//
//	return true;
//}


void enum_dir(const config& cfg)
{
	vector<string> v;
	read_directory(cfg.src_path, v);
	copy(v.begin(), v.end(),
		ostream_iterator<string>(cout, "\n"));
}


void start_filesender(future<shared_srt_socket> connection, const config& cfg, const atomic_bool& force_break)
{
	if (!connection.valid())
	{
		cerr << "Error: Unexpected socket creation failure!" << endl;
		return;
	}

	const shared_srt_socket sock = connection.get();
	if (!sock)
	{
		cerr << "Error: Unexpected socket connection failure!" << endl;
		return;
	}

	//run(sock, cfg, force_break);
}


void xtransmit::file::send(const string& dst_url, const config& cfg, const atomic_bool& force_break)
{
	return enum_dir(cfg);

	UriParser ut(dst_url);
	ut["transtype"]  = string("file");
	ut["messageapi"] = string("true");
	ut["sndbuf"] = to_string(1061313/*g_buffer_size*/ /* 1456*/);

	shared_srt_socket socket = make_shared<srt::socket>(ut);
	const bool        accept = socket->mode() == srt::socket::LISTENER;
	try
	{
		start_filesender(accept ? socket->async_accept() : socket->async_connect(), cfg, force_break);
	}
	catch (const srt::socket_exception & e)
	{
		cerr << e.what() << endl;
		return;
	}
}

#endif
