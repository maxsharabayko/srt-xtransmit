#include "tcp_socket.hpp"
#include "misc.hpp"
#include "socketoptions.hpp"

#ifndef _WIN32
//#include <linux/tcp.h>
#include <netinet/tcp.h>
#endif

// submodules
#include "spdlog/spdlog.h"

using namespace std;
using namespace xtransmit;
using shared_tcp = shared_ptr<socket::tcp>;

#define LOG_SOCK_TCP "SOCKET::TCP "

socket::tcp::tcp(const UriParser& src_uri)
	: m_host(src_uri.host())
	, m_port(src_uri.portno())
	, m_options(src_uri.parameters())
{
	sockaddr_in sa = sockaddr_in();
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = INADDR_ANY;
	m_bind_socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	if (m_bind_socket == INVALID_SOCKET)
		throw socket::exception("Failed to create a TCP socket");

	if (m_options.count("blocking"))
	{
		m_blocking_mode = !false_names.count(m_options.at("blocking"));
		m_options.erase("blocking");
	}
	set_blocking_flags(m_blocking_mode);

	int yes = 1;
	::setsockopt(m_bind_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof yes);

	netaddr_any sa_requested;
	try
	{
		sa_requested = create_addr(m_host, m_port);
	}
	catch (const std::invalid_argument&)
	{
		throw socket::exception("create_addr_inet failed");
	}

	const auto bind_me = [&](const sockaddr* sa) {
		const int       bind_res = ::bind(m_bind_socket, sa, sizeof * sa);
		if (bind_res < 0)
		{
			throw socket::exception("TCP binding has failed");
		}
	};

	bool ip_bonded = false;
	if (m_options.count("bind"))
	{
		string bindipport = m_options.at("bind");
		transform(bindipport.begin(), bindipport.end(), bindipport.begin(), [](char c) { return tolower(c); });
		const size_t idx = bindipport.find(":");
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

		bind_me(reinterpret_cast<const sockaddr*>(&sa_bind));
		ip_bonded = true;
		spdlog::info(LOG_SOCK_TCP "tcp://{}:{:d}: bound to '{}:{}'.",
			m_host, m_port, bindip, bindport);
	}

	if (m_host != "" || ip_bonded)
	{
		m_dst_addr = sa_requested.sin;
	}
	else
	{
		bind_me(reinterpret_cast<const sockaddr*>(&sa_requested));
	}
}

socket::tcp::tcp(const int sock, bool blocking)
	: m_bind_socket(sock)
	, m_blocking_mode(blocking)
{
	set_blocking_flags(m_blocking_mode);
}

socket::tcp::~tcp() { closesocket(m_bind_socket); }

void socket::tcp::listen()
{
	int res = ::listen(m_bind_socket, 1);

	if (res == SRT_ERROR)
	{
		closesocket(m_bind_socket);
		raise_exception("listen", std::to_string(get_last_error()));
	}
}

shared_tcp socket::tcp::connect()
{
	netaddr_any sa;
	try
	{
		sa = create_addr(m_host, m_port);
	}
	catch (const std::invalid_argument& e)
	{
		raise_exception("connect::create_addr", e.what());
	}

	spdlog::debug(LOG_SOCK_TCP "0x{:X} {} Connecting to tcp://{}:{:d}",
		m_bind_socket, m_blocking_mode ? "SYNC" : "ASYNC", m_host, m_port);

	{
		const int res = ::connect(m_bind_socket, sa.get(), sa.size());
		if (res == -1)
		{
#ifdef _WIN32
			// See https://docs.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-connect
			if (get_last_error() != WSAEWOULDBLOCK || m_blocking_mode)
#else
			if (get_last_error() != EINPROGRESS || m_blocking_mode)
#endif
			{
				raise_exception("connect failed", ::to_string(get_last_error()));
			}
		}
	}

	// Wait for REAL connected state if nonblocking mode
	if (!m_blocking_mode)
	{
		fd_set set;
		timeval tv;
		FD_ZERO(&set);
		FD_SET(m_bind_socket, &set);
		tv.tv_sec = 5;
		tv.tv_usec = 0;
		const int select_ret = ::select((int)m_bind_socket + 1, NULL, &set, &set, &tv);

		if (select_ret < 1) {
			spdlog::debug(LOG_SOCK_TCP "0x{:X} ASYNC Can't connect to tcp://{}:{:d}. ::select returned {}",
				m_bind_socket, m_host, m_port, select_ret);

			raise_exception("connect failed", ::to_string(get_last_error()));
		}
	}

	spdlog::debug(LOG_SOCK_TCP "0x{:X} {} Connected to tcp://{}:{:d}",
		m_bind_socket, m_blocking_mode ? "SYNC" : "ASYNC", m_host, m_port);

	return shared_from_this();
}

