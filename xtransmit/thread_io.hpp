#pragma once
#include <future>
#include <thread>
#include "misc.hpp"
#include "socket_stats.hpp"
#include "srt_socket_group.hpp"

// submodules
#include "spdlog/spdlog.h"

namespace xtransmit
{

#define LOG_IO "IO "

using std::atomic_bool;
using std::map;
using std::future;
using std::chrono::steady_clock;
using on_connection_fn_t = std::function<void(shared_sock_t, const std::atomic_bool&)>;
using on_read_fn_t = std::function<void(shared_sock_t&)>;
//using on_write_fn_t = std::function<void(shared_sock_t, const std::atomic_bool&)>;

//processing_fn(conn, force_break);

struct io_ctx
{
	io_ctx(shared_sock_t&& s, on_read_fn_t f)
		: sock(s)
		, read_fn(f)
	{}

	shared_sock_t sock;
	on_read_fn_t  read_fn;
};

class io_dispatch
{
public:
	io_dispatch()
	{
		m_epoll_io = srt_epoll_create();
		if (m_epoll_io == SRT_ERROR)
			throw socket::exception(srt_getlasterror_str());
	}

	~io_dispatch()
	{
		stop();
		if (m_epoll_io != SRT_ERROR)
			srt_epoll_release(m_epoll_io);
	}

	void add(shared_sock_t sock, const std::atomic_bool& force_break, int epoll_flags, on_read_fn_t read_fn);

	void stop();

private:
	std::future<void> launch();

private:
	map<SOCKET, io_ctx> m_sockets;
	int                 m_epoll_io = SRT_ERROR;
	std::future<void>   m_worker;
	std::atomic<bool>   m_stop_requested;
};

} // namespace xtransmit
