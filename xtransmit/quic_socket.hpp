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
	quicly_conn_t* quic_conn(quicly_conn_t* new_conn);

	quicly_conn_t* quic_conn() const;

	// Called from quicly as a callback when there is a new datagram to read.
	// Sets m_pkt_to_read and notifies datagram consumer.
	void on_canread_datagram(ptls_iovec_t payload);


	struct resumption_token_cb
		: public quicly_generate_resumption_token_t
	{
		ptls_context_t* tls_ctx;
	};

	static int on_generate_resumption_token(quicly_generate_resumption_token_t* self, quicly_conn_t* conn, ptls_buffer_t* buf,
		quicly_address_token_plaintext_t* token);

	struct receive_datagram_cb
		: public quicly_receive_datagram_frame_t
	{
		quic* quic_socket_ptr;
	};
	
private:
	void raise_exception(const string&& reason) const;

private:
	ptls_context_t m_tlsctx;
	quicly_context_t m_ctx;
	quicly_conn_t* m_conn = nullptr;
	socket::udp    m_udp;
	std::future<void>   m_rcvth;
	std::atomic_bool m_closing;

	mutable std::mutex m_mtx_accept;
	std::condition_variable m_cv_accept;
	resumption_token_cb m_resump_token_ctx;// = { &quic::on_generate_resumption_token, m_tlsctx };

	receive_datagram_cb m_receive_datagram_cb;

	mutable std::mutex m_mtx_read;	// protects simulteneous access to m_pkt_to_read
	std::condition_variable m_cv_read;
	const_buffer m_pkt_to_read;
};

} // namespace socket
} // namespace xtransmit