shared_tcp socket::tcp::accept()
{
	spdlog::debug(LOG_SOCK_TCP "0x{:X} {} Awaiting connection on tcp://{}:{:d}",
		m_bind_socket, m_blocking_mode ? "SYNC" : "ASYNC", m_host, m_port);

	// Wait for REAL connected state if nonblocking mode
	if (!m_blocking_mode)
	{
		fd_set set;
		timeval tv;
		FD_ZERO(&set);
		FD_SET(m_bind_socket, &set);
		tv.tv_sec = 5;
		tv.tv_usec = 0;
		const int select_ret = ::select((int)m_bind_socket + 1, &set, NULL, &set, &tv);

		if (select_ret == -1) {
			spdlog::debug(LOG_SOCK_TCP "0x{:X} ASYNC accept failed on tcp://{}:{:d}. ::select returned {}",
				m_bind_socket, m_host, m_port, select_ret);

			raise_exception("accept failed", ::to_string(select_ret));
		}
	}

	netaddr_any sa(AF_INET);
	socklen_t sa_size = (socklen_t) sa.size();
	const int sock = ::accept(m_bind_socket, sa.get(), &sa_size);
	if (sock == -1)
	{
#ifdef _WIN32
		// See https://docs.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-connect
		if (get_last_error() != WSAEWOULDBLOCK || m_blocking_mode)
#endif
			raise_exception("accept failed", ::to_string(get_last_error()));
	}

	spdlog::debug(LOG_SOCK_TCP "0x{:X} {} Accepted connection 0x{:X} from {}",
		m_bind_socket, m_blocking_mode ? "SYNC" : "ASYNC", sock, sa.str());

	return make_shared<tcp>(sock, m_blocking_mode);
}

void socket::tcp::raise_exception(const string&& place, const string&& reason) const
{
	spdlog::debug(LOG_SOCK_TCP "0x{:X} {}. ERROR: {}.", m_bind_socket, place, reason);
	throw socket::exception(place + ": " + reason);
}

int socket::tcp::get_last_error() const
{
#ifndef _WIN32
	return errno;
#else
	return WSAGetLastError();
#endif
}

void socket::tcp::set_blocking_flags(bool is_blocking) const
{
	unsigned long nonblocking = is_blocking ? 0 : 1;

#if defined(_WIN32)
	if (ioctlsocket(m_bind_socket, FIONBIO, &nonblocking) == SOCKET_ERROR)
#else
	if (ioctl(m_bind_socket, FIONBIO, (const char*)&nonblocking) < 0)
#endif
	{
		throw socket::exception("Failed to set blocking mode for TCP");
	}
}

size_t socket::tcp::read(const mutable_buffer& buffer, int timeout_ms)
{
	while (!m_blocking_mode)
	{
		fd_set fdread;
		fd_set fderror;
		timeval tv;
		FD_ZERO(&fdread);
		FD_SET(m_bind_socket, &fdread);
		FD_ZERO(&fderror);
		FD_SET(m_bind_socket, &fderror);
		tv.tv_sec = 0;
		tv.tv_usec = 10000;
		const int select_ret = ::select((int)(m_bind_socket + 1), &fdread, NULL, &fderror, &tv);

		if (select_ret == -1)
		{
			raise_exception("tcp::read::select", fmt::format("{}", get_last_error()));
		}

		if (select_ret != 0)    // ready
		{
			if (FD_ISSET(m_bind_socket, &fderror))
				spdlog::info(LOG_SOCK_TCP "0x{:X} select signalled error.", m_bind_socket);
			break;
		}

		if (timeout_ms >= 0)   // timeout
			return 0;
	}

	const int res =
		::recv(m_bind_socket, static_cast<char*>(buffer.data()), (int)buffer.size(), 0);
	if (res == -1)
	{
		const int err = get_last_error();
		if (err != EAGAIN && err != EINTR && err != ECONNREFUSED)
			raise_exception("tcp::read::recv", to_string(err));

		spdlog::info("TCP reading failed: error {0}. Again.", err);
		return 0;
	}
	else if (res == 0)
	{
		// TODO: receive - receive does not work. Windows side returns 0 and breaks the connection.
		// With TCP if there was nothing read the connection is likely broken.
		// On Linux get_last_error() returns 22 (EINVAL - fd is attached to an object which is unsuitable for reading).
		raise_exception("tcp::read", fmt::format("zero bytes read (connection broken). Error code {}.", get_last_error()));
	}

	return static_cast<size_t>(res);
}

