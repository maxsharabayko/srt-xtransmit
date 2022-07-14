#include <array>
#include <thread>
#include <iostream>
#include <iterator> // std::ostream_iterator
#include <random>

// submodules
#include "spdlog/spdlog.h"

// xtransmit
#include "quic_socket.hpp"
#include "misc.hpp"

// srt utils
#include "verbose.hpp"
#include "socketoptions.hpp"
#include "apputil.hpp"

using namespace std;
using namespace xtransmit;
using shared_quic = shared_ptr<socket::quic>;


#define LOG_SOCK_QUIC "SOCKET::QUIC "

constexpr size_t MAX_DATAGRAM_SIZE = 1350;
constexpr size_t MAX_TOKEN_LEN = sizeof("quiche") - 1 + sizeof(struct sockaddr_storage) + QUICHE_MAX_CONN_ID_LEN;


socket::quic::quic(const UriParser &src_uri)
	: m_udp(src_uri)
{
	m_quic_config = quiche_config_new(is_caller() ? 0xbabababa : QUICHE_PROTOCOL_VERSION);
	if (m_quic_config == NULL) {
		raise_exception("failed to create config");
	}

	quiche_config_set_application_protos(m_quic_config,
		(uint8_t*)"\x0ahq-interop\x05hq-29\x05hq-28\x05hq-27\x08http/0.9", 38);

	// TODO: Enable setting these via URI query.
	quiche_config_set_max_idle_timeout(m_quic_config, 5000);
	quiche_config_set_max_recv_udp_payload_size(m_quic_config, MAX_DATAGRAM_SIZE);
	quiche_config_set_max_send_udp_payload_size(m_quic_config, MAX_DATAGRAM_SIZE);
	quiche_config_set_initial_max_data(m_quic_config, 10000000);
	quiche_config_set_initial_max_stream_data_bidi_local(m_quic_config, 1000000);
	quiche_config_set_initial_max_stream_data_uni(m_quic_config, 1000000);
	quiche_config_set_initial_max_streams_bidi(m_quic_config, 100);
	quiche_config_set_initial_max_streams_uni(m_quic_config, 100);
	quiche_config_set_disable_active_migration(m_quic_config, true);

	quiche_config_set_cc_algorithm(m_quic_config, QUICHE_CC_RENO);

	const char* tlskeyopt = "tlskey";
	const char* tlscertopt = "tlscert";
	const char* tlskeylog = "tlskeylog";

	if (src_uri.parameters().count(tlskeyopt))
	{
		const string keyfile = src_uri.parameters().at(tlskeyopt);
		quiche_config_load_cert_chain_from_pem_file(m_quic_config, keyfile.c_str());
	}

	if (src_uri.parameters().count(tlscertopt))
	{
		const string certfile = src_uri.parameters().at(tlscertopt);
		quiche_config_load_priv_key_from_pem_file(m_quic_config, certfile.c_str());
	}

	if (src_uri.parameters().count(tlskeylog))
	{
		//const string logfile = src_uri.parameters().at(tlskeylog);
		quiche_config_log_keys(m_quic_config);
	}
}

socket::quic::~quic()
{
	change_state(state::closing);
	m_rcvth.wait();
	m_sndth.wait();
	quiche_conn_free(m_conn);
	while (!m_queued_connections.empty())
	{
		quiche_conn* conn = m_queued_connections.front();
		quiche_conn_free(conn);
		m_queued_connections.pop();
	}

	quiche_config_free(m_quic_config);

	change_state(state::closed);
}

namespace detail {
	void mint_token(const uint8_t* dcid, size_t dcid_len,
		const netaddr_any& addr,
		uint8_t* token, size_t* token_len) {
		memcpy(token, "quiche", sizeof("quiche") - 1);
		memcpy(token + sizeof("quiche") - 1, addr.get(), addr.size());
		memcpy(token + sizeof("quiche") - 1 + addr.size(), dcid, dcid_len);

		*token_len = sizeof("quiche") - 1 + addr.size() + dcid_len;
	}

