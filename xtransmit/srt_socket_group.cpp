#include <thread>
#include <iostream>
#include <iterator> // std::ostream_iterator

// submodules
#include "spdlog/spdlog.h"

// xtransmit
#include "srt_socket_group.hpp"
#include "srt_socket.hpp"
#include "misc.hpp" // HAS_PUTTIME

// srt utils
#include "verbose.hpp"
#include "socketoptions.hpp"
#include "apputil.hpp"
#include "common.h" // SRT library's SockStatusStr(..)

using namespace std;
using namespace xtransmit;
using shared_srt_group = shared_ptr<socket::srt_group>;

#define LOG_SRT_GROUP "SOCKET::SRT_GROUP "

SocketOption::Mode detect_srt_mode(const UriParser& uri)
{
	string modestr = "default";
	string adapter;

	const auto& options = uri.parameters();

	if (options.count("mode"))
	{
		modestr = options.at("mode");
	}

	if (options.count("adapter"))
	{
		adapter = options.at("adapter");
	}

	return SrtInterpretMode(modestr, uri.host(), adapter);
}

SRT_GROUP_TYPE socket::srt_group::detect_group_type(const options& opts)
{
	const string key("grouptype");

	if (!opts.count(key))
		return SRT_GTYPE_BROADCAST;

	const string gmode = opts.at(key);
	if (gmode == "broadcast")
		return SRT_GTYPE_BROADCAST;

	if (gmode == "backup")
		return SRT_GTYPE_BACKUP;

	throw socket::exception(LOG_SRT_GROUP ": Failed to detect group mode. Value provided: " + gmode);
}

static int detect_link_weight(const UriParser& uri)
{
	auto& options = uri.parameters();
	const string key("weight");

	if (!options.count(key))
		return 0;

	const string weight_str = options.at(key);
	int weight = 0;
	try {
		weight = std::stoi(weight_str);
	}
	catch (std::invalid_argument const &)
	{
		throw socket::exception(LOG_SRT_GROUP ": Bad input. weight=" + weight_str);
	}
	catch (std::out_of_range const &e)
	{
		throw socket::exception(LOG_SRT_GROUP ": Integer overflow. weight=" + weight_str);
	}

	// the allowed value for weight is between 0 and 32767
	if (weight < 0 || weight >32767)
		throw socket::exception(LOG_SRT_GROUP ": Wrong link weight provided. The allowed value is between 0 and 32767.");

	return weight;
}

SocketOption::Mode validate_srt_group(const vector<UriParser>& urls)
{
	SocketOption::Mode prev_mode = SocketOption::FAILURE;
	// All URLs ha
	for (const auto url : urls)
	{
		if (url.type() != UriParser::SRT)
		{
			spdlog::error(LOG_SRT_GROUP "URI {} is not SRT.", url.uri());
			return SocketOption::FAILURE;
		}

		const auto mode = detect_srt_mode(url);
		if (mode <= SocketOption::FAILURE || mode > SocketOption::RENDEZVOUS)
		{
			spdlog::error(LOG_SRT_GROUP "Failed to detect SRT mode for URI {}.", url.uri());
			return SocketOption::FAILURE;
		}

		if (prev_mode != SocketOption::FAILURE && mode != prev_mode)
		{
			spdlog::error(LOG_SRT_GROUP
						  "Failed to match SRT modes for provided URIs. URI {} has mode {}. Previous mode is {}",
						  url.uri(),
						  SocketOption::mode_names[mode],
						  SocketOption::mode_names[prev_mode]);
			return SocketOption::FAILURE;
		}

		prev_mode = mode;
	}

	return prev_mode;
}

