#pragma once
#include <map>;
#include <future>
#include <string>

// xtransmit
#include "buffer.hpp"
#include "socket.hpp"

// OpenSRT
#include "uriparser.hpp"

namespace xtransmit
{
namespace socket
{

class udp
	: public std::enable_shared_from_this<udp>
	, public isocket
{
	using shared_udp = std::shared_ptr<udp>;
	using string     = std::string;

public:
	udp(const UriParser &src_uri);
	~udp();

public:
	void listen();

public:
	bool is_caller() const final { return m_is_caller; }

public:
	/**
	 * @returns The number of bytes received.
	 *
	 * @throws socket_exception Thrown on failure.
	 */
	size_t read(const mutable_buffer &buffer, int timeout_ms = -1) final;
	int    write(const const_buffer &buffer, int timeout_ms = -1) final;

private:
	SOCKET m_bind_socket = -1; // INVALID_SOCK;
	int    m_epoll_io    = -1;
	bool   m_is_caller     = false;

	sockaddr_in m_dst_addr = {};

	bool                     m_blocking_mode = false;
	string                   m_host;
	int                      m_port;
	std::map<string, string> m_options; // All other options, as provided in the URI
};

} // namespace socket
} // namespace xtransmit
