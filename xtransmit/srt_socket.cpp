#include <thread>
#include <iostream>
#include <iterator> // std::ostream_iterator

#include "srt_socket.hpp"


#include "verbose.hpp"
#include "socketoptions.hpp"
#include "apputil.hpp"



using namespace std;
using namespace xtransmit;
using shared_socket  = shared_ptr<srt::socket>;



srt::socket::socket(const UriParser& src_uri)
	: m_host(src_uri.host())
	, m_port(src_uri.portno())
	, m_options(src_uri.parameters())
{
	m_bind_socket = srt_create_socket();
	if (m_bind_socket == SRT_INVALID_SOCK)
		throw socket_exception(srt_getlasterror_str());

	if (m_options.count("blocking"))
	{
		m_blocking_mode = !false_names.count(m_options.at("blocking"));
		m_options.erase("blocking");
	}

	if (!m_blocking_mode)
	{
		m_epoll_connect = srt_epoll_create();
		if (m_epoll_connect == -1)
			throw socket_exception(srt_getlasterror_str());

		int modes = SRT_EPOLL_OUT;
		if (SRT_ERROR == srt_epoll_add_usock(m_epoll_connect, m_bind_socket, &modes))
			throw socket_exception(srt_getlasterror_str());

		m_epoll_io = srt_epoll_create();
		modes = SRT_EPOLL_IN | SRT_EPOLL_OUT | SRT_EPOLL_ERR;
		if (SRT_ERROR == srt_epoll_add_usock(m_epoll_io, m_bind_socket, &modes))
			throw socket_exception(srt_getlasterror_str());
	}

	if (SRT_SUCCESS != configure_pre(m_bind_socket))
		throw socket_exception(srt_getlasterror_str());
}


xtransmit::srt::socket::socket(const int sock, bool blocking)
	: m_bind_socket(sock)
	, m_blocking_mode(blocking)
{
	if (!m_blocking_mode)
	{
		m_epoll_io = srt_epoll_create();
		int modes = SRT_EPOLL_IN | SRT_EPOLL_OUT | SRT_EPOLL_ERR;
		if (SRT_ERROR == srt_epoll_add_usock(m_epoll_io, m_bind_socket, &modes))
			throw socket_exception(srt_getlasterror_str());
	}
}


xtransmit::srt::socket::~socket()
{
	if (!m_blocking_mode)
	{
		Verb() << "Releasing epolls for socket " << m_bind_socket;
		if (m_epoll_connect != -1)
			srt_epoll_release(m_epoll_connect);
		srt_epoll_release(m_epoll_io);
	}
	Verb() << "Closing socket " << m_bind_socket;
	srt_close(m_bind_socket);
}


void xtransmit::srt::socket::listen()
{
	int num_clients = 2;
	sockaddr_in sa = CreateAddrInet(m_host, m_port);
	sockaddr* psa = (sockaddr*)& sa;
	Verb() << "Binding a server on " << m_host << ":" << m_port << " ..." << VerbNoEOL;
	int res = srt_bind(m_bind_socket, psa, sizeof sa);
	if (res == SRT_ERROR)
	{
		srt_close(m_bind_socket);
		raise_exception(UDT::getlasterror(), "srt_bind");
	}

	res = srt_listen(m_bind_socket, num_clients);
	if (res == SRT_ERROR)
	{
		srt_close(m_bind_socket);
		raise_exception(UDT::getlasterror(), "srt_listen");
	}


	Verb() << " connected.";
	res = configure_post(m_bind_socket);
	if (res == SRT_ERROR)
		raise_exception(UDT::getlasterror(), "configure_post");

}


shared_socket xtransmit::srt::socket::accept()
{
	sockaddr_in scl;
	int sclen = sizeof scl;

	// Wait for REAL connected state if nonblocking mode
	if (!m_blocking_mode)
	{
		Verb() << "[ASYNC] " << VerbNoEOL;

		// Socket readiness for connection is checked by polling on WRITE allowed sockets.

		constexpr int timeout_ms = -1;
		int len = 2;
		SRTSOCKET ready[2];
		if (srt_epoll_wait(m_epoll_connect, 0, 0, ready, &len, timeout_ms, 0, 0, 0, 0) == -1)
		{
			//if (srt_getlasterror(nullptr) == SRT_ETIMEOUT)
			//	continue;

			raise_exception(UDT::getlasterror(), "srt_epoll_wait");
		}

		Verb() << "[EPOLL: " << len << " sockets] " << VerbNoEOL;
	}

	SRTSOCKET sock = srt_accept(m_bind_socket, (sockaddr*)& scl, &sclen);
	if (sock == SRT_INVALID_SOCK)
	{
		raise_exception(UDT::getlasterror(), "srt_accept");
	}

	// we do one client connection at a time,
	// so close the listener.
	//srt_close(m_bindsock);
	//m_bindsock = SRT_INVALID_SOCK;

	Verb() << " connected.";

	int res = configure_post(sock);
	if (res == SRT_ERROR)
		raise_exception(UDT::getlasterror(), "configure_post");

	return make_shared<socket>(sock, m_blocking_mode);
}



