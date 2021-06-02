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
#include "udp_socket.hpp"

// SRT
#include "uriparser.hpp"

// quicly
#include "quicly.h"
#include "../deps/picotls/include/picotls.h"


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

	quic(const int sock, bool blocking);

	virtual ~quic();

public:
	std::future<shared_quic> async_connect() noexcept(false);
	std::future<shared_quic> async_accept() noexcept(false);

	bool is_caller() const final { return m_udp.is_caller(); }

	int id() const final { return m_udp.id(); }

	shared_quic connect();
	shared_quic accept();

	/**
	 * Start listening on the incomming connection requests.
	 *
	 * May throw a socket::exception.
	 */
	void listen() noexcept(false);

public:
	/// @returns The number of bytes received.
	/// @throws socket_exception Thrown on failure.
	size_t read(const mutable_buffer& buffer, int timeout_ms = -1);

	int    write(const const_buffer& buffer, int timeout_ms = -1);

	// Internally used
	void on_accept_new_conn(quicly_conn_t* new_conn);
	
private:
	void raise_exception(const string&& place) const;
	void raise_exception(const string&& place, const string&& reason) const;

private:
	ptls_context_t m_tlsctx;
	quicly_context_t m_ctx;
	quicly_conn_t* m_conn = nullptr;
	socket::udp    m_udp;
	std::future<void>   m_rcvth;
	std::atomic_bool m_closing;

	std::mutex m_mtx_accept;
	std::condition_variable m_cv_accept;
};

} // namespace socket
} // namespace xtransmit