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

		modes = SRT_EPOLL_IN | SRT_EPOLL_OUT;
		if (SRT_ERROR == srt_epoll_add_usock(m_epoll_io, m_bind_socket, &modes))
			throw socket_exception(srt_getlasterror_str());
	}

	if (SRT_SUCCESS != configure_pre(m_bind_socket))
		throw socket_exception(srt_getlasterror_str());
}


void xtransmit::srt::socket::listen()
{


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

	//return async(std::launch::async, [self]() {std::this_thread::sleep_for(std::chrono::seconds(1)); return self; });
}


std::future<shared_socket> srt::socket::async_read(std::vector<char>& buffer)
{
	return std::future<shared_socket>();
}



std::future<shared_socket> srt::socket::async_establish(bool is_caller)
{
	return std::future<shared_socket>();
#if 0
	m_bind_socket = srt_create_socket();

	if (m_bind_socket == SRT_INVALID_SOCK)
		throw socket_exception(srt_getlasterror_str());

	int result = configure_pre(m_bind_socket);
	if (result == SRT_ERROR)
		throw socket_exception(srt_getlasterror_str());

	sockaddr_in sa = CreateAddrInet(m_host, m_port);
	sockaddr * psa = (sockaddr*)& sa;

	if (is_caller)
	{
		const int no = 0;
		const int yes = 1;

		int result = 0;
		result = srt_setsockopt(m_bind_socket, 0, SRTO_RCVSYN, &yes, sizeof yes);
		if (result == SRT_ERROR)
			throw socket_exception(srt_getlasterror_str());

		Verb() << "Connecting to " << m_host << ":" << m_port << " ... " << VerbNoEOL;
		int stat = srt_connect(m_bind_socket, psa, sizeof sa);
		if (stat == SRT_ERROR)
		{
			srt_close(m_bind_socket);
			Verb() << " failed: " << srt_getlasterror_str();
			return SRT_ERROR;
		}

		result = srt_setsockopt(m_bind_socket, 0, SRTO_RCVSYN, &no, sizeof no);
		if (result == -1)
		{
			Verb() << " failed while setting socket options: " << srt_getlasterror_str();
			return result;
		}

		const int events = SRT_EPOLL_IN | SRT_EPOLL_ERR;
		srt_epoll_add_usock(m_epoll_receive, m_bind_socket, &events);
		Verb() << " suceeded";
	}
	else
	{
		Verb() << "Binding a server on " << m_host << ":" << m_port << VerbNoEOL;
		stat = srt_bind(m_bind_socket, psa, sizeof sa);
		if (stat == SRT_ERROR)
		{
			srt_close(m_bind_socket);
			return SRT_ERROR;
		}


		const int modes = SRT_EPOLL_IN;
		srt_epoll_add_usock(m_epoll_accept, m_bind_socket, &modes);

		stat = srt_listen(m_bind_socket, max_conn);
		if (stat == SRT_ERROR)
		{
			srt_close(m_bind_socket);
			Verb() << ", but listening failed with " << srt_getlasterror_str();
			return SRT_ERROR;
		}

		Verb() << " and listening";

		//m_accepting_thread = thread(&SrtNode::AcceptingThread, this);
	}

	return 0;
#endif
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



int xtransmit::srt::socket::write(const vector<char> &buffer)
{
	// Check first if it's ready to write.
	// If not, wait indefinitely.
	if (!m_blocking_mode)
	{
		int ready[2];
		int len = 2;
		if (srt_epoll_wait(m_epoll_io, 0, 0, ready, &len, -1, 0, 0, 0, 0) == SRT_ERROR)
			raise_exception(UDT::getlasterror(), "socket::write");
	}

	return srt_sendmsg2(m_bind_socket, buffer.data(), (int)buffer.size(), nullptr);
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

