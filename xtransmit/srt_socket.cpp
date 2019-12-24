#include <thread>
#include <iostream>
#include <iterator> // std::ostream_iterator

// submodules
#include "spdlog/spdlog.h"

// xtransmit
#include "srt_socket.hpp"

// srt utils
#include "verbose.hpp"
#include "socketoptions.hpp"
#include "apputil.hpp"

using namespace std;
using namespace xtransmit;
using shared_srt = shared_ptr<socket::srt>;


#define LOG_SOCK_SRT "SOCKET::SRT "


socket::srt::srt(const UriParser &src_uri)
	: m_host(src_uri.host())
	, m_port(src_uri.portno())
	, m_options(src_uri.parameters())
{
	m_bind_socket = srt_create_socket();
	if (m_bind_socket == SRT_INVALID_SOCK)
		throw socket::exception(srt_getlasterror_str());

	if (m_options.count("blocking"))
	{
		m_blocking_mode = !false_names.count(m_options.at("blocking"));
		m_options.erase("blocking");
	}

	if (!m_blocking_mode)
	{
		m_epoll_connect = srt_epoll_create();
		if (m_epoll_connect == -1)
			throw socket::exception(srt_getlasterror_str());

		int modes = SRT_EPOLL_OUT | SRT_EPOLL_ERR;
		if (SRT_ERROR == srt_epoll_add_usock(m_epoll_connect, m_bind_socket, &modes))
			throw socket::exception(srt_getlasterror_str());

		m_epoll_io = srt_epoll_create();
		modes      = SRT_EPOLL_IN | SRT_EPOLL_OUT | SRT_EPOLL_ERR;
		if (SRT_ERROR == srt_epoll_add_usock(m_epoll_io, m_bind_socket, &modes))
			throw socket::exception(srt_getlasterror_str());
	}

	check_options_exist();

	if (SRT_SUCCESS != configure_pre(m_bind_socket))
		throw socket::exception(srt_getlasterror_str());
}

socket::srt::srt(const int sock, bool blocking)
	: m_bind_socket(sock)
	, m_blocking_mode(blocking)
{
	if (!m_blocking_mode)
	{
		m_epoll_io = srt_epoll_create();
		int modes  = SRT_EPOLL_IN | SRT_EPOLL_OUT | SRT_EPOLL_ERR;
		if (SRT_ERROR == srt_epoll_add_usock(m_epoll_io, m_bind_socket, &modes))
			throw socket::exception(srt_getlasterror_str());
	}
}

socket::srt::~srt()
{
	if (!m_blocking_mode)
	{
		spdlog::debug(LOG_SOCK_SRT "0x{:X} Closing. Releasing epolls", m_bind_socket);
		if (m_epoll_connect != -1)
			srt_epoll_release(m_epoll_connect);
		srt_epoll_release(m_epoll_io);
	}
	spdlog::debug(LOG_SOCK_SRT "0x{:X} Closing", m_bind_socket);
	srt_close(m_bind_socket);
}

void socket::srt::listen()
{
	int         num_clients = 2;
	sockaddr_in sa;

	try
	{
		sa = CreateAddrInet(m_host, m_port);
	}
	catch (const std::invalid_argument &e)
	{
		raise_exception("listen::create_addr", e.what());
	}

	sockaddr *psa = (sockaddr *)&sa;

	int res = srt_bind(m_bind_socket, psa, sizeof sa);
	if (res == SRT_ERROR)
	{
		srt_close(m_bind_socket);
		raise_exception("bind", UDT::getlasterror());
	}

	res = srt_listen(m_bind_socket, num_clients);
	if (res == SRT_ERROR)
	{
		srt_close(m_bind_socket);
		raise_exception("listen", UDT::getlasterror());
	}

	spdlog::debug(LOG_SOCK_SRT "0x{:X} (srt://{}:{:d}) Listening", m_bind_socket, m_host, m_port);
	res = configure_post(m_bind_socket);
	if (res == SRT_ERROR)
		raise_exception("listen::configure_post", UDT::getlasterror());
}

