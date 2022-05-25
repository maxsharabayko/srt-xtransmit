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


socket::quic::quic(const UriParser &src_uri)
	: m_udp(src_uri)
{
	m_quic_config = quiche_config_new(QUICHE_PROTOCOL_VERSION);
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
	quiche_conn_free(m_conn);
	quiche_config_free(m_quic_config);
}


static void th_receive(socket::quic* self)
{
	array<uint8_t, 1500> buffer;
	static bool req_sent = false;

	while (true)
	{
		const auto recv_res = self->recvfrom(mutable_buffer(buffer.data(), buffer.size()), -1);
		const size_t read_len = recv_res.first;

		// bites read
		if (read_len == 0)
		{
			spdlog::debug(LOG_SOCK_QUIC "udp::read() returned 0 bytes (spurious read ready?). Retrying.");
			continue;
		}

		quiche_recv_info recv_info = {
			(struct sockaddr*) recv_res.second.get(),
			recv_res.second.size()
		};

		const ssize_t done = quiche_conn_recv(self->conn(), buffer.data(), read_len, &recv_info);
		if (done < 0) {
			spdlog::error(LOG_SOCK_QUIC "failed to process packet");
			continue;
		}

		if (quiche_conn_is_closed(self->conn()))
		{
			spdlog::warn(LOG_SOCK_QUIC "connection closedt");
			return;
		}
	}


	if (quiche_conn_is_established(self->conn()) && !req_sent)
	{
		const uint8_t* app_proto;
		size_t app_proto_len;

		quiche_conn_application_proto(self->conn(), &app_proto, &app_proto_len);

		fprintf(stderr, "connection established: %.*s\n",
			(int)app_proto_len, app_proto);

		const static uint8_t r[] = "GET /index.html\r\n";
		if (quiche_conn_stream_send(self->conn(), 4, r, sizeof(r), true) < 0) {
			fprintf(stderr, "failed to send HTTP request\n");
			return;
		}

		fprintf(stderr, "sent HTTP request\n");

		req_sent = true;
	}

}


void socket::quic::listen()
{
	//struct connections c;
	//c.sock = sock;
	//c.h = NULL;

	//if (m_rcvth.valid())
	//	return;
	//spdlog::trace(LOG_SOCK_QUIC "listening.");
	//m_rcvth = ::async(::launch::async, th_receive, &m_ctx, m_udp.id(), ref(m_closing), this);

}

shared_quic socket::quic::accept()
{
	raise_exception("accept not implemented");
	return shared_from_this();
}

void socket::quic::raise_exception(const string &&place) const
{
	const int    udt_result = srt_getlasterror(nullptr);
	const string message = srt_getlasterror_str();
	spdlog::debug(LOG_SOCK_QUIC "0x{:X} {} ERROR {} {}", id(), place, udt_result, message);
	throw socket::exception(place + ": " + message);
}

void socket::quic::raise_exception(const string &&place, const string &&reason) const
{
	spdlog::debug(LOG_SOCK_QUIC "0x{:X} {}. ERROR: {}.", id(), place, reason);
	throw socket::exception(place + ": " + reason);
}

namespace detail {
	int generate_socket_id()
	{
		random_device              rd;                // Will be used to obtain a seed for the random number engine
		mt19937                    gen(rd());         // Standard mersenne_twister_engine seeded with rd()
		uniform_int_distribution<> dis(1, INT32_MAX); // Same distribution as before, but explicit and without bias
		return dis(gen);
	}
}

shared_quic socket::quic::connect()
{
	int socketid = detail::generate_socket_id();
	m_conn = quiche_connect(m_udp.host().c_str(), (const uint8_t*) &socketid, sizeof(socketid),
		m_udp.dst_addr().get(), m_udp.dst_addr().size(),
		m_quic_config);

	m_rcvth = ::async(::launch::async, th_receive, this);

	return shared_from_this();
}

size_t socket::quic::read(const mutable_buffer &buffer, int timeout_ms)
{
	if (!quiche_conn_is_established(conn()))
		raise_exception("not connected");

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
		raise_exception("not connected");

	//const uint8_t* app_proto;
	//size_t app_proto_len;

	//quiche_conn_application_proto(conn(), &app_proto, &app_proto_len);

	//fprintf(stderr, "connection established: %.*s\n",
	//	(int)app_proto_len, app_proto);

	//const static uint8_t r[] = "GET /index.html\r\n";
	//if (quiche_conn_stream_send(conn_io->conn, 4, r, sizeof(r), true) < 0) {
	//	fprintf(stderr, "failed to send HTTP request\n");
	//	return;
	//}

	//fprintf(stderr, "sent HTTP request\n");

	return 0;
}


