#include <numeric>
#include <atomic>
#include <chrono>
#include <future>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "srt_socket.hpp"
#include "udp_socket.hpp"
#include "generate.hpp"

// OpenSRT
#include "apputil.hpp"
#include "uriparser.hpp"

using namespace std;
using namespace chrono;
using namespace xtransmit;
using namespace xtransmit::generate;

using shared_srt  = std::shared_ptr<socket::srt>;
using shared_sock = std::shared_ptr<socket::isocket>;


void write_timestamp(vector<char> &message_to_send)
{
	const auto   systime_now = system_clock::now();
	const time_t now_c = system_clock::to_time_t(systime_now);
	*(reinterpret_cast<time_t*>(message_to_send.data())) = now_c;

	system_clock::duration frac =
		systime_now.time_since_epoch() - duration_cast<seconds>(systime_now.time_since_epoch());

	*(reinterpret_cast<long long*>(message_to_send.data() + 8)) = duration_cast<microseconds>(frac).count();
}


void run_pipe(shared_sock dst, const config &cfg, const atomic_bool &force_break)
{
	atomic_bool local_break(false);

	auto stats_func = [&cfg, &force_break, &local_break](shared_sock sock) {
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

	socket::isocket *target = dst.get();

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

		if (cfg.add_timestamp)
			write_timestamp(message_to_send);

		target->write(const_buffer(message_to_send.data(), message_to_send.size()));
	}

	local_break = true;
	stats_logger.wait();
}


void xtransmit::generate::run(const string &dst_url, const config &cfg, const atomic_bool &force_break)
{
	const UriParser uri(dst_url);

	shared_sock socket;
	shared_sock connection;

	try
	{
		if (uri.proto() == "udp")
		{
			connection = make_shared<socket::udp>(uri);
		}
		else
		{
			socket = make_shared<socket::srt>(uri);
			socket::srt* s = static_cast<socket::srt *>(socket.get());
			const bool  accept = s->mode() == socket::srt::LISTENER;
			if (accept)
				s->listen();
			connection = accept ? s->accept() : s->connect();
		}

		run_pipe(connection, cfg, force_break);
	}
	catch (const socket::exception &e)
	{
		cerr << e.what() << endl;
		return;
	}
}

CLI::App* xtransmit::generate::add_subcommand(CLI::App &app, config &cfg, string &dst_url)
{
	const map<string, int> to_bps{ {"kbps", 1000}, {"Mbps", 1000000}, {"Gbps", 1000000000} };
	const map<string, int> to_ms{ {"s", 1000}, {"ms", 1} };
	const map<string, int> to_sec{ {"s", 1}, {"min", 60}, {"mins", 60} };

	CLI::App* sc_generate = app.add_subcommand("generate", "Send generated data (SRT, UDP)")->fallthrough();
	sc_generate->add_option("dst", dst_url, "Destination URI");
	sc_generate->add_option("--msgsize", cfg.message_size, "Size of a message to send");
	sc_generate->add_option("--sendrate", cfg.sendrate, "Bitrate to generate")
		->transform(CLI::AsNumberWithUnit(to_bps, CLI::AsNumberWithUnit::CASE_SENSITIVE));
	sc_generate->add_option("--num", cfg.num_messages, "Number of messages to send (-1 for infinite)");
	sc_generate->add_option("--duration", cfg.duration, "Sending duration in seconds (supresses --num option)")
		->transform(CLI::AsNumberWithUnit(to_sec, CLI::AsNumberWithUnit::CASE_SENSITIVE));
	sc_generate->add_option("--statsfile", cfg.stats_file, "output stats report filename");
	sc_generate->add_option("--statsfreq", cfg.stats_freq_ms, "output stats report frequency (ms)")
		->transform(CLI::AsNumberWithUnit(to_ms, CLI::AsNumberWithUnit::CASE_SENSITIVE));
	sc_generate->add_flag("--twoway", cfg.two_way, "Both send and receive data");
	sc_generate->add_flag("--timestamp", cfg.add_timestamp, "Place a timestamp in the message payload");

	return sc_generate;
}




