#include "udp_socket.hpp"
#include "apputil.hpp"
#include "verbose.hpp"

#ifdef _WIN32
#define _WINSOCKAPI_ // to include Winsock2.h instead of Winsock.h from windows.h
#include <winsock2.h>

#if defined(__GNUC__) || defined(__MINGW32__)
extern "C"
{
	WINSOCK_API_LINKAGE INT WSAAPI inet_pton(INT Family, PCSTR pszAddrString, PVOID pAddrBuf);
	WINSOCK_API_LINKAGE PCSTR WSAAPI inet_ntop(INT Family, PVOID pAddr, PSTR pStringBuf, size_t StringBufSize);
}
#endif

#define INC__WIN_WINTIME // exclude gettimeofday from srt headers

#else
typedef int SOCKET;
#define INVALID_SOCKET ((SOCKET)-1)
#define closesocket close
#endif

using namespace std;
using namespace xtransmit;
using shared_socket = shared_ptr<udp::socket>;

udp::socket::socket(const UriParser &src_uri)
    : m_host(src_uri.host())
    , m_port(src_uri.portno())
    , m_options(src_uri.parameters())
{
	sockaddr_in sa     = sockaddr_in();
	sa.sin_family      = AF_INET;
	sa.sin_addr.s_addr = INADDR_ANY;
	m_bind_socket      = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	//if (m_bind_socket == -1)
	//	throw socket_exception("");
}

udp::socket::~socket() { closesocket(m_bind_socket); }


shared_socket udp::socket::connect()
{
	try
	{
		m_dst_addr = CreateAddrInet(m_host, m_port);
	}
	catch (const std::invalid_argument &e)
	{
		throw socket_exception("Error at create_addr_inet");
	}

	return shared_from_this();
}


size_t udp::socket::read(const mutable_buffer &buffer, int timeout_ms)
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

	recvfrom(m_bind_socket, static_cast<char *>(buffer.data()), (int)buffer.size())
	const int res = srt_recvmsg2(m_bind_socket, static_cast<char *>(buffer.data()), (int)buffer.size(), nullptr);
	if (SRT_ERROR == res)
		raise_exception("socket::read::recv", UDT::getlasterror());

	return static_cast<size_t>(res);
}
