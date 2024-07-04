#include <list>
#include <thread>
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

	if (parsed_urls.size() > 1 || parsed_urls[0].parameters().count("grouptype"))
	{
#if ENABLE_BONDING
		// Group SRT connection
		const bool is_listening = !!listening_sock;
		if (!is_listening)
			listening_sock = make_shared<socket::srt_group>(parsed_urls);
		socket::srt_group* s = dynamic_cast<socket::srt_group*>(listening_sock.get());
		const bool  accept = s->mode() == socket::srt_group::LISTENER;
		if (accept && !is_listening) {
			s->listen();
		}
		shared_sock_t connection = accept ? s->accept() : s->connect();

		// Only save the shared pointer for a listener to re-accept a connection.
		if (s->mode() != socket::srt_group::LISTENER)
			listening_sock.reset();

		return connection;
#else
		throw socket::exception("Use -DENABLE_BONDING=ON to enable socket groups!");
#endif // ENABLE_BONDING
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

	throw socket::exception(fmt::format("Unknown protocol '{}'.", uri.proto()));
}

class concurrent_pipes
{
public:
	concurrent_pipes(socket::stats_writer* stats)
		:m_stats(stats)
	{}

	void add_pipe(shared_sock_t conn, processing_fn_t const& run_pipe, const atomic_bool& break_token)
	{
		m_pipes.emplace(conn->id(), ::async(::launch::async, run_pipe, conn, [this](int conn_id) { on_pipe_exit(conn_id); }, ref(break_token)));
	}

	void on_pipe_exit(int conn_id)
	{
		using namespace std;
		if (m_stats)
			m_stats->remove_socket(conn_id);

		lock_guard<mutex> lck(m_mtx);
		const auto& pipe = m_pipes.find(conn_id);
		if (pipe == m_pipes.end())
		{
			spdlog::error("PIPE Failed to find exiting pipe (conn @{}), {} remain active.", conn_id, m_pipes.size());
			return;
		}

		// Can't erase a pipe here, because execition of a pipe function is not done.
		m_to_close.insert(conn_id);
		spdlog::info("PIPE Pipe exit (conn @{}), {} remain active.", conn_id, m_pipes.size() - m_to_close.size());
		m_cv_active.notify_all();
	}

	size_t size() const
	{
		using namespace std;
		lock_guard<mutex> lck(m_mtx);
		return m_pipes.size() - m_to_close.size();
	}

	// Wait for changes in the number of active pipes.
	void wait()
	{
		using namespace std;
		unique_lock<mutex> lck(m_mtx);
		m_cv_active.wait(lck);

		spdlog::debug("PIPE Clearing {} inactive pipes.", m_to_close.size());
		for (const auto id : m_to_close)
			m_pipes.erase(id);
		m_to_close.clear();
	}

private:
	socket::stats_writer*        m_stats;
	map<SOCKET, future<void>>    m_pipes;
	set<SOCKET>                  m_to_close;
	mutable std::mutex           m_mtx;
	std::condition_variable      m_cv_active;
};


// Use std::bind to pass the run_pipe function, and bind arguments to it.
void common_run(const vector<string>& urls, const stats_config& cfg_stats, const conn_config& cfg_conn,
	const atomic_bool& break_token, processing_fn_t& processing_fn)
{
	if (urls.empty())
	{
		spdlog::error(LOG_SC_CONN "URL was not provided");
		return;
	}

	const bool write_stats = cfg_stats.stats_file != "" && cfg_stats.stats_freq_ms > 0;
	unique_ptr<socket::stats_writer> stats;

	if (write_stats)
	{
		// make_unique is not supported by GCC 4.8, only starting from GCC 4.9 :(
		try {
			stats = unique_ptr<socket::stats_writer>(
				new socket::stats_writer(cfg_stats.stats_file, cfg_stats.stats_format, milliseconds(cfg_stats.stats_freq_ms)));
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

	// future<void> route_bkwd = cfg.bidir
	// 		? ::async(::launch::async, route, dst, src, cfg, "[DST->SRC]", ref(force_break))
	// 		: future<void>();	
	// TODO: Add connection lost event.
	//map<SRTSOCKET, future<void>> processing_pipes;
	concurrent_pipes pipes(stats.get());
	unsigned conns_cnt = 0;

	do {
		const auto tstart = steady_clock::now();
		try
		{
			if (tstart < next_reconnect)
				this_thread::sleep_until(next_reconnect);

			spdlog::info(LOG_SC_CONN "Establishing connection no.{}.", conns_cnt +1);

			// It is important to close `conn` after processing is done.
			// The scope of `conn` closes it unless stats_writer holds a pointer.
			shared_sock_t conn = create_connection(parsed_urls, listening_sock);

			if (!conn)
			{
				spdlog::error(LOG_SC_CONN "Failed to create a connection to '{}'.", urls[0]);
				break;
			}

			// Closing a listener socket (if any) will not allow further connections.
			if (cfg_conn.close_listener)
				listening_sock.reset();

			if (stats)
				stats->add_socket(conn);

			pipes.add_pipe(conn, processing_fn, break_token);
			++conns_cnt;

		}
		catch (const socket::exception& e)
		{
			spdlog::warn(LOG_SC_CONN "{}", e.what());
		}

		if (conns_cnt >= cfg_conn.max_conns)
			break;

		if (pipes.size() < cfg_conn.concurrent_streams)
			continue;

		if (!cfg_conn.reconnect || break_token)
			break;

		while (!break_token && pipes.size() >= cfg_conn.concurrent_streams)
			pipes.wait();

		// Slow down reconnection pace to not faster than once per second.
		next_reconnect = tstart + seconds(1);

	} while (!break_token);

	while (pipes.size() > 0)
	{
		pipes.wait();
	}
}

netaddr_any create_addr(const string& name, unsigned short port, int pref_family)
{
	// Handle empty name.
	// If family is specified, empty string resolves to ANY of that family.
	// If not, it resolves to IPv4 ANY (to specify IPv6 any, use [::]).
	if (name == "")
	{
		netaddr_any result(pref_family == AF_INET6 ? pref_family : AF_INET);
		result.hport(port);
		return result;
	}

	bool first6 = pref_family != AF_INET;
	int families[2] = {AF_INET6, AF_INET};
	if (!first6)
	{
		families[0] = AF_INET;
		families[1] = AF_INET6;
	}

	for (int i = 0; i < 2; ++i)
	{
		int family = families[i];
		netaddr_any result (family);
		// Try to resolve the name by pton first
		if (inet_pton(family, name.c_str(), result.get_addr()) == 1)
		{
			result.hport(port); // same addr location in ipv4 and ipv6
			return result;
		}
	}

	// If not, try to resolve by getaddrinfo
	// This time, use the exact value of pref_family

	netaddr_any result;
	addrinfo fo = {
		0,
		pref_family,
		0, 0,
		0, 0,
		NULL, NULL
	};

	addrinfo* val = nullptr;
	int erc = getaddrinfo(name.c_str(), nullptr, &fo, &val);
	if (erc == 0)
	{
		result.set(val->ai_addr);
		result.len = result.size();
		result.hport(port); // same addr location in ipv4 and ipv6
	}
	freeaddrinfo(val);

	return result;
}

} // namespace xtransmit
