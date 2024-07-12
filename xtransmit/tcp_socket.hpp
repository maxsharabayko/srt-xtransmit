#pragma once
#include <map>
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

class tcp
	: public std::enable_shared_from_this<tcp>
	, public isocket
{
	using shared_tcp = std::shared_ptr<tcp>;
	using string     = std::string;

public:
	explicit tcp(const UriParser& src_uri);
	tcp(const int sock, bool blocking);
	~tcp();

public:
	void listen();

	shared_tcp connect();
	shared_tcp accept();

public:
	bool is_caller() const final { return m_host != ""; }

	SOCKET id() const final { return m_bind_socket; }

public:
	/**
	 * @returns The number of bytes received.
	 *
	 * @throws socket_exception Thrown on failure.
	 */
	size_t read(const mutable_buffer& buffer, int timeout_ms = -1) final;
	int    write(const const_buffer& buffer, int timeout_ms = -1) final;

public:
	bool supports_statistics() const final
	{
#ifdef ENABLE_TCP_STATS
		return true;
#else
		return false;
#endif
	}

	const std::string get_statistics(std::string stats_format, bool print_header) const final;

private:
	void raise_exception(const string&& place, const string&& reason) const;

	int get_last_error() const;

	/// @brief Sets blocking flags on a socket.
	/// Does not change the m_blocking_mode.
	/// @param is_blocking true if blocking mode is requested.
	void set_blocking_flags(bool is_blocking) const;

private:
	SOCKET      m_bind_socket = SRT_INVALID_SOCK;
	sockaddr_in m_dst_addr    = {};

	bool                     m_blocking_mode = false;
	string                   m_host;
	int                      m_port;
	std::map<string, string> m_options; // All other options, as provided in the URI
};

} // namespace socket
} // namespace xtransmit
