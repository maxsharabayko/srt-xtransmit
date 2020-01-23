#include <atomic>
#include <chrono>
#include <ctime>
#include <future>
#include <iomanip>
#include <memory>
#include <thread>
#include <vector>

#include "srt_socket.hpp"
#include "udp_socket.hpp"
#include "route.hpp"
#include "socket_stats.hpp"

// OpenSRT
#include "apputil.hpp"
#include "uriparser.hpp"

using namespace std;
using namespace xtransmit;
using namespace xtransmit::route;
using namespace std::chrono;

using shared_srt = std::shared_ptr<socket::srt>;
using shared_sock = std::shared_ptr<socket::isocket>;


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

		while (!force_break)
		{
			const size_t bytes_read = sock_src.read(mutable_buffer(buffer.data(), buffer.size()), -1);

			if (bytes_read == 0)
			{
				::cerr << desc << " read returned 0\n";
				continue;
			}

			const int bytes_sent = sock_dst.write(const_buffer(buffer.data(), bytes_read));

			if (bytes_sent != bytes_read)
			{
				::cerr << desc << " write returned " << bytes_sent << " while sending " << bytes_read << " bytes" << endl;
				continue;
			}
		}
	}

	shared_sock create_connection(const string &url)
	{
		const UriParser uri(url);

		if (uri.proto() == "udp")
		{
			return make_shared<socket::udp>(uri);
		}
		
		if(uri.proto() == "srt")
		{
			shared_sock socket = make_shared<socket::srt>(uri);
			socket::srt* s = static_cast<socket::srt*>(socket.get());
			const bool  accept = s->mode() == socket::srt::LISTENER;
			if (accept)
				s->listen();
			return accept ? s->accept() : s->connect();
		}

		return nullptr;
	}

}
}


void xtransmit::route::run(const string& src_url, const string& dst_url,
	const config& cfg, const atomic_bool& force_break)
{
	unique_ptr<socket::stats_writer> stats;
	try {
		shared_sock dst = create_connection(dst_url);
		shared_sock src = create_connection(src_url);

		if (cfg.stats_file != "" && cfg.stats_freq_ms > 0)
		{
			socket::stats_writer stats(cfg.stats_file, milliseconds(cfg.stats_freq_ms));
			stats.add_socket(src);
			stats.add_socket(dst);
		}

		route(src, dst, cfg, "[SRC->DST]", force_break);
	}
	catch (const socket::exception & e)
	{
		cerr << e.what() << endl;
	}
}

CLI::App* xtransmit::route::add_subcommand(CLI::App& app, config& cfg, string& src_url, string& dst_url)
{
	const map<string, int> to_ms{ {"s", 1000}, {"ms", 1} };

	CLI::App* sc_route = app.add_subcommand("route", "Route data (SRT, UDP)")->fallthrough();
	sc_route->add_option("src", src_url, "Source URI");
	sc_route->add_option("dst", dst_url, "Destination URI");
	sc_route->add_option("--msgsize", cfg.message_size, "Size of a buffer to receive message payload");
	sc_route->add_option("--statsfile", cfg.stats_file, "output stats report filename");
	sc_route->add_option("--statsfreq", cfg.stats_freq_ms, "output stats report frequency (ms)")
		->transform(CLI::AsNumberWithUnit(to_ms, CLI::AsNumberWithUnit::CASE_SENSITIVE));

	return sc_route;
}



