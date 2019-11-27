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

	//if (m_bind_socket == -1)
	//	throw socket_exception("");
}

socket::udp::~udp()
{
	closesocket(m_bind_socket);
}


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

	const int res = ::recvfrom(m_bind_socket, static_cast<char*>(buffer.data()), (int)buffer.size(), 0, nullptr, nullptr);
	if (res == -1)
		throw socket::exception("udp::read::recv");

	return static_cast<size_t>(res);
}