// TODO: m_options per socket:
// - m_opts_group
// - m_opts_link[n]
socket::srt_group::srt_group(const vector<UriParser>& uris)
{
	// validate_srt_group(..) also checks for empty 'uris'
	m_mode = (connection_mode)validate_srt_group(uris);
	if (m_mode == FAILURE)
		throw socket::exception("Group mode validation failed!");
	if (m_mode == RENDEZVOUS)
		throw socket::exception("Rendezvous mode is not supported by socket groups!");

	for (auto uri : uris)
	{
		// Will throw an exception if invalid options were provided.
		srt::assert_options_valid(uri.parameters(), {"bind", "mode", "weight", "grouptype"});
		m_opts_link.push_back(uri.parameters());
	}

	const SRT_GROUP_TYPE gtype = detect_group_type(m_opts_link[0]);

	if (m_opts_link[0].count("blocking"))
	{
		m_blocking_mode = !false_names.count(m_opts_link[0].at("blocking"));
		m_opts_link[0].erase("blocking");
	}

	if (!m_blocking_mode)
	{
		m_epoll_connect = srt_epoll_create();
		if (m_epoll_connect == -1)
			throw socket::exception(srt_getlasterror_str());

		m_epoll_io = srt_epoll_create();
		if (m_epoll_io == -1)
			throw socket::exception(srt_getlasterror_str());
	}

	// Create SRT socket group
	if (m_mode == LISTENER)
	{
		spdlog::trace(LOG_SRT_GROUP "Creating a group of listeners");
		create_listeners(uris);
	}
	else
	{
		const char* gtype_str = (gtype == SRT_GTYPE_BACKUP) ? "main/backup" : "broadcast";
		spdlog::trace(LOG_SRT_GROUP "Creating a group of callers (type {}).", gtype_str);
		create_callers(uris, gtype);
	}
}

socket::srt_group::srt_group(srt_group& group, int group_id)
	: m_bind_socket(group_id)
	, m_blocking_mode(group.m_blocking_mode)
	, m_mode(group.m_mode)
{
	if (!m_blocking_mode)
	{
		m_epoll_io = srt_epoll_create();
		if (m_epoll_io == -1)
			throw socket::exception(srt_getlasterror_str());

		const int io_modes = SRT_EPOLL_IN | SRT_EPOLL_OUT | SRT_EPOLL_ERR;
		if (SRT_ERROR == srt_epoll_add_usock(m_epoll_io, m_bind_socket, &io_modes))
			throw socket::exception(srt_getlasterror_str());
	}
}

socket::srt_group::~srt_group()
{
	if (!m_blocking_mode)
	{
		spdlog::debug(LOG_SRT_GROUP "0x{:X} Closing. Releasing epolls", m_bind_socket);
		if (m_epoll_connect != -1)
			srt_epoll_release(m_epoll_connect);
		if (m_epoll_io != -1)
			srt_epoll_release(m_epoll_io);
	}
	spdlog::debug(LOG_SRT_GROUP "0x{:X} Closing SRT group", m_bind_socket);
	release_targets();
	release_listeners();
	srt_close(m_bind_socket);
}

void socket::srt_group::create_listeners(const vector<UriParser>& src_uri)
{
	// Create listeners according to the parameters
	for (size_t i = 0; i < src_uri.size(); ++i)
	{
		const UriParser& url = src_uri[i];
		sockaddr_any     sa  = CreateAddr(url.host(), url.portno());

		SRTSOCKET s = srt_create_socket();
		if (s == SRT_INVALID_SOCK)
			throw socket::exception(srt_getlasterror_str());

		int gcon = 1;
		if (SRT_SUCCESS != srt_setsockflag(s, SRTO_GROUPCONNECT, &gcon, sizeof gcon))
			throw socket::exception(srt_getlasterror_str());

		if (SRT_SUCCESS != configure_pre(s, i))
			throw socket::exception(srt_getlasterror_str());

		if (SRT_SUCCESS != srt_bind(s, sa.get(), sa.size()))
			throw socket::exception(srt_getlasterror_str());

		if (!m_blocking_mode)
		{
			const int modes = SRT_EPOLL_IN | SRT_EPOLL_ERR;
			if (SRT_ERROR == srt_epoll_add_usock(m_epoll_connect, s, &modes))
				throw socket::exception(srt_getlasterror_str());

			const int io_modes = SRT_EPOLL_IN | SRT_EPOLL_OUT | SRT_EPOLL_ERR;
			if (SRT_ERROR == srt_epoll_add_usock(m_epoll_io, s, &io_modes))
				throw socket::exception(srt_getlasterror_str());
		}

		spdlog::trace(LOG_SRT_GROUP "Created listener 0x{:X} on {}:{}", s, url.host(), url.portno());

		m_listeners.push_back(s);
	}
}

