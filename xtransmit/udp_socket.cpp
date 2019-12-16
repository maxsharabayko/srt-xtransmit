#include "udp_socket.hpp"
#include "apputil.hpp"
#include "socketoptions.hpp"
#include "verbose.hpp"

using namespace std;
using namespace xtransmit;
using shared_udp = shared_ptr<socket::udp>;

socket::udp::udp(const UriParser &src_uri)
	: m_host(src_uri.host())
	, m_port(src_uri.portno())
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

	if (m_blocking_mode)
	{ // set non-blocking mode
#if defined(_WIN32)
		unsigned long ulyes = 1;
		if (ioctlsocket(m_bind_socket, FIONBIO, &ulyes) == SOCKET_ERROR)
#else
		if (ioctl(m_bind_socket, FIONBIO, (const char *)&yes) < 0)
#endif
		{
			throw socket::exception("UdpCommon::Setup: ioctl FIONBIO");
		}
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
	if (!m_blocking_mode)
	{
		//	int ready[2] = {SRT_INVALID_SOCK, SRT_INVALID_SOCK};
		//	int len      = 2;
		//
		//	const int epoll_res = srt_epoll_wait(m_epoll_io, ready, &len, nullptr, nullptr, timeout_ms, 0, 0, 0, 0);
		//	if (epoll_res == SRT_ERROR)
		//	{
		//		if (srt_getlasterror(nullptr) == SRT_ETIMEOUT)
		//			return 0;
		//
		//		raise_exception("socket::read::epoll", UDT::getlasterror());
		//	}
	}

	const int res =
		::recvfrom(m_bind_socket, static_cast<char *>(buffer.data()), (int)buffer.size(), 0, nullptr, nullptr);
	if (res == -1)
		throw socket::exception("udp::read::recv");

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
