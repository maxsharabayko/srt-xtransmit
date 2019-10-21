#include <numeric>
#include <atomic>
#include <chrono>
#include <future>
#include <limits>
#include <memory>
#include <thread>
#include <vector>
#include "srt_socket.hpp"
#include "generate.hpp"

// OpenSRT
#include "apputil.hpp"
#include "uriparser.hpp"

using namespace std;
using namespace chrono;
using namespace xtransmit;
using namespace xtransmit::generate;

using shared_srt_socket = std::shared_ptr<srt::socket>;


void write_timestamp(vector<char> &message_to_send)
{
    const auto   systime_now = system_clock::now();
    const time_t now_c = system_clock::to_time_t(systime_now);
    *(reinterpret_cast<time_t*>(message_to_send.data())) = now_c;

    system_clock::duration frac =
        systime_now.time_since_epoch() - duration_cast<seconds>(systime_now.time_since_epoch());

    *(reinterpret_cast<long long*>(message_to_send.data() + 8)) = duration_cast<microseconds>(frac).count();
}


void run(shared_srt_socket dst, const config &cfg, const atomic_bool &force_break)
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

		bool               print_header = true;
		const milliseconds interval(cfg.stats_freq_ms);
		while (!force_break && !local_break)
		{
			this_thread::sleep_for(interval);

			logfile_stats << sock->statistics_csv(print_header) << flush;
			print_header = false;
		}
	};

	auto stats_logger = async(launch::async, stats_func, dst);

	vector<char> message_to_send(cfg.message_size);
	iota(message_to_send.begin(), message_to_send.end(), (char)0);

	const auto start_time      = steady_clock::now();
	auto       time_prev       = steady_clock::now();
	long       time_dev_us     = 0;
	const long msgs_per_s      = static_cast<long long>(cfg.sendrate / 8) / cfg.message_size;
	const long msg_interval_us = msgs_per_s ? 1000000 / msgs_per_s : 0;

	const int num_messages = cfg.duration > 0 ? -1 : cfg.num_messages;

	srt::socket *target = dst.get();

	for (int i = 0; (num_messages < 0 || i < num_messages) && !force_break; ++i)
	{
		if (cfg.sendrate)
		{
			const long               duration_us = time_dev_us > msg_interval_us ? 0 : (msg_interval_us - time_dev_us);
			const auto               next_time   = time_prev + microseconds(duration_us);
			steady_clock::time_point time_now;
			for (;;)
			{
				time_now = steady_clock::now();
				if (time_now >= next_time)
					break;
				if (force_break)
					break;
			}

			time_dev_us += (long)duration_cast<microseconds>(time_now - time_prev).count() - msg_interval_us;
			time_prev = time_now;
		}

		// Check if sending duration is respected
		if (cfg.duration > 0 && (steady_clock::now() - start_time > seconds(cfg.duration)))
		{
			break;
		}

		// write_timestamp(message_to_send);

		target->write(const_buffer(message_to_send.data(), message_to_send.size()));
	}

	local_break = true;
	stats_logger.wait();
}

void start_generator(future<shared_srt_socket> connection, const config &cfg, const atomic_bool &force_break)
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

	run(sock, cfg, force_break);
}

void xtransmit::generate::generate_main(const string &dst_url, const config &cfg, const atomic_bool &force_break)
{
	shared_srt_socket socket = make_shared<srt::socket>(UriParser(dst_url));
	const bool        accept = socket->mode() == srt::socket::LISTENER;
	try
	{
		start_generator(accept ? socket->async_accept() : socket->async_connect(), cfg, force_break);
	}
	catch (const srt::socket_exception &e)
	{
		cerr << e.what() << endl;
		return;
	}
}