void socket::srt_group::create_callers(const vector<UriParser>& uris, SRT_GROUP_TYPE gtype)
{
	m_bind_socket = srt_create_group(gtype);
	if (m_bind_socket == SRT_INVALID_SOCK)
		raise_exception("srt_create_group");

	if (SRT_SUCCESS != configure_pre(m_bind_socket, 0))
		throw socket::exception(srt_getlasterror_str());

	for (const auto& uri : uris)
	{
		sockaddr_any sa;
		try
		{
			sa = CreateAddr(uri.host(), uri.portno());
		}
		catch (const std::invalid_argument& e)
		{
			raise_exception("connect::create_addr", e.what());
		}

		const sockaddr* bindsa = nullptr;

		SRT_SOCKGROUPCONFIG gd = srt_prepare_endpoint(bindsa, sa.get(), sa.size());

		gd.weight = detect_link_weight(uri);
		m_targets.push_back(gd);
	}

	if (!m_blocking_mode)
	{
		const int modes = SRT_EPOLL_OUT | SRT_EPOLL_ERR;
		if (SRT_ERROR == srt_epoll_add_usock(m_epoll_connect, m_bind_socket, &modes))
			throw socket::exception(srt_getlasterror_str());

		const int io_modes = SRT_EPOLL_IN | SRT_EPOLL_OUT | SRT_EPOLL_ERR;
		if (SRT_ERROR == srt_epoll_add_usock(m_epoll_io, m_bind_socket, &io_modes))
			throw socket::exception(srt_getlasterror_str());
	}

	set_connect_callback();
}

void socket::srt_group::listen()
{
	set_listen_callback();

	for (const auto sockid : m_listeners)
	{
		if (srt_listen(sockid, 5) == SRT_ERROR)
			raise_exception("listen failed with {}", srt_getlasterror_str());
	}
}

shared_srt_group socket::srt_group::accept()
{
	// spdlog::debug(LOG_SRT_GROUP "0x{:X} (srt://{}:{:d}) {} Waiting for incoming connection",
	//	m_bind_socket, m_host, m_port, m_blocking_mode ? "SYNC" : "ASYNC");

	SRTSOCKET accepted_sock = SRT_INVALID_SOCK;
	// Wait for REAL connected state if nonblocking mode
	if (!m_blocking_mode)
	{
		// Socket readiness for connection is checked by polling on READ allowed sockets in case of listeners.
		// In the group connection mode we wait for the first accepted connection.
		constexpr int timeout_ms = -1;
		int           len        = 2;
		SRTSOCKET     ready[2];
		if (srt_epoll_wait(m_epoll_connect, ready, &len, 0, 0, timeout_ms, 0, 0, 0, 0) == SRT_ERROR)
			raise_exception("accept::epoll_wait");

		spdlog::trace(LOG_SRT_GROUP "Epoll read-ready sock 0x{:X}, 0x{:X}", ready[0], len > 1 ? ready[1] : 0);

		sockaddr_in     scl;
		int             sclen      = sizeof scl;
		const SRTSOCKET lstnr_sock = ready[0];
		accepted_sock              = srt_accept(lstnr_sock, (sockaddr*)&scl, &sclen);
		if (accepted_sock == SRT_INVALID_SOCK)
			raise_exception("accept", ready[1]);
	}
	else
	{
		accepted_sock = srt_accept_bond(m_listeners.data(), m_listeners.size(), -1);
		if (accepted_sock == SRT_INVALID_SOCK)
			raise_exception("accept_bond failed with {}", srt_getlasterror_str());
	}

	spdlog::info(LOG_SRT_GROUP "Accepted connection sock 0x{:X}", accepted_sock);
	const int res = configure_post(accepted_sock, 0); // TODO: are there POST options per link?
	if (res == SRT_ERROR)
		raise_exception("accept::configure_post");

	return make_shared<srt_group>(*this, accepted_sock);
}