void srt::socket::raise_exception(UDT::ERRORINFO& udt_error, const string&& src)
{
	const int udt_result = udt_error.getErrorCode();
	const string message = udt_error.getErrorMessage();
	Verb() << src << " ERROR #" << udt_result << ": " << message;

	udt_error.clear();
	throw socket_exception("error: " + src + ": " + message);
}



shared_socket srt::socket::connect()
{
	sockaddr_in sa = CreateAddrInet(m_host, m_port);
	sockaddr* psa = (sockaddr*)& sa;
	Verb() << "Connecting to " << m_host << ":" << m_port << " ... " << VerbNoEOL;
	int res = srt_connect(m_bind_socket, psa, sizeof sa);
	if (res == SRT_ERROR)
	{
		srt_close(m_bind_socket);
		raise_exception(UDT::getlasterror(), "srt_connect");
	}

	// Wait for REAL connected state if nonblocking mode
	if (!m_blocking_mode)
	{
		Verb() << "[ASYNC] " << VerbNoEOL;

		// Socket readiness for connection is checked by polling on WRITE allowed sockets.
		int len = 2;
		SRTSOCKET ready[2];
		if (srt_epoll_wait(m_epoll_connect, 0, 0, ready, &len, -1, 0, 0, 0, 0) != -1)
		{
			Verb() << "[EPOLL: " << len << " sockets] " << VerbNoEOL;
		}
		else
		{
			raise_exception(UDT::getlasterror(), "srt_epoll_wait");
		}
	}

	Verb() << " connected.";
	res = configure_post(m_bind_socket);
	if (res == SRT_ERROR)
		raise_exception(UDT::getlasterror(), "configure_post");

	return shared_from_this();
}



std::future<shared_socket> srt::socket::async_connect()
{
	auto self = shared_from_this();

	return async(std::launch::async, [self]() {
		return self->connect();
		});
}


std::future<shared_socket> srt::socket::async_accept()
{
	listen();
	auto self = shared_from_this();

	return async(std::launch::async, [self]() {
		return self->accept();
		});
}


std::future<shared_socket> srt::socket::async_read(std::vector<char>& buffer)
{
	return std::future<shared_socket>();
}




int xtransmit::srt::socket::configure_pre(SRTSOCKET sock)
{
	int maybe = m_blocking_mode ? 1 : 0;
	int result = srt_setsockopt(sock, 0, SRTO_RCVSYN, &maybe, sizeof maybe);
	if (result == -1)
		return result;

	// host is only checked for emptiness and depending on that the connection mode is selected.
	// Here we are not exactly interested with that information.
	std::vector<string> failures;

	// NOTE: here host = "", so the 'connmode' will be returned as LISTENER always,
	// but it doesn't matter here. We don't use 'connmode' for anything else than
	// checking for failures.
	SocketOption::Mode conmode = SrtConfigurePre(sock, "", m_options, &failures);

	if (conmode == SocketOption::FAILURE)
	{
		if (Verbose::on)
		{
			Verb() << "WARNING: failed to set options: ";
			copy(failures.begin(), failures.end(), ostream_iterator<string>(*Verbose::cverb, ", "));
			Verb();
		}

		return SRT_ERROR;
	}

	return SRT_SUCCESS;
}



int xtransmit::srt::socket::configure_post(SRTSOCKET sock)
{
	int is_blocking = m_blocking_mode ? 1 : 0;

	int result = srt_setsockopt(sock, 0, SRTO_SNDSYN, &is_blocking, sizeof is_blocking);
	if (result == -1)
		return result;
	result = srt_setsockopt(sock, 0, SRTO_RCVSYN, &is_blocking, sizeof is_blocking);
	if (result == -1)
		return result;

	// host is only checked for emptiness and depending on that the connection mode is selected.
	// Here we are not exactly interested with that information.
	vector<string> failures;

	SrtConfigurePost(sock, m_options, &failures);

	if (!failures.empty())
	{
		if (Verbose::on)
		{
			Verb() << "WARNING: failed to set options: ";
			copy(failures.begin(), failures.end(), ostream_iterator<string>(*Verbose::cverb, ", "));
			Verb();
		}
	}

	return 0;
}


