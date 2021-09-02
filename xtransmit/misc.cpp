#include "misc.hpp"
#include "socket_stats.hpp"
#include "srt_socket_group.hpp"

// submodules
#include "spdlog/spdlog.h"

using namespace std;
using namespace std::chrono;

namespace xtransmit {

#define LOG_SC_CONN "CONN "


shared_sock_t create_connection(const vector<UriParser>& parsed_urls, shared_sock_t& listening_sock)
{
	if (parsed_urls.empty())
	{
		throw socket::exception("No URL was provided");
	}

	// if (parsed_urls[0].count("groupconnect"))
	// {

	// }
	// SRTO_GROUPCONNECT

	if (parsed_urls.size() > 1)
	{
#if ENABLE_EXPERIMENTAL_BONDING
		// Group SRT connection
		const bool is_listening = !!listening_sock;
		if (!is_listening)
			listening_sock = make_shared<socket::srt_group>(parsed_urls);
		socket::srt_group* s = dynamic_cast<socket::srt_group*>(listening_sock.get());
		const bool  accept = s->mode() == socket::srt_group::LISTENER;
		if (accept) {
			s->listen();
		}
		shared_sock_t connection = accept ? s->accept() : s->connect();

		// Only save the shared pointer for a listener to re-accept a connection.
		if (s->mode() != socket::srt_group::LISTENER)
			listening_sock.reset();

		return connection;
#else
		throw socket::exception("Use -DENABLE_EXPERIMENTAL_BONDING=ON to enable socket groups!");
#endif // ENABLE_EXPERIMENTAL_BONDING
	}

	const UriParser& uri = parsed_urls[0];

	if (uri.type() == UriParser::UDP)
	{
		return make_shared<socket::udp>(uri);
	}

	if (uri.type() == UriParser::SRT)
	{
		const bool is_listening = !!listening_sock;
		if (!is_listening)
			listening_sock = make_shared<socket::srt>(uri);
		socket::srt* s = dynamic_cast<socket::srt*>(listening_sock.get());
		const bool   accept = s->mode() == socket::srt::LISTENER;
		if (accept && !is_listening)
			s->listen();
		shared_sock_t connection;
		
		try {
			connection = accept ? s->accept() : s->connect();
		}
		catch (const socket::exception& e)
		{
			listening_sock.reset();
			throw e;
		}

		// Only save the shared pointer for a listener to re-accept a connection.
		if (s->mode() != socket::srt::LISTENER)
			listening_sock.reset();

		return connection;
	}

	return nullptr;
}


// Use std::bind to pass the run_pipe function, and bind arguments to it.
void common_run(const vector<string>& urls, const stats_config& cfg, bool reconnect, const atomic_bool& force_break,
	processing_fn_t& processing_fn)
{
	if (urls.empty())
	{
		spdlog::error(LOG_SC_CONN "URL was not provided");
		return;
	}

	const bool write_stats = cfg.stats_file != "" && cfg.stats_freq_ms > 0;
	unique_ptr<socket::stats_writer> stats;

	if (write_stats)
	{
		// make_unique is not supported by GCC 4.8, only starting from GCC 4.9 :(
		try {
			stats = unique_ptr<socket::stats_writer>(
				new socket::stats_writer(cfg.stats_file, milliseconds(cfg.stats_freq_ms)));
		}
		catch (const socket::exception& e)
		{
			spdlog::error(LOG_SC_CONN "{}", e.what());
			return;
		}
	}

	vector<UriParser> parsed_urls;
	for (const string& url : urls)
	{
		parsed_urls.emplace_back(url);
	}

	shared_sock_t listening_sock; // A shared pointer to store a listening socket for multiple connections.
	steady_clock::time_point next_reconnect = steady_clock::now();

	do {
		try
		{
			const auto tnow = steady_clock::now();
			if (tnow < next_reconnect)
				this_thread::sleep_until(next_reconnect);

			next_reconnect = tnow + seconds(1);
			// It is important to close `conn` after processing is done.
			// The scope of `conn` closes it unless stats_writer holds a pointer.
			shared_sock_t conn = create_connection(parsed_urls, listening_sock);

			// Closing a listener socket (if any) will not allow further connections.
			if (!reconnect)
				listening_sock.reset();

			if (stats)
				stats->add_socket(conn);

			processing_fn(conn, force_break);

			if (stats)
				stats->remove_socket(conn->id());
		}
		catch (const socket::exception& e)
		{
			spdlog::warn(LOG_SC_CONN "{}", e.what());
		}
	} while (reconnect && !force_break);
}

} // namespace xtransmit