void socket::srt_group::print_member_socket(SRTSOCKET sock)
{
	int weight = -1; // unknown
	int gtype = -1;
	int gtype_len = sizeof gtype;

	if (srt_getsockflag(sock, SRTO_GROUPTYPE, (void*) &gtype, &gtype_len) == SRT_SUCCESS
		&& gtype == SRT_GTYPE_BACKUP)
	{
		const SRTSOCKET group_id = srt_groupof(sock);
		SRT_SOCKGROUPDATA gdata[3] = {};
		size_t gdata_len = 3;
		const int gsize = srt_group_data(group_id, gdata, &gdata_len);
		for (int i = 0; i < gsize; ++i)
		{
			if (gdata[i].id != sock)
				continue;

			weight = gdata[i].weight;
			break;
		}
	}

	gtype += 1;
	gtype = gtype < 0 ? 0 : (gtype > 3 ? 0 : gtype);
	const char* gtype_str[] = { "NO GROUP", "BROADCAST", "BACKUP", "BALANCING"};
	spdlog::trace(LOG_SRT_GROUP "Member socket 0x{:X}, {} weight = {}", sock,
		gtype_str[gtype], weight);
}

int socket::srt_group::on_listen_callback(SRTSOCKET sock)
{
	m_scheduler.schedule_in(std::chrono::microseconds(20), &socket::srt_group::print_member_socket, this, sock);
	return 0;
}

int socket::srt_group::listen_callback_fn(void* opaq, SRTSOCKET sock, int hsversion,
	const struct sockaddr* peeraddr, const char* streamid)
{
	if (opaq == nullptr)
	{
		spdlog::warn(LOG_SRT_GROUP "listen_callback_fn does not have a pointer to the group");
		return 0;
	}

	sockaddr_any sa(peeraddr);
	spdlog::trace(LOG_SRT_GROUP "Accepted member socket 0x{:X}, remote IP {}", sock, sa.str());

	// TODO: this group may no longer exist. Use some global array to track valid groups.
	socket::srt_group* group = reinterpret_cast<socket::srt_group*>(opaq);
	return group->on_listen_callback(sock);
}

void socket::srt_group::set_listen_callback()
{
	for (const auto sockid : m_listeners)
	{
		if (srt_listen_callback(sockid, listen_callback_fn, (void*) this) == SRT_ERROR)
			raise_exception("listen failed with {}", srt_getlasterror_str());
	}
}

void socket::srt_group::connect_callback_fn(void* opaq, SRTSOCKET sock, int error, const sockaddr* peer, int token)
{
	if (opaq == nullptr)
	{
		spdlog::warn(LOG_SRT_GROUP "connect_callback_fn does not have a pointer to the group");
		return;
	}

	// TODO: this group may no longer exist. Use some global array to track valid groups.
	socket::srt_group* group = reinterpret_cast<socket::srt_group*>(opaq);

	group->on_connect_callback(sock, error, peer, token);
}

void socket::srt_group::on_connect_callback(SRTSOCKET sock, int error, const sockaddr* /*peer*/, int token)
{
	if (error == SRT_SUCCESS)
	{
		// After SRT v1.4.2 connection callback is no longer called on connection success.
		spdlog::trace(LOG_SRT_GROUP "Member socket connected 0x{:X} (token {}).", sock, token);
		return;
	}

	spdlog::warn(LOG_SRT_GROUP "Member socket 0x{:X} (token {}) connection failed: ({}) {}.", sock, token, error,
		srt_strerror(error, 0));

	bool reconn_scheduled = false;
	for (auto target : m_targets)
	{
		if (target.token != token)
			continue;

		auto connfn = [](SRTSOCKET group, SRT_SOCKGROUPCONFIG target) {
			spdlog::trace(LOG_SRT_GROUP "0x{:X}: Reconnecting member socket (token {})", group, target.token);
			const int st = srt_connect_group(group, &target, 1);
			if (st == SRT_ERROR)
				spdlog::warn(LOG_SRT_GROUP "0x{:X}: Member reconnection failed (token {})", group, target.token);
		};

		spdlog::trace(LOG_SRT_GROUP "0x{:X}: Scheduling member reconnection (token {})", m_bind_socket, token);
		reconn_scheduled = true;
		m_scheduler.schedule_in(std::chrono::seconds(1), connfn, m_bind_socket, target);
	}

	if (!reconn_scheduled)
		spdlog::warn(LOG_SRT_GROUP "0x{:X}: Could not schedule member reconnection (token {})", m_bind_socket, token);

	return;
}