size_t xtransmit::srt::socket::read(std::vector<char>& buffer, int timeout_ms)
{
	if (!m_blocking_mode)
	{
		int ready[2] = { SRT_INVALID_SOCK, SRT_INVALID_SOCK };
		int len = 2;

		const int epoll_res = srt_epoll_wait(m_epoll_io, ready, &len, nullptr, nullptr, timeout_ms, 0, 0, 0, 0);
		if (epoll_res == SRT_ERROR)
		{
			if (srt_getlasterror(nullptr) == SRT_ETIMEOUT)
				return 0;

			raise_exception(UDT::getlasterror(), "socket::read::epoll " + to_string(srt_getlasterror(nullptr)));
		}

		Verb() << "Socket state: " << srt_getsockstate(m_bind_socket);
	}

	const int res = srt_recvmsg2(m_bind_socket, buffer.data(), (int)buffer.size(), nullptr);
	if (SRT_ERROR == res)
		raise_exception(UDT::getlasterror(), "socket::read::recv");

	return static_cast<size_t>(res);
}


void xtransmit::srt::socket::write(const vector<char> &buffer, int timeout_ms)
{
	if (!m_blocking_mode)
	{
		// TODO: check error fds
		int ready[2] = { SRT_INVALID_SOCK, SRT_INVALID_SOCK };
		int len = 2;
		if (srt_epoll_wait(m_epoll_io, 0, 0, ready, &len, timeout_ms, 0, 0, 0, 0) == SRT_ERROR)
			raise_exception(UDT::getlasterror(), "socket::write::epoll");
	}

	if (SRT_ERROR == srt_sendmsg2(m_bind_socket, buffer.data(), (int)buffer.size(), nullptr))
		raise_exception(UDT::getlasterror(), "socket::write::send");
}


int xtransmit::srt::socket::statistics(SRT_TRACEBSTATS& stats)
{
	return srt_bstats(m_bind_socket, &stats, true);
}



const string xtransmit::srt::socket::statistics_csv(bool print_header)
{
	SRT_TRACEBSTATS stats;
	if (SRT_ERROR == srt_bstats(m_bind_socket, &stats, true))
		raise_exception(UDT::getlasterror(), "socket::statistics");

	std::ostringstream output;

	if (print_header)
	{
		output << "Time,SocketID,pktFlowWindow,pktCongestionWindow,pktFlightSize,";
		output << "msRTT,mbpsBandwidth,mbpsMaxBW,pktSent,pktSndLoss,pktSndDrop,";
		output << "pktRetrans,byteSent,byteAvailSndBuf,byteSndDrop,mbpsSendRate,usPktSndPeriod,";
		output << "pktRecv,pktRcvLoss,pktRcvDrop,pktRcvRetrans,pktRcvBelated,";
		output << "byteRecv,byteAvailRcvBuf,byteRcvLoss,byteRcvDrop,mbpsRecvRate,msRcvTsbPdDelay";
		output << endl;
	}

	output << stats.msTimeStamp << ",";
	output << m_bind_socket << ",";
	output << stats.pktFlowWindow << ",";
	output << stats.pktCongestionWindow << ",";
	output << stats.pktFlightSize << ",";

	output << stats.msRTT << ",";
	output << stats.mbpsBandwidth << ",";
	output << stats.mbpsMaxBW << ",";
	output << stats.pktSent << ",";
	output << stats.pktSndLoss << ",";
	output << stats.pktSndDrop << ",";

	output << stats.pktRetrans << ",";
	output << stats.byteSent << ",";
	output << stats.byteAvailSndBuf << ",";
	output << stats.byteSndDrop << ",";
	output << stats.mbpsSendRate << ",";
	output << stats.usPktSndPeriod << ",";

	output << stats.pktRecv << ",";
	output << stats.pktRcvLoss << ",";
	output << stats.pktRcvDrop << ",";
	output << stats.pktRcvRetrans << ",";
	output << stats.pktRcvBelated << ",";

	output << stats.byteRecv << ",";
	output << stats.byteAvailRcvBuf << ",";
	output << stats.byteRcvLoss << ",";
	output << stats.byteRcvDrop << ",";
	output << stats.mbpsRecvRate << ",";
	output << stats.msRcvTsbPdDelay;

	output << endl;

	return output.str();
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

