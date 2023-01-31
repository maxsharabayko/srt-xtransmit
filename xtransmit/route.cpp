#include <atomic>
#include <chrono>
#include <ctime>
#include <future>
#include <iomanip>
#include <memory>
#include <random>
#include <thread>
#include <vector>

// submodules
#include "spdlog/spdlog.h"

// xtransmit
#include "srt_socket.hpp"
#include "udp_socket.hpp"
#include "misc.hpp"
#include "route.hpp"
#include "socket_stats.hpp"

// OpenSRT
#include "apputil.hpp"
#include "uriparser.hpp"

// SRTX
#include "pkt_base.hpp"

using namespace std;
using namespace xtransmit;
using namespace xtransmit::route;
using namespace std::chrono;

using shared_srt = std::shared_ptr<socket::srt>;
using shared_sock = std::shared_ptr<socket::isocket>;

#define LOG_SC_ROUTE "ROUTE "

namespace xtransmit
{
namespace route
{

	void route(shared_sock src, shared_sock dst,
		const config& cfg, const string&& desc, const atomic_bool& force_break)
	{
		vector<char> buffer(cfg.message_size);

		socket::isocket& sock_src = *src.get();
		socket::isocket& sock_dst = *dst.get();

		spdlog::info(LOG_SC_ROUTE "{0} Started", desc);

		auto prev_corrupt_ts = steady_clock::now();
		int pkts_untill_corrupt = cfg.corrupt_pkt_freq;

		while (!force_break)
		{
			const size_t bytes_read = sock_src.read(mutable_buffer(buffer.data(), buffer.size()), -1);

			if (bytes_read == 0)
			{
				spdlog::info(LOG_SC_ROUTE "{} read 0 bytes on a socket (spurious read-ready?). Retrying.", desc);
				continue;
			}

			const auto tnow = steady_clock::now();
			if (bytes_read > 1 && (cfg.corrupt_freq_ms > 0 && (tnow - prev_corrupt_ts > milliseconds(cfg.corrupt_freq_ms))
				|| --pkts_untill_corrupt == 0))
			{
				prev_corrupt_ts = tnow;
				static std::random_device s_RandomDevice;
				static std::mt19937 s_GenMT19937(s_RandomDevice());
				uniform_int_distribution<size_t> dis(0, bytes_read);
				const ptrdiff_t byteoff = (ptrdiff_t) dis(s_GenMT19937);

				srtx::pkt_base<vector<char>> pkt(buffer);
				const auto pkt_type_str = pkt.is_ctrl() ? srtx::ctrl_type_str(pkt.control_type()) : "DATA";

				spdlog::info(LOG_SC_ROUTE "{} Corrupting a {} at byte offset {}", desc, pkt_type_str,
					byteoff);
				++buffer[byteoff];
				pkts_untill_corrupt = cfg.corrupt_pkt_freq;
			}

			// SRT can return 0 on SRT_EASYNCSND. Rare for sending. However might be worth to retry.
			const int bytes_sent = sock_dst.write(const_buffer(buffer.data(), bytes_read));

			if (bytes_sent != bytes_read)
			{
				spdlog::info("{} write returned {} bytes, expected {}", desc, bytes_sent, bytes_read);
				continue;
			}
		}
	}
}
}


void xtransmit::route::run(const vector<string>& src_urls, const vector<string>& dst_urls,
	const config& cfg, const atomic_bool& force_break)
{
	vector<UriParser> parsed_src_urls;
	for (const string& url : src_urls)
	{
		parsed_src_urls.emplace_back(url);
	}

	vector<UriParser> parsed_dst_urls;
	for (const string& url : dst_urls)
	{
		parsed_dst_urls.emplace_back(url);
	}

	try {
		const bool write_stats = cfg.stats_file != "" && cfg.stats_freq_ms > 0;
		// make_unique is not supported by GCC 4.8, only starting from GCC 4.9 :(
		unique_ptr<socket::stats_writer> stats = write_stats
			? unique_ptr<socket::stats_writer>(new socket::stats_writer(cfg.stats_file, cfg.stats_format, milliseconds(cfg.stats_freq_ms)))
			: nullptr;

		shared_sock dst = create_connection(parsed_dst_urls);
		shared_sock src = create_connection(parsed_src_urls);

		if (stats)
		{
			stats->add_socket(src);
			stats->add_socket(dst);
		}

		future<void> route_bkwd = cfg.bidir
			? ::async(::launch::async, route, dst, src, cfg, "[DST->SRC]", ref(force_break))
			: future<void>();	

		route(src, dst, cfg, "[SRC->DST]", force_break);

		route_bkwd.wait();
	}
	catch (const socket::exception & e)
	{
		spdlog::error(LOG_SC_ROUTE "{}", e.what());
	}
}

CLI::App* xtransmit::route::add_subcommand(CLI::App& app, config& cfg, vector<string>& src_urls, vector<string>& dst_urls)
{
	const map<string, int> to_ms{ {"s", 1000}, {"ms", 1} };

	CLI::App* sc_route = app.add_subcommand("route", "Route data (SRT, UDP)")->fallthrough();
	sc_route->add_option("-i,--input",  src_urls, "Source URIs");
	sc_route->add_option("-o,--output", dst_urls, "Destination URIs");
	sc_route->add_option("--msgsize", cfg.message_size, "Size of a buffer to receive message payload");
	sc_route->add_flag("--bidir", cfg.bidir, "Enable bidirectional transmission");
	sc_route->add_option("--corruptfreq", cfg.corrupt_freq_ms, "artificial packet corruption frequency (ms)")
		->transform(CLI::AsNumberWithUnit(to_ms, CLI::AsNumberWithUnit::CASE_SENSITIVE));
	sc_route->add_option("--corruptpkt", cfg.corrupt_pkt_freq, "artificial packet corruption frequency (every n-th packet)");
	sc_route->add_option("--statsfile", cfg.stats_file, "output stats report filename");
	sc_route->add_option("--statsformat", cfg.stats_format, "output stats report format (json, csv)");
	sc_route->add_option("--statsfreq", cfg.stats_freq_ms, "output stats report frequency (ms)")
		->transform(CLI::AsNumberWithUnit(to_ms, CLI::AsNumberWithUnit::CASE_SENSITIVE));

	return sc_route;
}



