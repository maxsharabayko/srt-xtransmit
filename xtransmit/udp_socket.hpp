#pragma once
#include <map>
#include <future>
#include <string>

// xtransmit
#include "buffer.hpp"
#include "socket.hpp"

// OpenSRT
#include "uriparser.hpp"
#include "netinet_any.h"
#include "srt.h"

namespace xtransmit
{
namespace socket
{

class udp_base
	: public isocket
{
	using string     = std::string;

public:
	udp_base(const UriParser &src_uri);
	~udp_base();

public:
	void listen();

public:
	bool is_caller() const final { return m_host != ""; }

	SOCKET id() const final { return m_bind_socket; }

protected:
	SOCKET m_bind_socket = -1; // INVALID_SOCK;
	sockaddr_in m_dst_addr = {};

	bool                     m_blocking_mode = false;
	string                   m_host;
	int                      m_port;
	std::map<string, string> m_options; // All other options, as provided in the URI
};

class udp
	: public std::enable_shared_from_this<udp>
    , public udp_base
{
	using shared_udp = std::shared_ptr<udp>;

public:
    using udp_base::udp_base;

public:
	/**
	 * @returns The number of bytes received.
	 *
	 * @throws socket_exception Thrown on failure.
	 */
	size_t read(const mutable_buffer &buffer, int timeout_ms = -1) final;
	int    write(const const_buffer &buffer, int timeout_ms = -1) final;

};

#ifdef __linux__

const auto MAX_SINGLE_READ = 10;

class mudp
	: public std::enable_shared_from_this<mudp>
    , public udp_base
{
	using shared_udp = std::shared_ptr<mudp>;
    char bufspace[MAX_SINGLE_READ][SRT_LIVE_MAX_PLSIZE];
    ::srt::sockaddr_any addresses[MAX_SINGLE_READ];
    mmsghdr mm_array[MAX_SINGLE_READ];
    iovec iovec_array[MAX_SINGLE_READ][1];

    unsigned int* bufsizes[MAX_SINGLE_READ];
    size_t nbuffers = 0;
    size_t cbuffer = 0;


public:
    mudp(const UriParser& u);

public:
	/**
	 * @returns The number of bytes received.
	 *
	 * @throws socket_exception Thrown on failure.
	 */
	size_t read(const mutable_buffer &buffer, int timeout_ms = -1) override final;
	int    write(const const_buffer &buffer, int timeout_ms = -1) override final;

};
#endif

} // namespace socket
} // namespace xtransmit
