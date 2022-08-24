#include <atomic>
#include <chrono>
#include <ctime>
#include <functional>
#include <future>
#include <iomanip>
#include <memory>
#include <thread>
#include <vector>

// submodules
#include "spdlog/spdlog.h"

// xtransmit
#include "socket_stats.hpp"
#include "misc.hpp"
#include "receive.hpp"
#include "metrics.hpp"

// OpenSRT
#include "apputil.hpp"
#include "uriparser.hpp"

using namespace std;
using namespace xtransmit;
using namespace xtransmit::receive;
using namespace std::chrono;

using shared_srt  = std::shared_ptr<socket::srt>;
using shared_sock = std::shared_ptr<socket::isocket>;

#define LOG_SC_RECEIVE "RECEIVE "

void trace_message(const size_t bytes, const vector<char>& buffer, SOCKET conn_id)
{
	::cout << "RECEIVED MESSAGE length " << bytes << " on conn ID " << conn_id;

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

	// CHandShake hs;
	// if (hs.load_from(buffer.data(), buffer.size()) < 0)
	//	return;
	//
	//::cout << "SRT HS: " << hs.show() << endl;
}

/// @brief
/// @param metrics_file
/// @param validator
/// @param mtx mutex to protect access to validator
/// @param freq
/// @param force_break
void metrics_writing_loop(ofstream&                   metrics_file,
						  metrics::validator&         validator,
						  mutex&                      mtx,
						  const chrono::milliseconds& freq,
						  const atomic_bool&          force_break)
{
	auto stat_time = steady_clock::now();
	while (!force_break)
	{
		const auto tnow = steady_clock::now();
		if (tnow >= stat_time)
		{
			if (metrics_file.is_open())
			{
				lock_guard<mutex> lck(mtx);
				metrics_file << validator.stats_csv(false);
			}
			else
			{
				lock_guard<mutex> lck(mtx);
				const auto        stats_str = validator.stats();
				spdlog::info(LOG_SC_RECEIVE "{}", stats_str);
			}
			stat_time += freq;
		}

		std::this_thread::sleep_until(stat_time);
	}
}

void run_pipe(shared_sock src, const config& cfg, const atomic_bool& force_break)
{
	socket::isocket& sock = *src.get();

	vector<char>       buffer(cfg.message_size);
	metrics::validator validator;

	atomic_bool  metrics_stop(false);
	mutex        metrics_mtx;
	future<void> metrics_th;
	ofstream     metrics_file;
	if (cfg.enable_metrics && cfg.metrics_freq_ms > 0)
	{
		if (!cfg.metrics_file.empty())
		{
			metrics_file.open(cfg.metrics_file, std::ofstream::out);
			if (!metrics_file)
			{
				spdlog::error(LOG_SC_RECEIVE "Failed to open metrics file {} for output", cfg.metrics_file);
				return;
			}
			metrics_file << validator.stats_csv(true);
		}

		metrics_th = async(::launch::async,
						   metrics_writing_loop,
						   ref(metrics_file),
						   ref(validator),
						   ref(metrics_mtx),
						   chrono::milliseconds(cfg.metrics_freq_ms),
						   ref(metrics_stop));
	}

	try
	{
		while (!force_break)
		{
			const size_t bytes = sock.read(mutable_buffer(buffer.data(), buffer.size()), -1);

			if (bytes == 0)
			{
				spdlog::debug(LOG_SC_RECEIVE "sock::read() returned 0 bytes (spurious read ready?). Retrying.");
				continue;
			}

			if (cfg.print_notifications)
				trace_message(bytes, buffer, sock.id());
			if (cfg.enable_metrics)
			{
				lock_guard<mutex> lck(metrics_mtx);
				validator.validate_packet(buffer);
			}

			if (cfg.send_reply)
			{
				const string out_message("Message received");
				sock.write(const_buffer(out_message.data(), out_message.size()));

				if (cfg.print_notifications)
					spdlog::error(LOG_SC_RECEIVE "Reply sent on conn ID {}", sock.id());
			}
		}
	}
	catch (const socket::exception& e)
	{
		spdlog::warn(LOG_SC_RECEIVE "{}", e.what());
	}

	metrics_stop = true;
	if (metrics_th.valid())
		metrics_th.get();

	if (force_break)
	{
		spdlog::info(LOG_SC_RECEIVE "interrupted by request!");
	}
}

void xtransmit::receive::run(const std::vector<std::string>& src_urls,
							 const config&                   cfg,
							 const atomic_bool&              force_break)
{
	using namespace std::placeholders;
	processing_fn_t process_fn = std::bind(run_pipe, _1, cfg, _2);
	common_run(src_urls, cfg, cfg.reconnect, force_break, process_fn);
}

CLI::App* xtransmit::receive::add_subcommand(CLI::App& app, config& cfg, std::vector<std::string>& src_urls)
{
	const map<string, int> to_ms{{"s", 1000}, {"ms", 1}};

	CLI::App* sc_receive = app.add_subcommand("receive", "Receive data (SRT, UDP)")->fallthrough();
	sc_receive->add_option("-i,--input,src", src_urls, "Source URI");
	sc_receive->add_option("--msgsize", cfg.message_size, "Size of a buffer to receive message payload");
	sc_receive->add_option("--statsfile", cfg.stats_file, "output stats report filename");
	sc_receive->add_option("--statsfreq", cfg.stats_freq_ms, "output stats report frequency (ms)")
		->transform(CLI::AsNumberWithUnit(to_ms, CLI::AsNumberWithUnit::CASE_SENSITIVE));
	sc_receive->add_flag("--printmsg", cfg.print_notifications, "print message into to stdout");
	sc_receive->add_flag("--reconnect", cfg.reconnect, "Reconnect automatically");
	sc_receive->add_flag("--enable-metrics", cfg.enable_metrics, "Enable checking metrics: jitter, latency, etc.");
	sc_receive->add_option("--metricsfile", cfg.metrics_file, "Metrics output filename (stdout if not set)");
	sc_receive->add_option("--metricsfreq", cfg.metrics_freq_ms, "Metrics report frequency")
		->transform(CLI::AsNumberWithUnit(to_ms, CLI::AsNumberWithUnit::CASE_SENSITIVE));
	sc_receive->add_flag("--twoway", cfg.send_reply, "Both send and receive data");

	return sc_receive;
}
