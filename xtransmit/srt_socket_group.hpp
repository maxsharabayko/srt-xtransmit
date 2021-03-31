#pragma once
#include <memory>
#include <exception>
#include <future>
#include <string>
#include <vector>
#include <map>

// xtransmit
#include "buffer.hpp"
#include "socket.hpp"
#include "scheduler.hpp"

// OpenSRT
#include "srt.h"
#include "uriparser.hpp"

namespace xtransmit
{
namespace socket
{

class srt_group
	: public std::enable_shared_from_this<srt_group>
	, public isocket
{
	using string           = std::string;
	using shared_srt_group = std::shared_ptr<srt_group>;

public:
	explicit srt_group(const std::vector<UriParser>& uris);

	srt_group(srt_group& group, int group_id);

	virtual ~srt_group();

public:
	shared_srt_group connect();
	shared_srt_group accept();

	/**
	 * Start listening on the incomming connection requests.
	 *
	 * May throw a socket_exception.
	 */
	void listen() noexcept(false);


private:
	void configure(const std::map<string, string>& options);

	void identify_connection_mode(const std::vector<UriParser>& uris);

	void set_listen_callback();
	void set_connect_callback();
	/// Set SRT socket options with PRE binding.
	/// @param [in] sock         member socket
	/// @param [in] link_index   link index in m_opts_link
	int  configure_pre(SRTSOCKET sock, int link_index);
	int  configure_post(SRTSOCKET sock, int link_index);
	void create_listeners(const std::vector<UriParser>& uris);
	void create_callers(const std::vector<UriParser>& uris, SRT_GROUP_TYPE gtype);
	void release_targets();
	void release_listeners();

	void on_connect_callback(SRTSOCKET sock, int error, const sockaddr*, int token);
	static void connect_callback_fn(void* opaq, SRTSOCKET sock, int error, const sockaddr* peer, int token);
	int on_listen_callback(SRTSOCKET sock);
	static int listen_callback_fn(void* opaq, SRTSOCKET sock, int hsversion,
		const struct sockaddr* peeraddr, const char* streamid);

	using options = std::map<string, string>;
	static SRT_GROUP_TYPE detect_group_type(const options& opts);

	void print_member_socket(SRTSOCKET sock);

public:
	/**
	 * @returns The number of bytes received.
	 *
	 * @throws socket_exception Thrown on failure.
	 */
	size_t read(const mutable_buffer& buffer, int timeout_ms = -1) final;
	int    write(const const_buffer& buffer, int timeout_ms = -1) final;

	enum connection_mode
	{
		FAILURE    = -1,
		LISTENER   = 0,
		CALLER     = 1,
		RENDEZVOUS = 2
	};

	connection_mode mode() const;

	bool is_caller() const final { return m_mode == CALLER; }

public:
	SOCKET                   id() const final { return m_bind_socket; }
	int                      statistics(SRT_TRACEBSTATS& stats, bool instant = true);
	bool                     supports_statistics() const final { return true; }
	const std::string        statistics_csv(bool print_header) const final;
	static const std::string stats_to_csv(int socketid, const SRT_TRACEBSTATS& stats, uint16_t weight, bool print_header);

private:
	void raise_exception(const string&& place, SRTSOCKET sock = SRT_INVALID_SOCK) const;
	void raise_exception(const string&& place, const string&& reason) const;

private:
	SRTSOCKET              m_bind_socket = SRT_INVALID_SOCK;
	std::vector<SRTSOCKET> m_listeners;
	std::vector<SRT_SOCKGROUPCONFIG> m_targets;
	int                    m_epoll_connect = -1;
	int                    m_epoll_io      = -1;

	connection_mode          m_mode          = FAILURE;
	bool                     m_blocking_mode = true;
	string                   m_host;
	int                      m_port = -1;
	std::vector<options> m_opts_link; // Options per member link. [0] - also defines common options.
	// Link index to token can be determined from m_targets

	scheduler  m_scheduler;
};

} // namespace socket
} // namespace xtransmit
