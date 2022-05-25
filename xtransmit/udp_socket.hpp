#pragma once
#include <map>
#include <future>
#include <string>

// xtransmit
#include "buffer.hpp"
#include "socket.hpp"
#include "netaddr_any.hpp"

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
	bool is_caller() const final { return m_host != ""; }

	SOCKET id() const final { return m_bind_socket; }

	string host() const { return m_host; }
	int port() const { return m_port; }
	const netaddr_any& dst_addr() const { return m_dst_addr; }

public:
	/**
	 * @returns The number of bytes received.
	 *
	 * @throws socket_exception Thrown on failure.
	 */
	size_t read(const mutable_buffer &buffer, int timeout_ms = -1) final;
	int    write(const const_buffer &buffer, int timeout_ms = -1) final;

	std::pair<size_t, netaddr_any>
		recvfrom(const mutable_buffer& buffer, int timeout_ms = -1);

	int sendto(const netaddr_any& dst_addr, const const_buffer& buffer, int timeout_ms = -1);

private:
	SOCKET m_bind_socket = -1; // INVALID_SOCK;
	netaddr_any m_dst_addr;

	bool                     m_blocking_mode = false;
	string                   m_host;
	int                      m_port;
	std::map<string, string> m_options; // All other options, as provided in the URI
};

} // namespace socket
} // namespace xtransmit
