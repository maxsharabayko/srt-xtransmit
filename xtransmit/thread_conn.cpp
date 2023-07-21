/// Connection handling.
#include <future>
#include <thread>
#include "misc.hpp"
#include "socket_stats.hpp"
#include "srt_socket_group.hpp"

// submodules
#include "spdlog/spdlog.h"


namespace xtransmit
{

using std::atomic_bool;
using std::map;
using std::future;
using std::chrono::steady_clock;
using on_connection_fn_t = std::function<void(shared_sock_t, const std::atomic_bool&)>;


class sock_connector
{
public:
	sock_connector()
	{
		m_epoll_conn = srt_epoll_create();
		if (m_epoll_conn == -1)
			throw socket::exception(srt_getlasterror_str());
	}

	~sock_connector()
	{
		if (m_epoll_conn != -1)
			srt_epoll_release(m_epoll_conn);
	}

private:
	std::future<void> launch();

private:
	// should be map<SOCKET, pair<shared_sock_t, on_connection_fn_t>>
	map<SOCKET, shared_sock_t> m_sockets;
	int                        m_epoll_conn = -1;
};


future<void> sock_connector::launch()
{
	// epoll polling loop

	// Socket readiness to accept a new connection is notified with READ event.
	// Socket readiness for connection is checked by polling on WRITE allowed sockets.
	// See also: https://github.com/Haivision/srt/pull/1831
	// System sockets can be polled using lrfds and lwfds arguments of the srt_epoll_wait.
	constexpr int POLL_TIMEOUT_MS = 100;
	constexpr int MAX_POLL_EVENTS = 1;
	int           rd_len = MAX_POLL_EVENTS;
	int           wd_len = MAX_POLL_EVENTS;
	SRTSOCKET     rd_ready[MAX_POLL_EVENTS] = {SRT_INVALID_SOCK};
	SRTSOCKET     wr_ready[MAX_POLL_EVENTS] = {SRT_INVALID_SOCK};
	if (srt_epoll_wait(m_epoll_conn, rd_ready, &rd_len, wd_ready, &wd_len, POLL_TIMEOUT_MS, 0, 0, 0, 0) >= 0)
	{
		if (rd_len == 1 && wd_len == 1)
		{
			// Handle error event.
		}

		if (rd_len == 1 && rd_ready[0] != SRT_INVALID_SOCK)
		{
			// Accept incoming connection
		}

		if (wd_len == 1 && wd_ready[0] != SRT_INVALID_SOCK)
		{
			// Handle caller connection event.
		}
	}
}

// Use std::bind to pass the run_pipe function, and bind arguments to it.
void connection_loop(bool reconnect, const atomic_bool& force_break,
	on_connection_fn_t& processing_fn)
{
	shared_sock_t listening_sock; // A shared pointer to store a listening socket for multiple connections.
	steady_clock::time_point next_reconnect = steady_clock::now();

	do {
		try
		{
			const auto tnow = steady_clock::now();
			if (tnow < next_reconnect)
				std::this_thread::sleep_until(next_reconnect);

			next_reconnect = tnow + seconds(1);
			// It is important to close `conn` after processing is done.
			// The scope of `conn` closes it unless stats_writer holds a pointer.
			shared_sock_t conn = create_connection(parsed_urls, listening_sock);

			if (!conn)
			{
				spdlog::error(LOG_SC_CONN "Failed to create a connection to '{}'.", urls[0]);
				return;
			}

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