void socket::srt_group::set_connect_callback()
{
	srt_connect_callback(m_bind_socket, connect_callback_fn, (void*) this);
}

void socket::srt_group::raise_exception(const string&& place, SRTSOCKET sock) const
{
	const int    udt_result = srt_getlasterror(nullptr);
	const string message    = srt_getlasterror_str();
	spdlog::debug(
		LOG_SRT_GROUP "0x{:X} {} ERROR {} {}", sock != SRT_INVALID_SOCK ? sock : m_bind_socket, place, udt_result, message);
	throw socket::exception(place + ": " + message);
}

void socket::srt_group::raise_exception(const string&& place, const string&& reason) const
{
	spdlog::debug(LOG_SRT_GROUP "0x{:X} {} ERROR {}", m_bind_socket, place, reason);
	throw socket::exception(place + ": " + reason);
}

void socket::srt_group::release_targets()
{
	for (auto& gd : m_targets)
		srt_delete_config(gd.config);
	m_targets.clear();
}

void socket::srt_group::release_listeners()
{
	for (auto sock : m_listeners) {
		spdlog::trace(LOG_SRT_GROUP "Closing listener 0x{:X}", sock);
		srt_close(sock);
	}
	m_listeners.clear();
}

shared_srt_group socket::srt_group::connect()
{
	spdlog::debug(
		LOG_SRT_GROUP "0x{:X} {} Connecting group to remote SRT", m_bind_socket, m_blocking_mode ? "SYNC" : "ASYNC");

	if (!m_blocking_mode && false)
	{
		// This branch does not assign a token to the target
		// therefore it is not possible to schedule a reconnection.
		// srt_connect_group is to be used instead in both blocking and non-blocking modes.
		spdlog::debug(
			LOG_SRT_GROUP "non blocking");
		for (auto target : m_targets)
		{
			sockaddr_any target_addr(target.peeraddr);
			const int    st = srt_connect(m_bind_socket, target_addr.get(), target_addr.size());
			if (st == SRT_ERROR)
				raise_exception("srt_group::connect_member");
		}

		// In case of a caller a connection event triggers write-readiness.
		int       len = 2;
		SRTSOCKET ready[2];
		if (srt_epoll_wait(m_epoll_connect, 0, 0, ready, &len, -1, 0, 0, 0, 0) != -1)
		{
			if (srt_getsockstate(m_bind_socket) != SRTS_CONNECTED)
			{
				const int reason = srt_getrejectreason(m_bind_socket);
				raise_exception("connect failed", srt_rejectreason_str(reason));
			}
		}
		else
		{
			raise_exception("srt_group::connect.epoll_wait");
		}
	}
	else
	{
		spdlog::debug(
			LOG_SRT_GROUP "srt_connect_group");
		const int st = srt_connect_group(m_bind_socket, m_targets.data(), m_targets.size());
		if (st == SRT_ERROR)
			raise_exception("srt_group::connect");
	}

	spdlog::debug(
		LOG_SRT_GROUP "0x{:X} {} Group member connected to remote", m_bind_socket, m_blocking_mode ? "SYNC" : "ASYNC");

	return shared_from_this();
}

int socket::srt_group::configure_pre(SRTSOCKET sock, int link_index)
{
	SRT_ASSERT(link_index < m_opts_link.size());
	int       maybe  = m_blocking_mode ? 1 : 0;
	const int result = srt_setsockopt(sock, 0, SRTO_RCVSYN, &maybe, sizeof maybe);
	if (result == -1)
		return result;

	const auto configure_link = [&](int li) -> int {
		// host is only checked for emptiness and depending on that the connection mode is selected.
		// Here we are not exactly interested with that information.
		std::vector<string> failures;

		// NOTE: here host = "", so the 'connmode' will be returned as LISTENER always,
		// but it doesn't matter here. We don't use 'connmode' for anything else than
		// checking for failures.
		// TODO: use per-link options too
		SocketOption::Mode conmode = SrtConfigurePre(sock, m_host, m_opts_link[li], &failures);

		if (conmode == SocketOption::FAILURE)
		{
			stringstream ss;
			for (const auto v : failures) ss << v << ", ";
			spdlog::error(LOG_SRT_GROUP "WARNING: failed to set options: {}", ss.str());
			return SRT_ERROR;
		}

		return SRT_SUCCESS;
	};

	if (configure_link(0) != SRT_SUCCESS)
		return SRT_ERROR;

	if (link_index != 0)
		return configure_link(link_index);

	return SRT_SUCCESS;
}

