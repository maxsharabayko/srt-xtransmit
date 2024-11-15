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
#include "metrics_writer.hpp"
#include "xtr_defs.hpp"

// OpenSRT
#include "apputil.hpp"
#include "uriparser.hpp"

using std::vector;
using std::unique_ptr;
using std::atomic_bool;
using std::string;
using namespace xtransmit;
using namespace xtransmit::receive;
using namespace std::chrono;

using shared_srt  = std::shared_ptr<socket::srt>;
using shared_sock = std::shared_ptr<socket::isocket>;

#define LOG_SC_RECEIVE "RECEIVE "


void trace_message(const size_t bytes, const vector<char>& buffer, SOCKET conn_id)
{
	using std::cout;
	cout << "RECEIVED MESSAGE length " << bytes << " on conn ID " << conn_id;

#if 0
	if (bytes < 50)
	{
		cout << ":\n";
		cout << string(buffer.data(), bytes).c_str();
	}
	else if (buffer[0] >= '0' && buffer[0] <= 'z')
	{
		cout << " (first character):";
		cout << buffer[0];
	}
#endif
	cout << std::endl;

	// CHandShake hs;
	// if (hs.load_from(buffer.data(), buffer.size()) < 0)
	//	return;
	//
	//cout << "SRT HS: " << hs.show() << endl;
}

void run_pipe(shared_sock src, const config& cfg, unique_ptr<metrics::metrics_writer>& metrics, std::function<void(int conn_id)> const& on_done, const atomic_bool& force_break)
{
	XTR_THREADNAME(std::string("XTR:Rcv"));
	socket::isocket& sock = *src.get();
	const auto conn_id = sock.id();

	vector<char>       buffer(cfg.message_size);
	metrics::metrics_writer::shared_validator validator;

	if (metrics)
	{
		validator = std::make_shared<metrics::validator>(conn_id);
		metrics->add_validator(validator, conn_id);
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
			if (metrics)
			{
				validator->validate_packet(const_buffer(buffer.data(), bytes));
			}

			if (cfg.send_reply)
			{
				const string out_message("Message received");
				sock.write(const_buffer(out_message.data(), out_message.size()));

				if (cfg.print_notifications)
					spdlog::error(LOG_SC_RECEIVE "{} Reply sent on conn ID {}", conn_id, sock.id());
			}
		}
	}
	catch (const socket::exception& e)
	{
		spdlog::warn(LOG_SC_RECEIVE "{}", e.what());
	}

	if (metrics)
		metrics->remove_validator(conn_id);

	if (force_break)
	{
		spdlog::info(LOG_SC_RECEIVE "interrupted by request!");
	}

	on_done(conn_id);
}

void xtransmit::receive::run(const std::vector<std::string>& src_urls,
							 const config&                   cfg,
							 const atomic_bool&              force_break)
{
	using namespace std::placeholders;

	const bool write_metrics = cfg.enable_metrics && cfg.metrics_freq_ms > 0;
	unique_ptr<metrics::metrics_writer> metrics;

	if (write_metrics)
	{
		try {
			metrics = details::make_unique<metrics::metrics_writer>(
				cfg.metrics_file, milliseconds(cfg.metrics_freq_ms));
		}
		catch (const std::runtime_error& e)
		{
			spdlog::error(LOG_SC_RECEIVE "{}", e.what());
			return;
		}
	}

	processing_fn_t process_fn = std::bind(run_pipe, _1, cfg, std::ref(metrics), _2, _3);
	common_run(src_urls, cfg, cfg, force_break, process_fn);
}

CLI::App* xtransmit::receive::add_subcommand(CLI::App& app, config& cfg, std::vector<std::string>& src_urls)
{
	const std::map<string, int> to_ms{{"s", 1000}, {"ms", 1}};

	CLI::App* sc_receive = app.add_subcommand("receive", "Receive data (SRT, UDP)")->fallthrough();
	sc_receive->add_option("-i,--input,src", src_urls, "Source URI");
	sc_receive->add_option("--msgsize", cfg.message_size, fmt::format("Size of the buffer to receive message payload (default {})", cfg.message_size));
	sc_receive->add_option("--statsfile", cfg.stats_file, "Output stats report filename");
	sc_receive->add_option("--statsformat", cfg.stats_format, "Output stats report format (csv - default, json)");
	sc_receive->add_option("--statsfreq", cfg.stats_freq_ms, fmt::format("Output stats report frequency, ms (default {})", cfg.stats_freq_ms))
		->transform(CLI::AsNumberWithUnit(to_ms, CLI::AsNumberWithUnit::CASE_SENSITIVE));
	sc_receive->add_flag("--printmsg", cfg.print_notifications, "Print message to stdout");
	sc_receive->add_flag("--enable-metrics", cfg.enable_metrics, "Enable checking metrics: jitter, latency, etc.");
	sc_receive->add_option("--metricsfile", cfg.metrics_file, "Metrics output filename (default stdout)");
	sc_receive->add_option("--metricsfreq", cfg.metrics_freq_ms, fmt::format("Metrics report frequency, ms (default {})", cfg.metrics_freq_ms))
		->transform(CLI::AsNumberWithUnit(to_ms, CLI::AsNumberWithUnit::CASE_SENSITIVE));
	sc_receive->add_flag("--twoway", cfg.send_reply, "Both send and receive data");

	apply_cli_opts(*sc_receive, cfg);

	return sc_receive;
}
