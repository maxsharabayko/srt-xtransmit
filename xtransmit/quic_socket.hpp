#pragma once
#include <memory>
#include <exception>
#include <future>
#include <string>
#include <vector>
#include <map>
#include <unordered_set>

// xtransmit
#include "buffer.hpp"
#include "socket.hpp"
#include "udp_socket.hpp"

// Quiche
#include "quiche.h"
// OpenSRT
#include "uriparser.hpp"
#include "netinet_any.h"

namespace xtransmit
{
namespace socket
{

class quic
	: public std::enable_shared_from_this<quic>
	, public isocket
{
	using string      = std::string;
	using shared_quic = std::shared_ptr<quic>;

public:
	explicit quic(const UriParser& src_uri);

	virtual ~quic();

public:
	shared_quic connect();
	shared_quic accept();

	/**
	 * Start listening on the incomming connection requests.
	 *
	 * May throw a socket::exception.
	 */
	void listen() noexcept(false);

	/// Verifies URI options provided are valid.
	///
	/// @param [in] options  a map of options key-value pairs to validate
	/// @param [in] extra a set of extra options that are valid, e.g. "mode", "bind"
	/// @throw socket::exception on failure
	///
	static void assert_options_valid(const std::map<string, string>& options, const std::unordered_set<string>& extra);

	///
	/// @returns The number of bytes received.
	///
	/// @throws socket_exception Thrown on failure.
	///
	size_t read(const mutable_buffer& buffer, int timeout_ms = -1) final;
	int    write(const const_buffer& buffer, int timeout_ms = -1) final;

	bool is_caller() const final { return m_udp.is_caller(); }

	auto udp_recvfrom(const mutable_buffer& buffer, int timeout_ms = -1)
	{
		return m_udp.recvfrom(buffer, timeout_ms);
	}

	int udp_sendto(const netaddr_any& dst_addr, const const_buffer& buffer, int timeout_ms = -1)
	{
		return m_udp.sendto(dst_addr, buffer, timeout_ms);
	}

	socket::udp& udp_sock() { return m_udp; }

	quiche_conn* conn() {
		return m_conn;
	}

	bool is_closing() const { return m_closing; }

public:
	SOCKET                   id() const final { return m_udp.id(); }
	bool                     supports_statistics() const final { return false; }

private:
	void raise_exception(const string&& place) const;
	void raise_exception(const string&& place, const string&& reason) const;

private:
	socket::udp  m_udp;
	quiche_conn* m_conn;
	quiche_config* m_quic_config;
	std::future<void>   m_rcvth;
	std::future<void>   m_sndth;
	std::atomic_bool m_closing = false;
};

} // namespace socket
} // namespace xtransmit