	bool validate_token(const uint8_t* token, size_t token_len,
		const netaddr_any& addr, uint8_t* odcid, size_t* odcid_len) {
		if ((token_len < sizeof("quiche") - 1) ||
			memcmp(token, "quiche", sizeof("quiche") - 1)) {
			return false;
		}

		token += sizeof("quiche") - 1;
		token_len -= sizeof("quiche") - 1;

		if ((token_len < addr.size()) || memcmp(token, addr.get(), addr.size())) {
			return false;
		}

		token += addr.size();
		token_len -= addr.size();

		if (*odcid_len < token_len) {
			return false;
		}

		memcpy(odcid, token, token_len);
		*odcid_len = token_len;

		return true;
	}

	int generate_socket_id()
	{
		random_device              rd;                // Will be used to obtain a seed for the random number engine
		mt19937                    gen(rd());         // Standard mersenne_twister_engine seeded with rd()
		uniform_int_distribution<> dis(1, INT32_MAX); // Same distribution as before, but explicit and without bias
		return dis(gen);
	}
}


static void th_receive(socket::quic* self)
{
	array<uint8_t, 1500> buffer;

	while (!self->is_closing())
	{
		const auto recv_res = self->udp_recvfrom(mutable_buffer(buffer.data(), buffer.size()), -1);
		const size_t read_len = recv_res.first;

		// bites read
		if (read_len == 0)
		{
			spdlog::debug(LOG_SOCK_QUIC "udp::read() returned 0 bytes (spurious read ready?). Retrying.");
			continue;
		}

		const netaddr_any& peer_addr = recv_res.second;
		quiche_recv_info recv_info = {
			(struct sockaddr*) peer_addr.get(),
			recv_res.second.size()
		};

		quiche_conn* conn = nullptr;
		if (self->is_listening())
		{
			// Process possible incoming connections.
			uint8_t type;
			uint32_t version;

			uint8_t scid[QUICHE_MAX_CONN_ID_LEN];
			size_t scid_len = sizeof(scid);

			uint8_t dcid[QUICHE_MAX_CONN_ID_LEN];
			size_t dcid_len = sizeof(dcid);

			uint8_t odcid[QUICHE_MAX_CONN_ID_LEN];
			size_t odcid_len = sizeof(odcid);

			uint8_t token[MAX_TOKEN_LEN];
			size_t token_len = sizeof(token);

			int rc = quiche_header_info(buffer.data(), read_len, socket::quic::LOCAL_CONN_ID_LEN, &version,
				&type, scid, &scid_len, dcid, &dcid_len,
				token, &token_len);

			conn = self->find_conn(string((char*)scid, scid_len));

			if (conn == nullptr)
			{
				if (!quiche_version_is_supported(version))
				{
					spdlog::info(LOG_SOCK_QUIC "version negotiation");

					uint8_t out[MAX_DATAGRAM_SIZE];
					ssize_t written = quiche_negotiate_version(scid, scid_len,
						dcid, dcid_len,
						out, sizeof(out));

					if (written < 0)
					{
						spdlog::error(LOG_SOCK_QUIC "Failed to create vneg packet: {}.", written);
						continue;
					}

					ssize_t sent = self->udp_sock().sendto(peer_addr, const_buffer(out, written));
					if (sent != written) {
						spdlog::error(LOG_SOCK_QUIC "Failed to send vneg packet.");
						continue;
					}

					continue;
				}
				else
				{
					spdlog::info(LOG_SOCK_QUIC "Negotiated version {}", version);
				}

				if (token_len == 0)
				{
					spdlog::info(LOG_SOCK_QUIC "stateless retry");

					detail::mint_token(dcid, dcid_len, peer_addr, token, &token_len);

					uint8_t scid[socket::quic::LOCAL_CONN_ID_LEN];
					for (int i = 0; i < 4; ++i)
					{
						*reinterpret_cast<int*>(scid + 4 * i) = detail::generate_socket_id();
					}

					uint8_t out[MAX_DATAGRAM_SIZE];
					ssize_t written = quiche_retry(scid, scid_len,
						dcid, dcid_len,
						scid, sizeof(scid),
						token, token_len,
						version, out, sizeof(out));

					if (written < 0) {
						spdlog::error(LOG_SOCK_QUIC "Failed to create retry packet: %zd\n",
							written);
						continue;
					}

					ssize_t sent = self->udp_sock().sendto(peer_addr, const_buffer(out, written));
					if (sent != written) {
						spdlog::error(LOG_SOCK_QUIC "Failed to send retry packet.");
						continue;
					}

					continue;
				}

				if (!detail::validate_token(token, token_len, peer_addr, odcid, &odcid_len))
				{
					fprintf(stderr, "invalid address validation token\n");
					continue;
				}

				conn = self->create_accepted_conn(scid, scid_len, odcid, odcid_len, peer_addr);

				if (conn == NULL)
					continue;
			}
		}
		else
		{
			conn = self->conn();
		}

		const ssize_t done = quiche_conn_recv(conn, buffer.data(), read_len, &recv_info);
		if (done < 0) {
			spdlog::error(LOG_SOCK_QUIC "failed to process packet");
			continue;
		}

		if (quiche_conn_is_closed(conn))
		{
			spdlog::warn(LOG_SOCK_QUIC "connection closedt");
			return;
		}

		if (self->get_state() == socket::quic::state::connecting && quiche_conn_is_established(conn))
		{
			const uint8_t* app_proto;
			size_t app_proto_len;
			quiche_conn_application_proto(conn, &app_proto, &app_proto_len);
			spdlog::info(LOG_SOCK_QUIC "connection established: {:<{}}.", app_proto, app_proto_len);

			self->change_state(socket::quic::state::connected);
		}
		else if (self->get_state() == socket::quic::state::listening && quiche_conn_is_established(conn))
		{
			self->queue_accepted_conn(conn);
		}
	}
}

