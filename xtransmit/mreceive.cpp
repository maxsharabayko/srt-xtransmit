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
#include "mreceive.hpp"
#include "metrics.hpp"
#include "thread_io.hpp"

// OpenSRT
#include "apputil.hpp"
#include "uriparser.hpp"
#include "threadname.h"

using namespace std;
using namespace xtransmit;
using namespace xtransmit::mreceive;
using namespace std::chrono;

using shared_srt = std::shared_ptr<socket::srt>;
using shared_sock = std::shared_ptr<socket::isocket>;

#define LOG_SC_RECEIVE "MRCV "


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
			metrics::writing_loop,
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

			if (cfg.enable_metrics)
			{
				lock_guard<mutex> lck(metrics_mtx);
				validator.validate_packet(const_buffer(buffer.data(), bytes));
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

void xtransmit::mreceive::run(const std::vector<std::string>& src_urls,
	const config& cfg,
	const atomic_bool& force_break)
{
	using namespace std::placeholders;
	io_dispatch io;

	std::function<void(shared_sock_t, const std::atomic_bool&)> on_connect_fn = [&io, cfg](shared_sock_t sock, const std::atomic_bool& force_break)
	{
		auto read_fn = [cfg](shared_sock_t& src)
		{
			try
			{
				vector<char>       buffer(cfg.message_size);
				socket::isocket& sock = *src.get();
				const size_t bytes = sock.read(mutable_buffer(buffer.data(), buffer.size()), -1);

				if (bytes == 0)
				{
					spdlog::debug(LOG_SC_RECEIVE "sock::read() returned 0 bytes (spurious read ready?). Retrying.");
					return;
				}
			}
			catch (const socket::exception& e)
			{
				spdlog::warn(LOG_SC_RECEIVE "{}", e.what());
			}
		};

		const int events = SRT_EPOLL_IN | SRT_EPOLL_ERR;
		io.add(sock, force_break, events, read_fn);
	};

	common_run(src_urls, cfg, cfg.reconnect, force_break, on_connect_fn);
}

CLI::App* xtransmit::mreceive::add_subcommand(CLI::App& app, config& cfg, std::vector<std::string>& src_urls)
{
	const map<string, int> to_ms{{"s", 1000}, { "ms", 1 }};

	CLI::App* sc_receive = app.add_subcommand("mreceive", "Receive data from multiple sockets (SRT, UDP)")->fallthrough();
	sc_receive->add_option("-i,--input,src", src_urls, "Source URI");
	sc_receive->add_option("--msgsize", cfg.message_size, fmt::format("Size of the buffer to receive message payload (default {})", cfg.message_size));
	sc_receive->add_option("--statsfile", cfg.stats_file, "Output stats report filename");
	sc_receive->add_option("--statsformat", cfg.stats_format, "Output stats report format (csv - default, json)");
	sc_receive->add_option("--statsfreq", cfg.stats_freq_ms, fmt::format("Output stats report frequency, ms (default {})", cfg.stats_freq_ms))
		->transform(CLI::AsNumberWithUnit(to_ms, CLI::AsNumberWithUnit::CASE_SENSITIVE));
	sc_receive->add_flag("--printmsg", cfg.print_notifications, "Print message to stdout");
	sc_receive->add_flag("--reconnect", cfg.reconnect, "Reconnect automatically");
	sc_receive->add_flag("--enable-metrics", cfg.enable_metrics, "Enable checking metrics: jitter, latency, etc.");
	sc_receive->add_option("--metricsfile", cfg.metrics_file, "Metrics output filename (default stdout)");
	sc_receive->add_option("--metricsfreq", cfg.metrics_freq_ms, fmt::format("Metrics report frequency, ms (default {})", cfg.metrics_freq_ms))
		->transform(CLI::AsNumberWithUnit(to_ms, CLI::AsNumberWithUnit::CASE_SENSITIVE));

	return sc_receive;
}