shared_srt socket::srt::accept()
{
	spdlog::debug(LOG_SOCK_SRT "0x{:X} (srt://{}:{:d}) {} Waiting for incoming connection",
		m_bind_socket, m_host, m_port, m_blocking_mode ? "SYNC" : "ASYNC");
	
	// Wait for REAL connected state if nonblocking mode
	if (!m_blocking_mode)
	{

		// Socket readiness for connection is checked by polling on WRITE allowed sockets.
		constexpr int timeout_ms = -1;
		int           len        = 2;
		SRTSOCKET     ready[2];
		if (srt_epoll_wait(m_epoll_connect, 0, 0, ready, &len, timeout_ms, 0, 0, 0, 0) == -1)
		{
			// if (srt_getlasterror(nullptr) == SRT_ETIMEOUT)
			//	continue;

			raise_exception("accept::epoll_wait", UDT::getlasterror());
		}
	}

	sockaddr_in scl;
	int         sclen = sizeof scl;
	const SRTSOCKET sock = srt_accept(m_bind_socket, (sockaddr *)&scl, &sclen);
	if (sock == SRT_INVALID_SOCK)
	{
		raise_exception("accept", UDT::getlasterror());
	}

	// we do one client connection at a time,
	// so close the listener.
	// srt_close(m_bindsock);
	// m_bindsock = SRT_INVALID_SOCK;

	spdlog::debug(LOG_SOCK_SRT "0x{:X} (srt://{}:{:d}) Accepted connection 0x{:X}",
		m_bind_socket, m_host, m_port, sock);

	const int res = configure_post(sock);
	if (res == SRT_ERROR)
		raise_exception("accept::configure_post", UDT::getlasterror());

	return make_shared<srt>(sock, m_blocking_mode);
}

void socket::srt::raise_exception(const string &&place, UDT::ERRORINFO &udt_error) const
{
	const int    udt_result = udt_error.getErrorCode();
	const string message    = udt_error.getErrorMessage();
	spdlog::debug(LOG_SOCK_SRT "0x{:X} {} ERROR {} {}", m_bind_socket, place, udt_result, message);
	udt_error.clear();
	throw socket::exception(place + ": " + message);
}

void socket::srt::raise_exception(const string &&place, const string &&reason) const
{
	spdlog::debug(LOG_SOCK_SRT "0x{:X} {} ERROR {}", m_bind_socket, place, reason);
	throw socket::exception(place + ": " + reason);
}

shared_srt socket::srt::connect()
{
	sockaddr_in sa;
	try
	{
		sa = CreateAddrInet(m_host, m_port);
	}
	catch (const std::invalid_argument &e)
	{
		raise_exception("connect::create_addr", e.what());
	}

	spdlog::debug(LOG_SOCK_SRT "0x{:X} {} Connecting to srt://{}:{:d}",
		m_bind_socket, m_blocking_mode ? "SYNC" : "ASYNC", m_host, m_port);

	sockaddr *psa = (sockaddr *)&sa;
	{
		const int res = srt_connect(m_bind_socket, psa, sizeof sa);
		if (res == SRT_ERROR)
		{
			srt_close(m_bind_socket);
			raise_exception("connect", UDT::getlasterror());
		}
	}

	// Wait for REAL connected state if nonblocking mode
	if (!m_blocking_mode)
	{
		// Socket readiness for connection is checked by polling on WRITE allowed sockets.
		int       len = 2;
		SRTSOCKET ready[2];
		if (srt_epoll_wait(m_epoll_connect, 0, 0, ready, &len, -1, 0, 0, 0, 0) != -1)
		{
			const SRT_SOCKSTATUS state = srt_getsockstate(m_bind_socket);
			if (state != SRTS_CONNECTED)
				raise_exception("connect", "connection failed, socket state " + to_string(state));
		}
		else
		{
			raise_exception("connect.epoll_wait", UDT::getlasterror());
		}
	}

	spdlog::debug(LOG_SOCK_SRT "0x{:X} {} Connected to srt://{}:{:d}",
		m_bind_socket, m_blocking_mode ? "SYNC" : "ASYNC", m_host, m_port);
	{
		const int res = configure_post(m_bind_socket);
		if (res == SRT_ERROR)
			raise_exception("connect::onfigure_post", UDT::getlasterror());
	}

	return shared_from_this();
}

std::future<shared_srt> socket::srt::async_connect()
{
	auto self = shared_from_this();

	return async(std::launch::async, [self]() { return self->connect(); });
}

std::future<shared_srt> socket::srt::async_accept()
{
	listen();

	auto self = shared_from_this();
	return async(std::launch::async, [self]() { return self->accept(); });
}

std::future<shared_srt> socket::srt::async_read(std::vector<char> &buffer)
{
	return std::future<shared_srt>();
}

void socket::srt::check_options_exist() const
{
	for (const auto el : m_options)
	{
		bool opt_found = false;
		for (const auto o : srt_options)
		{
			if (o.name != el.first)
				continue;

			opt_found = true;
			break;
		}
		
		if (opt_found)
			continue;

		spdlog::warn(LOG_SOCK_SRT "srt://{}:{:d}: Ignoring socket option '{}={}' (not recognized)!",
			m_host, m_port, el.first, el.second);
	}
}


