#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <thread>
#include <vector>

#include "srt_socket.hpp"
#include "receive.hpp"

// OpenSRT
#include "apputil.hpp"
#include "uriparser.hpp"

using namespace std;
using namespace xtransmit;
using namespace xtransmit::receive;

using shared_srt_socket = std::shared_ptr<srt::socket>;

void run(shared_srt_socket src, const config &cfg, const atomic_bool &force_break)
{
	atomic_bool local_break(false);

	auto stats_func = [&cfg, &force_break, &local_break](shared_srt_socket sock) {
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

		bool print_header = true;

		const chrono::milliseconds interval(cfg.stats_freq_ms);
		while (!force_break && !local_break)
		{
			this_thread::sleep_for(interval);

			logfile_stats << sock->statistics_csv(print_header) << flush;
			print_header = false;
		}
	};

	auto stats_logger = async(launch::async, stats_func, src);

	vector<char> buffer(cfg.message_size);
	try
	{
		while (!force_break)
		{
			const size_t bytes = src->read(boost::asio::mutable_buffer(buffer.data(), buffer.size()), 500);

			if (cfg.print_notifications)
			{
				::cout << "RECEIVED MESSAGE length " << bytes << " on conn ID " << src->id();
				if (bytes < 50)
				{
					::cout << ":\n";
					::cout << string(buffer.data(), bytes).c_str();
				}
				else if (buffer[0] >= '0' && buffer[0] <= 'z')
				{
					::cout << " (first character):";
					::cout << buffer[0];
				}
				::cout << endl;
			}

			if (cfg.send_reply)
			{
				const string out_message("Message received");
				src->write(boost::asio::const_buffer(out_message.data(), out_message.size()));

				if (cfg.print_notifications)
					::cout << "Reply sent on conn ID " << src->id() << "\n";
			}
		}
	}
	catch (const srt::socket_exception &e)
	{
		local_break = true;
	}

	if (force_break)
	{
		cerr << "\n (interrupted on request)\n";
	}

	local_break = true;
	stats_logger.wait();
}

void start_receiver(future<shared_srt_socket> &&connection, const config &cfg, const atomic_bool &force_break)
{
	try
	{
		const shared_srt_socket sock = connection.get();
		run(sock, cfg, force_break);
	}
	catch (const srt::socket_exception &e)
	{
		std::cerr << e.what();
		return;
	}
}

void xtransmit::receive::receive_main(const string &url, const config &cfg, const atomic_bool &force_break)
{
	shared_srt_socket socket = make_shared<srt::socket>(UriParser(url));
	start_receiver(socket->async_accept(), cfg, force_break);
}
