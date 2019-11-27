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

  public:
	udp(const UriParser &src_uri);
	~udp();

  public:
	void               listen();
	future<shared_udp> async_connect() noexcept(false);
	future<shared_udp> async_accept() noexcept(false);

	shared_udp connect();
	shared_udp accept();

  public:
	std::future<shared_udp> async_read(std::vector<char> &buffer);
	void                    async_write();

	/**
	 * @returns The number of bytes received.
	 *
	 * @throws socket_exception Thrown on failure.
	 */
	size_t read (const mutable_buffer &buffer, int timeout_ms = -1);
	int    write(const const_buffer &buffer, int timeout_ms = -1);

  private:
	SOCKET m_bind_socket   = -1; // INVALID_SOCK;
	int    m_epoll_io      = -1;

	sockaddr_in m_dst_addr = {};

	bool                m_blocking_mode = false;
	string              m_host;
	int                 m_port;
	map<string, string> m_options; // All other options, as provided in the URI
};

} // namespace udp
} // namespace xtransmit
