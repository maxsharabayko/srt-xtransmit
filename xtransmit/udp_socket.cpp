#include "udp_socket.hpp"
#include "misc.hpp"
#include "socketoptions.hpp"

// submodules
#include "spdlog/spdlog.h"

using namespace std;
using namespace xtransmit;
using shared_udp = shared_ptr<socket::udp>;

#define LOG_SOCK_UDP "SOCKET::UDP "

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
	}

	netaddr_any sa_requested;
	try
	{
		sa_requested = create_addr(m_host, m_port);
	}
	catch (const std::invalid_argument &)
	{
		throw socket::exception("create_addr_inet failed");
	}

	const auto bind_me = [&](const netaddr_any& sa) {
		const int       bind_res = ::bind(m_bind_socket, sa.get(), sa.size());
		if (bind_res < 0)
		{
			throw socket::exception("UDP binding has failed");
		}
	};

	bool ip_bonded = false;
	if (m_options.count("bind"))
	{
		string bindipport = m_options.at("bind");
		transform(bindipport.begin(), bindipport.end(), bindipport.begin(), [](char c) { return tolower(c); });
		const size_t idx    = bindipport.find(":");
		const string bindip = bindipport.substr(0, idx);
		const int bindport = idx != string::npos
			? stoi(bindipport.substr(idx + 1, bindipport.size() - (idx + 1)))
			: m_port;
		m_options.erase("bind");

		netaddr_any sa_bind;
		try
		{
			sa_bind = create_addr(bindip, bindport);
		}
		catch (const std::invalid_argument&)
		{
			throw socket::exception("create_addr_inet failed");
		}

		bind_me(sa_bind);
		ip_bonded = true;
		spdlog::info(LOG_SOCK_UDP "udp://{}:{:d}: bound to '{}:{}'.",
			m_host, m_port, bindip, bindport);
	}

	if (m_host != "" || ip_bonded)
	{
		m_dst_addr = sa_requested;
	}
	else
	{
		bind_me(sa_requested);
	}
}

socket::udp::~udp() { closesocket(m_bind_socket); }

const netaddr_any socket::udp::src_addr() const
{
	sockaddr_storage ss;
	int sz = (int) sizeof ss;
	if (getsockname(m_bind_socket, (sockaddr*) &ss, &sz) != 0)
	{
		throw socket::exception("failed to get local address of socket");
	};

	return netaddr_any((sockaddr*)&ss, sz);
}

size_t socket::udp::read(const mutable_buffer &buffer, int timeout_ms)
{
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

std::pair<size_t, netaddr_any>
socket::udp::recvfrom(const mutable_buffer& buffer, int timeout_ms)
{
	typedef std::pair<size_t, netaddr_any> return_pair;

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
			return return_pair();
	}


	netaddr_any peer_addr;
	socklen_t addrlen = (socklen_t)peer_addr.storage_size();

	const int res =
		::recvfrom(m_bind_socket, reinterpret_cast<char*>(buffer.data()), (int)buffer.size()
			, 0, peer_addr.get(), &addrlen);

	if (res == -1)
	{
		const int err = NET_ERROR;
		if (err != EAGAIN && err != EINTR && err != ECONNREFUSED)
		{
			// TODO: Catch this error
			// TODO: On Windows if UDP socket recv a ICMP(port unreachable) message after send a message,
			// error 10054 will be stored, and next time call recvfrom() will return this error.
			spdlog::error("UDP {} reading failed: error {}.", id(), err);
			//throw runtime_error("udp::recv::recv");
			return return_pair();
		}

		spdlog::info("UDP reading failed: error {0}. Again.", err);
		return return_pair();
	}

	return return_pair(static_cast<size_t>(res), peer_addr);
}

int socket::udp::sendto(const netaddr_any& dst_addr, const const_buffer& buffer, int timeout_ms)
{
	while (!m_blocking_mode)
	{
		fd_set set;
		timeval tv;
		FD_ZERO(&set);
		FD_SET(m_bind_socket, &set);
		tv.tv_sec = 0;
		tv.tv_usec = 10000;
		const int select_ret = ::select((int)m_bind_socket + 1, nullptr, &set, &set, &tv);

		if (select_ret != 0)    // ready
			break;

		if (timeout_ms >= 0)   // timeout
			return 0;
	}

	const int res = ::sendto(m_bind_socket,
		reinterpret_cast<const char*>(buffer.data()),
		(int)buffer.size(),
		0,
		dst_addr.get(),
		dst_addr.size());
	if (res == -1)
	{
		const int err = NET_ERROR;
		spdlog::error("UDP sendto {0} failed: error {1}.", dst_addr.str(), err);
		throw runtime_error("udp::send::send");
	}

	return res;
}

int socket::udp::write(const const_buffer &buffer, int timeout_ms)
{
	while (!m_blocking_mode)
	{
		fd_set set;
		timeval tv;
		FD_ZERO(&set);
		FD_SET(m_bind_socket, &set);
		tv.tv_sec = 0;
		tv.tv_usec = 10000;
		const int select_ret = ::select((int)m_bind_socket + 1, nullptr, &set, &set, &tv);

		if (select_ret != 0)    // ready
			break;

		if (timeout_ms >= 0)   // timeout
			return 0;
	}

	const int res = ::sendto(m_bind_socket,
							 static_cast<const char *>(buffer.data()),
							 (int)buffer.size(),
							 0,
							 (sockaddr *)&m_dst_addr,
							 sizeof m_dst_addr);
	if (res == -1)
	{
#ifndef _WIN32
#define NET_ERROR errno
#else
#define NET_ERROR WSAGetLastError()
#endif
		const int err = NET_ERROR;
		if (err != EAGAIN && err != EINTR && err != ECONNREFUSED)
		{
			spdlog::info("udp::write::sendto: error {0}.", err);
			throw socket::exception("udp::write::sendto error");
		}

		spdlog::info("udp::sendto failed: error {0}. Again.", err);
		return 0;
	}

	return static_cast<size_t>(res);
}
