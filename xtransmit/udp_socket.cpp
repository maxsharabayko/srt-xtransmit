#include "udp_socket.hpp"
#include "apputil.hpp"
#include "socketoptions.hpp"

#ifndef _WIN32
#include <sys/epoll.h>
#endif

// submodules
#include "spdlog/spdlog.h"

using namespace std;
using namespace xtransmit;
using shared_udp = shared_ptr<socket::udp>;

socket::udp::udp(const UriParser &src_uri)
	: m_host(src_uri.host())
	, m_port(src_uri.portno())
	, m_options(src_uri.parameters())
{
	sockaddr_in sa     = sockaddr_in();
	sa.sin_family      = AF_INET;
	sa.sin_addr.s_addr = INADDR_ANY;
	m_bind_socket      = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if (m_bind_socket == INVALID_SOCKET)
		throw socket::exception("Failed to create a UDP socket");

	if (m_options.count("blocking"))
	{
		m_blocking_mode = !false_names.count(m_options.at("blocking"));
		m_options.erase("blocking");
	}

	int yes = 1;
	::setsockopt(m_bind_socket, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof yes);

	if (!m_blocking_mode)
	{ // set non-blocking mode
		unsigned long nonblocking = 1;
#if defined(_WIN32)
		if (ioctlsocket(m_bind_socket, FIONBIO, &nonblocking) == SOCKET_ERROR)
#else
		if (ioctl(m_bind_socket, FIONBIO, (const char *)&nonblocking) < 0)
#endif
		{
			throw socket::exception("Failed to set blocking mode for UDP");
		}

		int rcvbuf_size = 65536;
		socklen_t rcvbuf_len  = sizeof(int);
		::getsockopt(m_bind_socket, SOL_SOCKET, SO_RCVBUF, (char*)&rcvbuf_size, &rcvbuf_len);
		spdlog::info("UDP SO_RCVBUF: {0}.", rcvbuf_size);
#ifndef _WIN32

		rcvbuf_size = 4 * 212992;
		::setsockopt(m_bind_socket, SOL_SOCKET, SO_RCVBUF, (char*)&rcvbuf_size, sizeof(int));

		::getsockopt(m_bind_socket, SOL_SOCKET, SO_RCVBUF, (char*)&rcvbuf_size, &rcvbuf_len);
		spdlog::info("UDP new SO_RCVBUF: {0}.", rcvbuf_size);

		epoll_event ev;
		m_epoll_io = epoll_create(2);
		ev.data.fd = m_bind_socket;
		ev.events = EPOLLIN | EPOLLET;
		epoll_ctl(m_epoll_io, EPOLL_CTL_ADD, m_bind_socket, &ev);
#endif
	}

	// Use the following convention:
	// 1. Server for source, Client for target
	// 2. If host is empty, then always server.
	m_is_caller = (m_host != "");

	sockaddr_in sa_requested;
	try
	{
		sa_requested = CreateAddrInet(m_host, m_port);
	}
	catch (const std::invalid_argument &)
	{
		throw socket::exception("create_addr_inet failed");
	}

	if (m_is_caller)
	{
		m_dst_addr = sa_requested;
	}
	else
	{
		const sockaddr *psa      = reinterpret_cast<const sockaddr *>(&sa_requested);
		const int       bind_res = ::bind(m_bind_socket, psa, sizeof sa_requested);
		if (bind_res < 0)
		{
			throw socket::exception("UDP binding has failed");
		}
	}
}

socket::udp::~udp() { closesocket(m_bind_socket); }

size_t socket::udp::read(const mutable_buffer &buffer, int timeout_ms)
{
#if defined(_WIN32)
	while (!m_blocking_mode)
	{
		fd_set set;
		timeval tv;
		FD_ZERO(&set);
		FD_SET(m_bind_socket, &set);
		tv.tv_sec = 0;
		tv.tv_usec = 10000;
		const int select_ret = ::select((int)m_bind_socket + 1, &set, NULL, &set, &tv);

		if (select_ret != 0)    // ready
			break;

		if (timeout_ms >= 0)   // timeout
			return 0;
	}
#else
	struct epoll_event events[2];
	const int nfds = epoll_wait(m_epoll_io, events, 2, timeout_ms);
#endif

	const int res =
		::recv(m_bind_socket, static_cast<char *>(buffer.data()), (int)buffer.size(), 0);
	if (res == -1)
	{
#ifndef _WIN32
#define NET_ERROR errno
#else
#define NET_ERROR WSAGetLastError()
#endif
		const int err = NET_ERROR;
		if (err != EAGAIN && err != EINTR && err != ECONNREFUSED)
			throw socket::exception("udp::read::recv");

		spdlog::info("UDP reading failed: error {0}. Again.", err);
		return 0;
	}

	return static_cast<size_t>(res);
}

int socket::udp::write(const const_buffer &buffer, int timeout_ms)
{
	const int res = ::sendto(m_bind_socket,
							 static_cast<const char *>(buffer.data()),
							 (int)buffer.size(),
							 0,
							 (sockaddr *)&m_dst_addr,
							 sizeof m_dst_addr);
	if (res == -1)
	{
		throw socket::exception("udp::write::send");
	}

	return static_cast<size_t>(res);
}
