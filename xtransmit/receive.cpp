#include <atomic>
#include <chrono>
#include <ctime>
#include <future>
#include <iomanip>
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
using namespace std::chrono;

using shared_srt_socket = std::shared_ptr<srt::socket>;


void trace_message(const size_t bytes, const vector<char> &buffer, int conn_id)
{
	::cout << "RECEIVED MESSAGE length " << bytes << " on conn ID " << conn_id;

	const time_t    send_time    = *(reinterpret_cast<const time_t *>(buffer.data()));
	const long long send_time_us = *(reinterpret_cast<const long long *>(buffer.data() + 8));

	const auto   systime_now = system_clock::now();
	const time_t read_time   = system_clock::to_time_t(systime_now);

	std::tm tm_send = *std::localtime(&send_time);
	::cout << " snd_time " << std::put_time(&tm_send, "%T.") << std::setfill('0') << std::setw(6) << send_time_us;
	std::tm                tm_read = *std::localtime(&read_time);
	system_clock::duration read_time_frac =
	    systime_now.time_since_epoch() - duration_cast<seconds>(systime_now.time_since_epoch());
	::cout << " read_time " << std::put_time(&tm_read, "%T.") << duration_cast<microseconds>(read_time_frac).count();

	const auto delay = systime_now - (system_clock::from_time_t(send_time) + microseconds(send_time_us));
	::cout << " delta " << duration_cast<milliseconds>(delay).count() << " ms";

#if 0
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
#endif
	::cout << endl;
}

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

		const milliseconds interval(cfg.stats_freq_ms);
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
			const size_t bytes = src->read(mutable_buffer(buffer.data(), buffer.size()), 500);

			if (cfg.print_notifications)
				trace_message(bytes, buffer, src->id());

			if (cfg.send_reply)
			{
				const string out_message("Message received");
				src->write(const_buffer(out_message.data(), out_message.size()));

				if (cfg.print_notifications)
					::cout << "Reply sent on conn ID " << src->id() << "\n";
			}
		}
	}
	catch (const srt::socket_exception &)
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
	const bool        accept = socket->mode() == srt::socket::LISTENER;
	start_receiver(accept ? socket->async_accept() : socket->async_connect(), cfg, force_break);
}