int socket::tcp::write(const const_buffer& buffer, int timeout_ms)
{
	while (!m_blocking_mode)
	{
		fd_set fdwrite;
		fd_set fderror;
		timeval tv;
		FD_ZERO(&fdwrite);
		FD_SET(m_bind_socket, &fdwrite);
		FD_ZERO(&fderror);
		FD_SET(m_bind_socket, &fderror);
		tv.tv_sec = 0;
		tv.tv_usec = 10000;
		const int select_ret = ::select((int)m_bind_socket + 1, nullptr, &fdwrite, &fderror, &tv);

		if (select_ret == -1)
		{
			raise_exception("tcp::write::select", fmt::format("{}", get_last_error()));
		}

		if (select_ret != 0)    // ready
		{
			if (FD_ISSET(m_bind_socket, &fderror))
				spdlog::info(LOG_SOCK_TCP "0x{:X} select signalled error.", m_bind_socket);
			break;
		}

		if (timeout_ms >= 0)   // timeout
			return 0;
	}

	const int res = ::sendto(m_bind_socket,
		static_cast<const char*>(buffer.data()),
		(int)buffer.size(),
		0,
		(sockaddr*)&m_dst_addr,
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
			spdlog::info("tcp::write::sendto: error {0}.", err);
			throw socket::exception("tcp::write::sendto error");
		}

		spdlog::info("tcp::sendto failed: error {0}. Again.", err);
		return 0;
	}
	else if (res == 0)
	{
		spdlog::info("tcp::sendto returned 0: error {0}.", get_last_error());
	}

	return static_cast<size_t>(res);
}

#ifndef _WIN32
namespace detail {
const string tcp_info_to_csv(int socketid, const tcp_info& stats, bool print_header)
{
	std::ostringstream output;

	if (print_header)
	{
#ifdef HAS_PUT_TIME
		output << "Timepoint,";
#endif
		output << "Time,SocketID,";
		output << "rtt,rttvar,min_rtt,retransmits,snd_mss,rcv_mss,lost,retrans,cwnd,rcv_rtt,rcv_space,bytes_acked,bytes_received,delivery_rate,unacked";
		output << endl;
		return output.str();
	}

#ifdef HAS_PUT_TIME
	output << print_timestamp_now() << ',';
#endif // HAS_PUT_TIME

	//output << stats.tcpi_min_rtt << ',';
	output << stats.tcpi_rtt << ',';
	output << stats.tcpi_rttvar << ',';
	output << stats.tcpi_retransmits << ',';
	output << stats.tcpi_snd_mss << ',';
	output << stats.tcpi_rcv_mss << ',';
	output << stats.tcpi_lost << ',';
	output << stats.tcpi_retrans << ',';
	output << stats.tcpi_snd_cwnd << ',';
	output << stats.tcpi_rcv_rtt << ',';
	output << stats.tcpi_rcv_space << ',';
	//output << stats.tcpi_bytes_acked << ',';
	//output << stats.tcpi_bytes_received << ',';
	//output << stats.tcpi_delivery_rate << ',';
	output << stats.tcpi_unacked;

	output << endl;

	return output.str();

#undef HAS_PUT_TIME
}
}
#endif

const string socket::tcp::statistics_csv(bool print_header) const
{
#ifndef _WIN32
	tcp_info tcp_stats = {};
	socklen_t len = sizeof tcp_stats;

	protoent* ent = getprotobyname("tcp");
	spdlog::info(LOG_SOCK_TCP "getprotobyname TCP {}.", ent->p_proto);

	const int ret = getsockopt(m_bind_socket, SOL_TCP, TCP_INFO, reinterpret_cast<void*>(&tcp_stats), &len);
	if (ret == -1)
		raise_exception("statistics", fmt::format("Error {}", get_last_error()));

	return detail::tcp_info_to_csv(m_bind_socket, tcp_stats, print_header);
#endif

	raise_exception("TCP statistics", "Not implemented");
}


