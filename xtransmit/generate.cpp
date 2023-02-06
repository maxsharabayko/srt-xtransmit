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
#include "misc.hpp"
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
	iota(message_to_send.begin(), message_to_send.end(), (char)0);

	const auto start_time   = steady_clock::now();
	const int  num_messages = cfg.duration > 0 ? -1 : cfg.num_messages;

	socket::isocket* target = dst.get();

	metrics::generator pldgen(cfg.enable_metrics);

	auto stat_time = steady_clock::now();
	int  prev_i    = 0;

	unique_ptr<ipacer> ratepacer =
		cfg.sendrate ? unique_ptr<ipacer>(new pacer(cfg.sendrate, cfg.message_size, cfg.spin_wait))
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

void xtransmit::generate::run(const std::vector<std::string>& dst_urls, const config& cfg, const atomic_bool& force_break)
{
	using namespace std::placeholders;
	processing_fn_t process_fn = std::bind(run_pipe, _1, cfg, _2);
	common_run(dst_urls, cfg, cfg.reconnect, force_break, process_fn);
}

CLI::App* xtransmit::generate::add_subcommand(CLI::App& app, config& cfg, std::vector<std::string>& dst_urls)
{
	const map<string, int> to_bps{{"kbps", 1000}, {"Mbps", 1000000}, {"Gbps", 1000000000}};
	const map<string, int> to_ms{{"s", 1000}, {"ms", 1}};
	const map<string, int> to_sec{{"s", 1}, {"min", 60}, {"mins", 60}};

	CLI::App* sc_generate = app.add_subcommand("generate", "Send generated data (SRT, UDP)")->fallthrough();
	sc_generate->add_option("-o,--output,dst", dst_urls, "Destination URI");
	sc_generate->add_option("--msgsize", cfg.message_size, fmt::format("Size of a message to send (default {})", cfg.message_size));
	sc_generate->add_option("--sendrate", cfg.sendrate, "Bitrate to generate (default 0 - no limit)")
		->transform(CLI::AsNumberWithUnit(to_bps, CLI::AsNumberWithUnit::CASE_SENSITIVE));
	sc_generate->add_option("--num", cfg.num_messages, "Number of messages to send (default -1 - no limit)");
	sc_generate->add_option("--duration", cfg.duration, "Sending duration in seconds (supresses --num option, default 0 - no limit)")
		->transform(CLI::AsNumberWithUnit(to_sec, CLI::AsNumberWithUnit::CASE_SENSITIVE));
	sc_generate->add_option("--statsfile", cfg.stats_file, "Sutput stats report filename");
	sc_generate->add_option("--statsformat", cfg.stats_format, "Output stats report format (csv - default, json)");
	sc_generate->add_option("--statsfreq", cfg.stats_freq_ms, fmt::format("Output stats report frequency, ms (default {})", cfg.stats_freq_ms))
		->transform(CLI::AsNumberWithUnit(to_ms, CLI::AsNumberWithUnit::CASE_SENSITIVE));
	sc_generate->add_flag("--twoway", cfg.two_way, "Both send and receive data");
	sc_generate->add_flag("--reconnect", cfg.reconnect, "Reconnect automatically");
	sc_generate->add_flag("--enable-metrics", cfg.enable_metrics, "Enable embeding metrics: latency, loss, reordering, jitter, etc.");
	sc_generate->add_option("--playback-csv", cfg.playback_csv, "Input CSV file with timestamp of every packet");
	sc_generate->add_flag("--spin-wait", cfg.spin_wait, "Use CPU-expensive spin waiting for better sending accuracy");

	return sc_generate;
}
