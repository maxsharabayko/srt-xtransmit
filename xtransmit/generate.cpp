#include <numeric>
#include <atomic>
#include <chrono>
#include <future>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// submodules
#include "spdlog/spdlog.h"

// xtransmit
#include "socket_stats.hpp"
#include "srt_socket.hpp"
#include "srt_socket_group.hpp"
#include "udp_socket.hpp"
#include "generate.hpp"
#include "pacer.hpp"
#include "metrics.hpp"

// OpenSRT
#include "apputil.hpp"
#include "uriparser.hpp"

using namespace std;
using namespace chrono;
using namespace xtransmit;
using namespace xtransmit::generate;

using shared_srt  = std::shared_ptr<socket::srt>;
using shared_sock = std::shared_ptr<socket::isocket>;

#define LOG_SC_GENERATE "GENERATE "

void run_pipe(shared_sock dst, const config& cfg, const atomic_bool& force_break)
{
	vector<char> message_to_send(cfg.message_size);

	const auto start_time   = steady_clock::now();
	const int  num_messages = cfg.duration > 0 ? -1 : cfg.num_messages;

	socket::isocket* target = dst.get();

	metrics::generator pldgen(cfg.enable_metrics);

	auto stat_time = steady_clock::now();
	int  prev_i    = 0;

	unique_ptr<ipacer> ratepacer =
		cfg.sendrate ? unique_ptr<ipacer>(new pacer(cfg.sendrate, cfg.message_size))
					 : (!cfg.playback_csv.empty() ? unique_ptr<ipacer>(new csv_pacer(cfg.playback_csv)) : nullptr);

	try
	{
		for (int i = 0; (num_messages < 0 || i < num_messages) && !force_break; ++i)
		{
			if (ratepacer)
			{
				ratepacer->wait(force_break);
			}

			// Check if sending duration is respected
			if (cfg.duration > 0 && (steady_clock::now() - start_time > seconds(cfg.duration)))
			{
				break;
			}

			pldgen.generate_payload(message_to_send);

			target->write(const_buffer(message_to_send.data(), message_to_send.size()));

			const auto tnow = steady_clock::now();
			if (tnow > (stat_time + chrono::seconds(1)))
			{
				const int       n       = i - prev_i;
				const auto      elapsed = tnow - stat_time;
				const long long bps     = (8 * n * cfg.message_size) / duration_cast<milliseconds>(elapsed).count() * 1000;
				spdlog::info(LOG_SC_GENERATE "Sending at {} kbps", bps / 1000);
				stat_time = tnow;
				prev_i    = i;
			}
		}
	}
	catch (const socket::exception& e)
	{
		spdlog::warn(LOG_SC_GENERATE "{}", e.what());
	}

	if (force_break)
	{
		spdlog::info(LOG_SC_GENERATE "interrupted by request!");
	}
}

void xtransmit::generate::run(const vector<string>& dst_urls, const config& cfg, const atomic_bool& force_break)
{
	if (dst_urls.empty())
	{
		spdlog::error(LOG_SC_GENERATE "No destination URI was provided");
		return;
	}

	vector<UriParser> urls;
	for (const string& url : dst_urls)
	{
		urls.emplace_back(UriParser(url));
	}

	const bool write_stats = cfg.stats_file != "" && cfg.stats_freq_ms > 0;
	// make_unique is not supported by GCC 4.8, only starting from GCC 4.9 :(
	unique_ptr<socket::stats_writer> stats;

	if (write_stats)
	{
		try {
			stats = unique_ptr<socket::stats_writer>(
				new socket::stats_writer(cfg.stats_file, milliseconds(cfg.stats_freq_ms)));
		}
		catch (const socket::exception& e)
		{
			spdlog::error(LOG_SC_GENERATE "{}", e.what());
			return;
		}
	}

	do {
		try
		{
			shared_sock sock;
			shared_sock connection;

			if (urls.size() == 1)
			{
				if (urls[0].proto() == "udp")
				{
					connection = make_shared<socket::udp>(urls[0]);
				}
				else
				{
					sock = make_shared<socket::srt>(urls[0]);
					socket::srt* s = dynamic_cast<socket::srt*>(sock.get());
					const bool   accept = s->mode() == socket::srt::LISTENER;
					if (accept)
						s->listen();
					connection = accept ? s->accept() : s->connect();
				}
			}
			else
			{
				sock = make_shared<socket::srt_group>(urls);
				socket::srt_group* s = dynamic_cast<socket::srt_group*>(sock.get());
				const bool   accept = s->mode() == socket::srt_group::LISTENER;
				if (accept)
				{
					s->listen();
				}
				connection = accept ? s->accept() : s->connect();
			}

			if (stats)
				stats->add_socket(connection);
			run_pipe(connection, cfg, force_break);
			if (stats && cfg.reconnect)
				stats->clear();
		}
		catch (const socket::exception& e)
		{
			spdlog::warn(LOG_SC_GENERATE "{}", e.what());
			if (stats)
				stats->clear();
		}
	} while (cfg.reconnect && !force_break);
}

CLI::App* xtransmit::generate::add_subcommand(CLI::App& app, config& cfg, vector<string>& dst_urls)
{
	const map<string, int> to_bps{{"kbps", 1000}, {"Mbps", 1000000}, {"Gbps", 1000000000}};
	const map<string, int> to_ms{{"s", 1000}, {"ms", 1}};
	const map<string, int> to_sec{{"s", 1}, {"min", 60}, {"mins", 60}};

	CLI::App* sc_generate = app.add_subcommand("generate", "Send generated data (SRT, UDP)")->fallthrough();
	sc_generate->add_option("dst", dst_urls, "Destination URI")->expected(1, 10);
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
	sc_generate->add_flag("--reconnect", cfg.reconnect, "Reconnect automatically");
	sc_generate->add_flag("--enable-metrics", cfg.enable_metrics, "Enable all metrics: latency, loss, reordering, jitter, etc.");
	sc_generate->add_option("--playback-csv", cfg.playback_csv, "Input CSV file with timestamp of every packet");

	return sc_generate;
}