static void th_send(socket::quic* self)
{
	array<uint8_t, MAX_DATAGRAM_SIZE> buffer;

	quiche_send_info send_info;

	while (!self->is_closing())
	{
		while (!self->is_closing()) {

			const ssize_t written = quiche_conn_send(self->conn(), buffer.data(), buffer.size(),
				&send_info);

			if (written == QUICHE_ERR_DONE) {
				//spdlog::trace(LOG_SOCK_QUIC "(SNDTH) done waiting");
				break;
			}

			if (written < 0) {
				spdlog::error(LOG_SOCK_QUIC "Failed to create packet: {}", written);
				return;
			}

			netaddr_any dst_addr((sockaddr*)&send_info.to, send_info.to_len);
			ssize_t sent = self->udp_sock().sendto(dst_addr, const_buffer(buffer.data(), written));
			if (sent != written) {
				spdlog::error(LOG_SOCK_QUIC "failed to send");
				return;
			}

			spdlog::trace(LOG_SOCK_QUIC "send {} bytes.", sent);
		}

		const uint64_t t =quiche_conn_timeout_as_nanos(self->conn());
		spdlog::trace(LOG_SOCK_QUIC "(SNDTH) next send in {} ns", t);
		this_thread::sleep_for(chrono::nanoseconds(t));
	}
}


socket::quic::quic(quic& other, quiche_conn* conn)
	: m_udp(other.m_udp)
	, m_conn(conn)
{
	m_state = state::connected;
	m_sndth = ::async(::launch::async, th_send, this);
}

quiche_conn* socket::quic::create_accepted_conn(uint8_t*  scid,
									   size_t             scid_len,
									   uint8_t*           odcid,
									   size_t             odcid_len,
									   const netaddr_any& peer_addr)
{
	if (scid_len != LOCAL_CONN_ID_LEN)
	{
		spdlog::error(LOG_SOCK_QUIC "Failed to create a connection: SCID length too short.");
	}

	const netaddr_any local_addr = m_udp.src_addr();
	quiche_conn* conn = quiche_accept(
		scid, scid_len, odcid, odcid_len, local_addr.get(), local_addr.size(), peer_addr.get(), peer_addr.size(), m_quic_config);

	if (conn == nullptr)
	{
		spdlog::error(LOG_SOCK_QUIC "Failed to accept connection.");
		return nullptr;
	}

	conn_io new_conn_io;
	new_conn_io.peer_addr = peer_addr;
	new_conn_io.conn = conn;
	memcpy(new_conn_io.cid, scid, scid_len);

	lock_guard<mutex> lck(m_conn_mtx);
	spdlog::info(LOG_SOCK_QUIC "Accepted connection.");
	m_accepted_conns.emplace(string((char*)scid, scid_len), new_conn_io);

	m_conn_cv.notify_one();

	return conn;
}