int socket::srt_group::configure_post(SRTSOCKET sock, int link_index)
{
	SRT_ASSERT(link_index < m_opts_link.size());
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

	SrtConfigurePost(sock, m_opts_link[link_index], &failures);

	if (!failures.empty())
	{
		if (Verbose::on)
		{
			stringstream ss;
			for (const auto v : failures) ss << v << ", ";
			spdlog::error(LOG_SRT_GROUP "WARNING: failed to set options: {}", ss.str());
		}
	}

	return 0;
}

size_t socket::srt_group::read(const mutable_buffer& buffer, int timeout_ms)
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

			raise_exception("read::epoll");
		}
	}

	const int res = srt_recvmsg2(m_bind_socket, static_cast<char*>(buffer.data()), (int)buffer.size(), nullptr);
	if (SRT_ERROR == res)
	{
		if (srt_getlasterror(nullptr) != SRT_EASYNCRCV)
			raise_exception("read::recv");

		spdlog::warn(LOG_SRT_GROUP "recvmsg returned error 6002: read error, try again");
		return 0;
	}

	return static_cast<size_t>(res);
}

int socket::srt_group::write(const const_buffer& buffer, int timeout_ms)
{
	if (!m_blocking_mode)
	{
		int ready[2]  = {SRT_INVALID_SOCK, SRT_INVALID_SOCK};
		int len       = 2;
		int rready[2] = {SRT_INVALID_SOCK, SRT_INVALID_SOCK};
		int rlen      = 2;
		// TODO: check error fds
		const int res = srt_epoll_wait(m_epoll_io, rready, &rlen, ready, &len, timeout_ms, 0, 0, 0, 0);
		if (res == SRT_ERROR)
			raise_exception("write::epoll");
	}

	const int res =
		srt_sendmsg2(m_bind_socket, static_cast<const char*>(buffer.data()), static_cast<int>(buffer.size()), nullptr);
	if (res == SRT_ERROR)
	{
		if (srt_getlasterror(nullptr) == SRT_EASYNCSND)
			return 0;

		raise_exception("socket::write::send", srt_getlasterror_str());
	}

	return res;
}

socket::srt_group::connection_mode socket::srt_group::mode() const { return m_mode; }

int socket::srt_group::statistics(SRT_TRACEBSTATS& stats, bool instant)
{
	return srt_bstats(m_bind_socket, &stats, instant);
}

