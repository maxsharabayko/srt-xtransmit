#pragma once
#include <memory>
#include <exception>
#include <future>
#include <string>
#include <vector>
#include <list>
#include <map>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <queue>

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

	// Constructor for accepted socket connections.
	// Accepted sockets don't have receiving thread.
	quic(quic& other, quiche_conn* conn);

	virtual ~quic();

public:
	shared_quic connect();
	shared_quic accept();

	static constexpr size_t LOCAL_CONN_ID_LEN = 16;

	struct conn_io {
		quiche_conn* conn;
		netaddr_any peer_addr;
	};

	quiche_conn* create_accepted_conn(uint8_t* scid, size_t scid_len,
		uint8_t* odcid, size_t odcid_len,
		const netaddr_any& peer_addr);

	void check_pending_conns();

	void queue_accepted_conn(quiche_conn* conn);

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

	bool has_conn(const string& cid);

	quiche_conn* find_conn(const string& cid);

	enum class state
	{
		opened,
		listening,
		connecting,
		connected,
		closing, // broken or regular close
		closed
	};

	bool is_closing() const
	{
		std::lock_guard<std::mutex> lck(m_state_mtx);
		return m_state == state::closing;
	}

	bool is_listening() const
	{
		std::lock_guard<std::mutex> lck(m_state_mtx);
		return m_state == state::listening;
	}

	bool wait_state(state target_state, const std::chrono::steady_clock::duration& timeout)
	{
		std::unique_lock<std::mutex> lck(m_state_mtx);

		if (target_state == m_state)
			return true;

		m_state_cv.wait_for(lck, timeout);
		return target_state == m_state;
	}

	state get_state() const
	{
		std::lock_guard<std::mutex> lck(m_state_mtx);
		return m_state;
	}

	void change_state(state new_state)
	{
		std::lock_guard<std::mutex> lck(m_state_mtx);
		if (new_state == m_state)
			return;

		m_state = new_state;
		m_state_cv.notify_all();
	}

	void wait_udp_send(const std::chrono::steady_clock::duration& timeout)
	{
		std::unique_lock<std::mutex> lck(m_rw_mtx);
		m_quic_write.wait_for(lck, timeout);
	}

	const quiche_config* config() const { return m_quic_config; }

public:
	SOCKET                   id() const final { return m_udp.id(); }
	bool                     supports_statistics() const final { return false; }

private:
	void raise_exception(const string&& place) const;
	void raise_exception(const string&& place, const string&& reason) const;

private:
	socket::udp  m_udp;
	shared_quic  m_parent; // An accepted socket as to keep the reference to the parent listener to maintain the UDP receiving thread.
	quiche_conn* m_conn;

	mutable std::mutex m_conn_mtx;
	std::condition_variable m_conn_cv;
	std::unordered_map<std::string, conn_io> m_accepted_conns; // In the case of server.
	std::list<std::shared_ptr<quic>> m_pending_connections; // Connections pending to be queued to be accepted by the app.
	std::queue<std::shared_ptr<quic>> m_queued_connections; // Accepted connections queued to be accepted by the app.
	quiche_config* m_quic_config;
	std::shared_future<void> m_rcvth;
	std::future<void>   m_th_timeout;

	std::string m_tls_logpath;
	std::condition_variable m_state_cv;
	mutable std::mutex m_state_mtx;
	state m_state;

	mutable std::mutex m_rw_mtx;
	std::condition_variable m_conn_read; // There is something to read from this socket.
	std::condition_variable m_quic_write; // New data has been submitted. Wake up the UDP sending thread.

};

} // namespace socket
} // namespace xtransmit