void socket::quic::queue_accepted_conn(quiche_conn* conn)
{
	lock_guard<mutex> lck(m_conn_mtx);
	m_queued_connections.push(conn);
	m_conn_cv.notify_one();
}

bool socket::quic::has_conn(const string& cid)
{
	return m_accepted_conns.find(cid) != m_accepted_conns.end();
}

quiche_conn* socket::quic::find_conn(const string& cid)
{
	auto f = m_accepted_conns.find(cid);
	if (f == m_accepted_conns.end())
		return nullptr;

	return f->second.conn;
}

void socket::quic::listen()
{
	change_state(state::listening);

	m_rcvth = ::async(::launch::async, th_receive, this);
}

shared_quic socket::quic::accept()
{
	unique_lock<mutex> lck(m_conn_mtx);

	while (!is_closing() && m_queued_connections.empty())
	{
		m_conn_cv.wait(lck);
	}
	
	if (!is_closing())
	{
		quiche_conn* conn = m_queued_connections.front();
		m_queued_connections.pop();
		return make_shared<quic>(*this, conn);
	}

	raise_exception("accept()");
}

void socket::quic::raise_exception(const string &&place) const
{
	spdlog::debug(LOG_SOCK_QUIC "0x{:X} {} ERROR {} {}", id(), place);
	throw socket::exception(string(place));
}

void socket::quic::raise_exception(const string &&place, const string &&reason) const
{
	spdlog::debug(LOG_SOCK_QUIC "0x{:X} {}. ERROR: {}.", id(), place, reason);
	throw socket::exception(place + ": " + reason);
}

shared_quic socket::quic::connect()
{
	spdlog::warn(LOG_SOCK_QUIC "0x{:X} Forming scid.", id());
	uint8_t scid[socket::quic::LOCAL_CONN_ID_LEN];
	for (int i = 0; i < 4; ++i)
	{
		*reinterpret_cast<int*>(scid + 4 * i) = detail::generate_socket_id();
	}

	spdlog::warn(LOG_SOCK_QUIC "0x{:X} Retrieving local address.", id());
	const netaddr_any local_addr = m_udp.src_addr();

	spdlog::warn(LOG_SOCK_QUIC "0x{:X} Calling quiche_connect.", id());

	m_conn = quiche_connect(m_udp.host().c_str(), scid, sizeof(scid),
		local_addr.get(), local_addr.size(),
		m_udp.dst_addr().get(), m_udp.dst_addr().size(),
		m_quic_config);

	spdlog::warn(LOG_SOCK_QUIC "0x{:X} quiche_connect returned smth.", id());

	change_state(state::connecting);

	m_rcvth = ::async(::launch::async, th_receive, this);
	m_sndth = ::async(::launch::async, th_send, this);

	if (!wait_state(state::connected, 5s))
		raise_exception("connection timeout");

	spdlog::warn(LOG_SOCK_QUIC "0x{:X} connect state {}.", id(), get_state());

	return shared_from_this();
}

size_t socket::quic::read(const mutable_buffer &buffer, int timeout_ms)
{
	if (!quiche_conn_is_established(conn()))
		raise_exception("read", "not connected");

	uint64_t s = 0;
	quiche_stream_iter* readable = quiche_conn_readable(conn());

	while (quiche_stream_iter_next(readable, &s)) {
		spdlog::debug(LOG_SOCK_QUIC "stream {} is readable", s);

		bool fin = false;
		ssize_t recv_len = quiche_conn_stream_recv(conn(), s,
			(uint8_t*) buffer.data(), buffer.size(),
			&fin);
		if (recv_len < 0) {
			break;
		}

		//if (fin) {
		//	if (quiche_conn_close(conn(), true, 0, NULL, 0) < 0) {
		//		spdlog::error(LOG_SOCK_QUIC "failed to close connection\n");
		//	}
		//}
	}

	quiche_stream_iter_free(readable);

	return 0;
}

int socket::quic::write(const const_buffer &buffer, int timeout_ms)
{
	if (!quiche_conn_is_established(conn()))
		raise_exception("write: not connected.");

	if (quiche_conn_stream_send(conn(), 4, (uint8_t*) buffer.data(), buffer.size(), true) < 0) {
		raise_exception("write: failed to send a stream.");
	}

	return 0;
}