int socket::srt::configure_pre(SRTSOCKET sock)
{
	int maybe  = m_blocking_mode ? 1 : 0;
	int result = srt_setsockopt(sock, 0, SRTO_RCVSYN, &maybe, sizeof maybe);
	if (result == -1)
		return result;

	// host is only checked for emptiness and depending on that the connection mode is selected.
	// Here we are not exactly interested with that information.
	std::vector<string> failures;

	// NOTE: here host = "", so the 'connmode' will be returned as LISTENER always,
	// but it doesn't matter here. We don't use 'connmode' for anything else than
	// checking for failures.
	SocketOption::Mode conmode = SrtConfigurePre(sock, m_host, m_options, &failures);

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

	m_mode = static_cast<connection_mode>(conmode);

	return SRT_SUCCESS;
}

int socket::srt::configure_post(SRTSOCKET sock)
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

size_t socket::srt::read(const mutable_buffer &buffer, int timeout_ms)
{
	if (!m_blocking_mode)
	{
		int ready[2] = {SRT_INVALID_SOCK, SRT_INVALID_SOCK};
		int len      = 2;

		const int epoll_res = srt_epoll_wait(m_epoll_io, ready, &len, nullptr, nullptr, timeout_ms, 0, 0, 0, 0);
		if (epoll_res == SRT_ERROR)
		{
			if (srt_getlasterror(nullptr) == SRT_ETIMEOUT)
				return 0;

			raise_exception("read::epoll", UDT::getlasterror());
		}
	}

	const int res = srt_recvmsg2(m_bind_socket, static_cast<char *>(buffer.data()), (int)buffer.size(), nullptr);
	if (SRT_ERROR == res)
		raise_exception("read::recv", UDT::getlasterror());

	return static_cast<size_t>(res);
}

int socket::srt::write(const const_buffer &buffer, int timeout_ms)
{
	stringstream ss;
	if (!m_blocking_mode)
	{
		int ready[2] = {SRT_INVALID_SOCK, SRT_INVALID_SOCK};
		int len      = 2;
		int rready[2] = {SRT_INVALID_SOCK, SRT_INVALID_SOCK};
		int rlen      = 2;
		// TODO: check error fds
		const int res = srt_epoll_wait(m_epoll_io, rready, &rlen, ready, &len, timeout_ms, 0, 0, 0, 0);
		if (res == SRT_ERROR)
			raise_exception("write::epoll", UDT::getlasterror());

		ss << "write::epoll_wait result " << res << " rlen " << rlen << " wlen " << len << " wsocket " << ready[0];
		//Verb() << "srt::socket::write: srt_epoll_wait set len " << len << " socket " << ready[0];
	}

	const int res = srt_sendmsg2(m_bind_socket, static_cast<const char*>(buffer.data()), static_cast<int>(buffer.size()), nullptr);
	if (res == SRT_ERROR)
	{
		size_t blocks, bytes;
		srt_getsndbuffer(m_bind_socket, &blocks, &bytes);
		int sndbuf = 0;
		int optlen = sizeof sndbuf;
		srt_getsockopt(m_bind_socket, 0, SRTO_SNDBUF, &sndbuf, &optlen);
		ss << " SND Buffer: " << bytes << " / " << sndbuf << " bytes";
		ss << " (" << sndbuf - bytes << " bytes remaining)";
		ss << "trying to write " << buffer.size() << "bytes";
		raise_exception("socket::write::send", srt_getlasterror_str() + ss.str());
		//	raise_exception("socket::write::send", UDT::getlasterror());
	}

	return res;
}

socket::srt::connection_mode socket::srt::mode() const
{
	return m_mode;
}

int socket::srt::statistics(SRT_TRACEBSTATS &stats)
{
	return srt_bstats(m_bind_socket, &stats, true);
}

const string socket::srt::statistics_csv(bool print_header)
{
	SRT_TRACEBSTATS stats;
	if (SRT_ERROR == srt_bstats(m_bind_socket, &stats, true))
		raise_exception("statistics", UDT::getlasterror());

	std::ostringstream output;


#define HAS_PKT_REORDER_TOL (SRT_VERSION_MAJOR >= 1) && (SRT_VERSION_MINOR >= 4) && (SRT_VERSION_PATCH > 0)

	if (print_header)
	{
		output << "Time,SocketID,pktFlowWindow,pktCongestionWindow,pktFlightSize,";
		output << "msRTT,mbpsBandwidth,mbpsMaxBW,pktSent,pktSndLoss,pktSndDrop,";
		output << "pktRetrans,byteSent,byteAvailSndBuf,byteSndDrop,mbpsSendRate,usPktSndPeriod,";
		output << "pktRecv,pktRcvLoss,pktRcvDrop,pktRcvRetrans,pktRcvBelated,";
		output << "byteRecv,byteAvailRcvBuf,byteRcvLoss,byteRcvDrop,mbpsRecvRate,msRcvTsbPdDelay";
#if HAS_PKT_REORDER_TOL
		output << ",msRcvTsbPdDelay";
#endif
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

#if	HAS_PKT_REORDER_TOL
	output << "," << stats.pktReorderTolerance;
#endif

	output << endl;

	return output.str();
}
