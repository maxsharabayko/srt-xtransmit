#include <numeric>
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <thread>
#include <vector>
#include "srt_socket.hpp"
#include "generate.hpp"

// OpenSRT
#include "apputil.hpp"
#include "uriparser.hpp"

using namespace std;
using namespace xtransmit;
using namespace xtransmit::generate;

using shared_srt_socket = std::shared_ptr<srt::socket>;

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

		bool                       print_header = true;
		const chrono::milliseconds interval(cfg.stats_freq_ms);
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

	auto       time_prev       = chrono::steady_clock::now();
	long       time_dev_us     = 0;
	const long msgs_per_s      = static_cast<long long>(cfg.bitrate / 8) / cfg.message_size;
	const long msg_interval_us = msgs_per_s ? 1000000 / msgs_per_s : 0;

	const int num_messages = (cfg.duration != 0 && cfg.bitrate != 0) ? cfg.duration * msgs_per_s : cfg.num_messages;

	srt::socket *target = dst.get();

	for (int i = 0; (num_messages < 0 || i < num_messages) && !force_break; ++i)
	{
		if (cfg.bitrate)
		{
			const long duration_us = time_dev_us > msg_interval_us ? 0 : (msg_interval_us - time_dev_us);
			const auto next_time   = time_prev + chrono::microseconds(duration_us);
			chrono::time_point<chrono::steady_clock> time_now;
			for (;;)
			{
				time_now = chrono::steady_clock::now();
				if (time_now >= next_time)
					break;
				if (force_break)
					break;
			}

			time_dev_us +=
				(long)chrono::duration_cast<chrono::microseconds>(time_now - time_prev).count() - msg_interval_us;
			time_prev = time_now;
		}

		const auto   systime_now                           = chrono::system_clock::now();
		const time_t now_c                                 = chrono::system_clock::to_time_t(systime_now);
		*(reinterpret_cast<time_t *>(message_to_send.data())) = now_c;

		chrono::system_clock::duration frac = systime_now.time_since_epoch() -
			chrono::duration_cast<chrono::seconds>(systime_now.time_since_epoch());

		*(reinterpret_cast<long long *>(message_to_send.data() + 8)) = chrono::duration_cast<chrono::microseconds>(frac).count();

		target->write(boost::asio::const_buffer(message_to_send.data(), message_to_send.size()));
	}

	local_break = true;
	stats_logger.wait();
}

void start_generator(future<shared_srt_socket> connection, const config &cfg, const atomic_bool &force_break)
{
	try
	{
		const shared_srt_socket sock = connection.get();
		run(sock, cfg, force_break);
	}
	catch (const srt::socket_exception &e)
	{
		cerr << e.what() << endl;
		return;
	}
}

void xtransmit::generate::generate_main(const string &dst_url, const config &cfg, const atomic_bool &force_break)
{
	shared_srt_socket socket = make_shared<srt::socket>(UriParser(dst_url));
	const bool accept = socket->mode() == srt::socket::LISTENER;
	start_generator(accept ? socket->async_accept() : socket->async_connect(), cfg, force_break);
}
