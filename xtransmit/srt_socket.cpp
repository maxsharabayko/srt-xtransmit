#include <thread>
#include "srt_socket.hpp"



#include "srt.h"


xtransmit::srt::socket::socket()
	: m_port(4200)
{
	m_bindsocket = srt_create_socket();
	if (m_bindsocket == SRT_INVALID_SOCK)
		throw socket_exception(srt_getlasterror_str());
}


void xtransmit::srt::socket::listen()
{


}


std::future<std::shared_ptr<xtransmit::srt::socket>> xtransmit::srt::socket::async_connect()
{
	auto self = shared_from_this();
	return async(std::launch::async, [self]() {std::this_thread::sleep_for(std::chrono::seconds(1)); return self; });
}


#if 0
cti::continuable< std::shared_ptr<xtransmit::srt::socket> > xtransmit::srt::socket::async_connect()
{
	const int no = 0;
	const int yes = 1;

	int result = srt_setsockopt(m_bindsocket, 0, SRTO_RCVSYN, &yes, sizeof yes);
	//if (result == -1)
	//	return cti::make_exceptional_continuable< std::shared_ptr<xtransmit::srt::socket>>();



	return cti::make_continuable< std::shared_ptr<xtransmit::srt::socket>>([](auto && promise) {
		sockaddr_in sa = CreateAddrInet(m_host, m_port);
		sockaddr* psa = (sockaddr*)& sa;

		std::cerr << "Connecting to " << m_host << ":" << m_port << " ... \n";
		int stat = srt_connect(m_bindsocket, psa, sizeof sa);
		if (stat == SRT_ERROR)
		{
			srt_close(m_bindsock);
			Verb() << " failed: " << srt_getlasterror_str();
			return SRT_ERROR;
		}

		result = srt_setsockopt(m_bindsock, 0, SRTO_RCVSYN, &no, sizeof no);
		if (result == -1)
		{
			Verb() << " failed while setting socket options: " << srt_getlasterror_str();
			return result;
		}

		const int events = SRT_EPOLL_IN | SRT_EPOLL_ERR;
		srt_epoll_add_usock(m_epoll_receive, m_bindsock, &events);
		Verb() << " suceeded";
		});


	const int no = 0;
	const int yes = 1;

	int result = srt_setsockopt(m_bindsocket, 0, SRTO_RCVSYN, &yes, sizeof yes);
	if (result == -1)
		return result;

	Verb() << "Connecting to " << m_host << ":" << m_port << " ... " << VerbNoEOL;
	int stat = srt_connect(m_bindsock, psa, sizeof sa);
	if (stat == SRT_ERROR)
	{
		srt_close(m_bindsock);
		Verb() << " failed: " << srt_getlasterror_str();
		return SRT_ERROR;
	}

	result = srt_setsockopt(m_bindsock, 0, SRTO_RCVSYN, &no, sizeof no);
	if (result == -1)
	{
		Verb() << " failed while setting socket options: " << srt_getlasterror_str();
		return result;
	}

	const int events = SRT_EPOLL_IN | SRT_EPOLL_ERR;
	srt_epoll_add_usock(m_epoll_receive, m_bindsock, &events);
	Verb() << " suceeded";

	return cti::make_ready_continuable<std::shared_ptr<xtransmit::srt::socket>>(shared_from_this());
}
#endif