const string socket::srt_group::stats_to_csv(int socketid, const SRT_TRACEBSTATS& stats, bool print_header)
{
	std::ostringstream output;

#define HAS_PKT_REORDER_TOL (SRT_VERSION_MAJOR >= 1) && (SRT_VERSION_MINOR >= 4) && (SRT_VERSION_PATCH > 0)
// pktSentUnique, pktRecvUnique were added in SRT v1.4.2
#define HAS_UNIQUE_PKTS                                                                                                \
	(SRT_VERSION_MAJOR == 1) && ((SRT_VERSION_MINOR > 4) || ((SRT_VERSION_MINOR == 4) && (SRT_VERSION_PATCH >= 2)))

	if (print_header)
	{
#ifdef HAS_PUT_TIME
		output << "Timepoint,";
#endif
		output << "Time,SocketID,pktFlowWindow,pktCongestionWindow,pktFlightSize,";
		output << "msRTT,mbpsBandwidth,mbpsMaxBW,pktSent,";
#if HAS_UNIQUE_PKTS
		output << "pktSentUnique,";
#endif
		output << "pktSndLoss,pktSndDrop,pktRetrans,byteSent,";
		output << "byteAvailSndBuf,byteSndDrop,mbpsSendRate,usPktSndPeriod,msSndBuf,pktRecv,";
#if HAS_UNIQUE_PKTS
		output << "pktRecvUnique,";
#endif
		output << "pktRcvLoss,pktRcvDrop,pktRcvRetrans,pktRcvBelated,";
		output << "byteRecv,byteAvailRcvBuf,byteRcvLoss,byteRcvDrop,mbpsRecvRate,msRcvBuf,msRcvTsbPdDelay";
#if HAS_PKT_REORDER_TOL
		output << ",pktReorderTolerance";
#endif
		output << endl;
		return output.str();
	}

#ifdef HAS_PUT_TIME
	output << print_timestamp_now() << ',';
#endif // HAS_PUT_TIME

	output << stats.msTimeStamp << ',';
	output << socketid << ',';
	output << stats.pktFlowWindow << ',';
	output << stats.pktCongestionWindow << ',';
	output << stats.pktFlightSize << ',';

	output << stats.msRTT << ',';
	output << stats.mbpsBandwidth << ',';
	output << stats.mbpsMaxBW << ',';
	output << stats.pktSent << ',';
#if HAS_UNIQUE_PKTS
	output << stats.pktSentUnique << ",";
#endif
	output << stats.pktSndLoss << ',';
	output << stats.pktSndDrop << ',';

	output << stats.pktRetrans << ',';
	output << stats.byteSent << ',';
	output << stats.byteAvailSndBuf << ',';
	output << stats.byteSndDrop << ',';
	output << stats.mbpsSendRate << ',';
	output << stats.usPktSndPeriod << ',';
	output << stats.msSndBuf << ',';

	output << stats.pktRecv << ',';
#if HAS_UNIQUE_PKTS
	output << stats.pktRecvUnique << ",";
#endif
	output << stats.pktRcvLoss << ',';
	output << stats.pktRcvDrop << ',';
	output << stats.pktRcvRetrans << ',';
	output << stats.pktRcvBelated << ',';

	output << stats.byteRecv << ',';
	output << stats.byteAvailRcvBuf << ',';
	output << stats.byteRcvLoss << ',';
	output << stats.byteRcvDrop << ',';
	output << stats.mbpsRecvRate << ',';
	output << stats.msRcvBuf << ',';
	output << stats.msRcvTsbPdDelay;

#if HAS_PKT_REORDER_TOL
	output << "," << stats.pktReorderTolerance;
#endif

	output << endl;

	return output.str();

#undef HAS_PUT_TIME
#undef HAS_UNIQUE_PKTS
}

const string socket::srt_group::statistics_csv(bool print_header) const
{
	if (print_header)
		return stats_to_csv(m_bind_socket, SRT_TRACEBSTATS(), print_header);;

	SRT_TRACEBSTATS stats = {};
	if (SRT_ERROR == srt_bstats(m_bind_socket, &stats, true))
		raise_exception("statistics");
	string csv_stats = stats_to_csv(m_bind_socket, stats, print_header);

	size_t group_size = 0;
	if (srt_group_data(m_bind_socket, NULL, &group_size) != SRT_SUCCESS)
	{
		// Not throwing an exception as group stats was retrieved.
		spdlog::warn(LOG_SRT_GROUP "0x{:X} statistics_csv: Failed to retrieve the number of group members", m_bind_socket);
		return csv_stats;
	}

	vector<SRT_SOCKGROUPDATA> group_data(group_size);
	const int num_members = srt_group_data(m_bind_socket, group_data.data(), &group_size);
	if (num_members == SRT_ERROR)
	{
		// Not throwing an exception as group stats was retrieved.
		spdlog::warn(LOG_SRT_GROUP "0x{:X} statistics_csv: Failed to retrieve group data", m_bind_socket);
		return csv_stats;
	}

	for (int i = 0; i < num_members; ++i)
	{
		const int id = group_data[i].id;
		const SRT_SOCKSTATUS status = group_data[i].sockstate;

		if (group_data[i].sockstate != SRTS_CONNECTED)
		{
			spdlog::trace(LOG_SRT_GROUP "0x{:X} statistics_csv: Socket state is {}, skipping.", id, srt_logging::SockStatusStr(status));
			continue;
		}

		if (SRT_ERROR == srt_bstats(id, &stats, true))
		{
			spdlog::warn(LOG_SRT_GROUP "0x{:X} statistics_csv: Failed to retrieve group member stats. {}", id, srt_getlasterror_str());
			break;
		}

		csv_stats += stats_to_csv(id, stats, false);
	}

	return csv_stats;
}
