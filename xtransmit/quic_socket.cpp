#include "quic_socket.hpp"
#include "apputil.hpp"
#include "socketoptions.hpp"

// submodules
#include "spdlog/spdlog.h"

#include "quicly.h"
#include "quicly/defaults.h"
#include "quicly/streambuf.h"
#include "../deps/picotls/t/util.h" // setup_session_cache
#include "../deps/picotls/include/picotls/openssl.h"

using namespace std;
using namespace xtransmit;
using shared_quic = shared_ptr<socket::quic>;

#define LOG_SOCK_QUIC "SOCKET::QUIC "

struct st_stream_data_t {
	quicly_streambuf_t streambuf;
	FILE* outfp;
};

static const char* session_file = NULL;

static struct {
	ptls_aead_context_t* enc, * dec;
} address_token_aead;

static void on_stop_sending(quicly_stream_t* stream, int err)
{
	assert(QUICLY_ERROR_IS_QUIC_APPLICATION(err));
	fprintf(stderr, "received STOP_SENDING: %" PRIu16 "\n", QUICLY_ERROR_GET_ERROR_CODE(err));
}

static void on_receive_reset(quicly_stream_t* stream, int err)
{
	assert(QUICLY_ERROR_IS_QUIC_APPLICATION(err));
	fprintf(stderr, "received RESET_STREAM: %" PRIu16 "\n", QUICLY_ERROR_GET_ERROR_CODE(err));
}


static void server_on_receive(quicly_stream_t* stream, size_t off, const void* src, size_t len)
{
	fprintf(stderr, "server_on_receive \n");
	return;
//	char* path;
//	int is_http1;
//
//	if (!quicly_sendstate_is_open(&stream->sendstate))
//		return;
//
//	if (quicly_streambuf_ingress_receive(stream, off, src, len) != 0)
//		return;
//
//	if (!parse_request(quicly_streambuf_ingress_get(stream), &path, &is_http1)) {
//		if (!quicly_recvstate_transfer_complete(&stream->recvstate))
//			return;
//		/* failed to parse request */
//		send_header(stream, 1, 500, "text/plain; charset=utf-8");
//		send_str(stream, "failed to parse HTTP request\n");
//		goto Sent;
//	}
//	if (!quicly_recvstate_transfer_complete(&stream->recvstate))
//		quicly_request_stop(stream, QUICLY_ERROR_FROM_APPLICATION_ERROR_CODE(0));
//
//	if (strcmp(path, "/logo.jpg") == 0 && send_file(stream, is_http1, "assets/logo.jpg", "image/jpeg"))
//		goto Sent;
//	if (strcmp(path, "/main.jpg") == 0 && send_file(stream, is_http1, "assets/main.jpg", "image/jpeg"))
//		goto Sent;
//	if (send_sized_text(stream, path, is_http1))
//		goto Sent;
//	if (validate_path(path) && send_file(stream, is_http1, path + 1, "text/plain"))
//		goto Sent;
//
//	send_header(stream, is_http1, 404, "text/plain; charset=utf-8");
//	send_str(stream, "not found\n");
//Sent:
//	quicly_streambuf_egress_shutdown(stream);
//	quicly_streambuf_ingress_shift(stream, len);
}

static void client_on_receive(quicly_stream_t* stream, size_t off, const void* src, size_t len);

static void client_on_receive(quicly_stream_t* stream, size_t off, const void* src, size_t len)
{
	fprintf(stderr, "client_on_receive \n");
	return;
}

static const quicly_stream_callbacks_t server_stream_callbacks = { quicly_streambuf_destroy,
																  quicly_streambuf_egress_shift,
																  quicly_streambuf_egress_emit,
																  on_stop_sending,
																  server_on_receive,
																  on_receive_reset },
	client_stream_callbacks = { quicly_streambuf_destroy,
							   quicly_streambuf_egress_shift,
							   quicly_streambuf_egress_emit,
							   on_stop_sending,
							   client_on_receive,
							   on_receive_reset };

static int on_stream_open(quicly_stream_open_t* self, quicly_stream_t* stream)
{
	int ret;

	if ((ret = quicly_streambuf_create(stream, sizeof(st_stream_data_t))) != 0)
		return ret;
	//stream->callbacks = ctx.tls->certificates.count != 0 ? &server_stream_callbacks : &client_stream_callbacks;
	stream->callbacks = &server_stream_callbacks;
	return 0;
}

static quicly_stream_open_t stream_open = { &on_stream_open };

static void on_closed_by_remote(quicly_closed_by_remote_t* self, quicly_conn_t* conn, int err, uint64_t frame_type,
	const char* reason, size_t reason_len)
{
	if (QUICLY_ERROR_IS_QUIC_TRANSPORT(err)) {
		fprintf(stderr, "transport close:code=0x%" PRIx16 ";frame=%" PRIu64 ";reason=%.*s\n", QUICLY_ERROR_GET_ERROR_CODE(err),
			frame_type, (int)reason_len, reason);
	}
	else if (QUICLY_ERROR_IS_QUIC_APPLICATION(err)) {
		fprintf(stderr, "application close:code=0x%" PRIx16 ";reason=%.*s\n", QUICLY_ERROR_GET_ERROR_CODE(err), (int)reason_len,
			reason);
	}
	else if (err == QUICLY_ERROR_RECEIVED_STATELESS_RESET) {
		fprintf(stderr, "stateless reset\n");
	}
	else if (err == QUICLY_ERROR_NO_COMPATIBLE_VERSION) {
		fprintf(stderr, "no compatible version\n");
	}
	else {
		fprintf(stderr, "unexpected close:code=%d\n", err);
	}
}

static quicly_closed_by_remote_t closed_by_remote = { &on_closed_by_remote };

static int on_generate_resumption_token(quicly_generate_resumption_token_t* self, quicly_conn_t* conn, ptls_buffer_t* buf,
	quicly_address_token_plaintext_t* token)
{
	spdlog::info("quic::on_generate_resumption_token not implemented.");
	return 0;
	//return quicly_encrypt_address_token(tlsctx.random_bytes, address_token_aead.enc, buf, buf->off, token);
}

static struct {
	ptls_iovec_t tls_ticket;
	ptls_iovec_t address_token;
} session_info;

int save_session(const quicly_transport_parameters_t* transport_params)
{
	ptls_buffer_t buf;
	FILE* fp = NULL;
	int ret;

	if (session_file == NULL)
		return 0;

	char bufnochar[] = "";
	ptls_buffer_init(&buf, bufnochar, 0);

	/* build data (session ticket and transport parameters) */
	ptls_buffer_push_block(&buf, 2, { ptls_buffer_pushv(&buf, session_info.address_token.base, session_info.address_token.len); });
	ptls_buffer_push_block(&buf, 2, { ptls_buffer_pushv(&buf, session_info.tls_ticket.base, session_info.tls_ticket.len); });
	ptls_buffer_push_block(&buf, 2, {
		if ((ret = quicly_encode_transport_parameter_list(&buf, transport_params, NULL, NULL, NULL, NULL, 0)) != 0)
			goto Exit;
		});

	/* write file */
	if ((fp = fopen(session_file, "wb")) == NULL) {
		fprintf(stderr, "failed to open file:%s:%s\n", session_file, strerror(errno));
		ret = PTLS_ERROR_LIBRARY;
		goto Exit;
	}
	fwrite(buf.base, 1, buf.off, fp);

	ret = 0;
Exit:
	if (fp != NULL)
		fclose(fp);
	ptls_buffer_dispose(&buf);
	return 0;
}

static quicly_generate_resumption_token_t generate_resumption_token = { &on_generate_resumption_token };

static int save_resumption_token_cb(quicly_save_resumption_token_t* _self, quicly_conn_t* conn, ptls_iovec_t token)
{
	free(session_info.address_token.base);
	session_info.address_token = ptls_iovec_init(malloc(token.len), token.len);
	memcpy(session_info.address_token.base, token.base, token.len);

	return save_session(quicly_get_remote_transport_parameters(conn));
}

static quicly_save_resumption_token_t save_resumption_token = { save_resumption_token_cb };

static ptls_key_exchange_algorithm_t* key_exchanges[128];
static ptls_cipher_suite_t* cipher_suites[128];

int save_session_ticket_cb(ptls_save_ticket_t* _self, ptls_t* tls, ptls_iovec_t src)
{
	free(session_info.tls_ticket.base);
	session_info.tls_ticket = ptls_iovec_init(malloc(src.len), src.len);
	memcpy(session_info.tls_ticket.base, src.base, src.len);

	quicly_conn_t* conn = (quicly_conn_t*) *ptls_get_data_ptr(tls);
	return save_session(quicly_get_remote_transport_parameters(conn));
}
static ptls_save_ticket_t save_session_ticket = { save_session_ticket_cb };

static struct {
	ptls_iovec_t list[16];
	size_t count;
} negotiated_protocols;

static int on_client_hello_cb(ptls_on_client_hello_t* _self, ptls_t* tls, ptls_on_client_hello_parameters_t* params)
{
	int ret;

	if (negotiated_protocols.count != 0) {
		size_t i, j;
		const ptls_iovec_t* x, * y;
		for (i = 0; i != negotiated_protocols.count; ++i) {
			x = negotiated_protocols.list + i;
			for (j = 0; j != params->negotiated_protocols.count; ++j) {
				y = params->negotiated_protocols.list + j;
				if (x->len == y->len && memcmp(x->base, y->base, x->len) == 0)
					goto ALPN_Found;
			}
		}
		return PTLS_ALERT_NO_APPLICATION_PROTOCOL;
	ALPN_Found:
		if ((ret = ptls_set_negotiated_protocol(tls, (const char*)x->base, x->len)) != 0)
			return ret;
	}

	return 0;
}

static ptls_on_client_hello_t on_client_hello = { on_client_hello_cb };

static void on_receive_datagram_frame(quicly_receive_datagram_frame_t* self, quicly_conn_t* conn, ptls_iovec_t payload)
{
	printf("DATAGRAM: %.*s\n", (int)payload.len, payload.base);
	/* send responds with a datagram frame */
	if (!quicly_is_client(conn))
		quicly_send_datagram_frames(conn, &payload, 1);
}

socket::quic::quic(const UriParser& src_uri)
	: m_udp(src_uri)
	, m_tlsctx()
{
	m_tlsctx.random_bytes = ptls_openssl_random_bytes;
	m_tlsctx.get_time = &ptls_get_time;
	m_tlsctx.key_exchanges = key_exchanges;
	m_tlsctx.cipher_suites = cipher_suites;
	m_tlsctx.require_dhe_on_psk = 1;
	m_tlsctx.save_ticket = &save_session_ticket;
	m_tlsctx.on_client_hello = &on_client_hello;

	m_ctx = quicly_spec_context;
	m_ctx.tls = &m_tlsctx;
	m_ctx.stream_open = &stream_open;
	m_ctx.closed_by_remote = &closed_by_remote;
	m_ctx.save_resumption_token = &save_resumption_token;
	m_ctx.generate_resumption_token = &generate_resumption_token;

	key_exchanges[0] = &ptls_openssl_secp256r1;

	setup_session_cache(m_ctx.tls);
	quicly_amend_ptls_context(m_ctx.tls);

	{
		uint8_t secret[PTLS_MAX_DIGEST_SIZE];
		m_ctx.tls->random_bytes(secret, ptls_openssl_sha256.digest_size);
		address_token_aead.enc = ptls_aead_new(&ptls_openssl_aes128gcm, &ptls_openssl_sha256, 1, secret, "");
		address_token_aead.dec = ptls_aead_new(&ptls_openssl_aes128gcm, &ptls_openssl_sha256, 0, secret, "");
	}

	// Amend cipher-suites. Copy the defaults when `-y` option is not used. Otherwise, complain if aes128gcmsha256 is not specified.
	size_t i;
	for (i = 0; ptls_openssl_cipher_suites[i] != NULL; ++i) {
		cipher_suites[i] = ptls_openssl_cipher_suites[i];
	}

	// Send datagram frame.
	static quicly_receive_datagram_frame_t cb = { on_receive_datagram_frame };
	m_ctx.receive_datagram_frame = &cb;
	m_ctx.transport_params.max_datagram_frame_size = m_ctx.transport_params.max_udp_payload_size;
}

socket::quic::~quic() { }

static ptls_handshake_properties_t hs_properties;
static quicly_cid_plaintext_t next_cid;
static ptls_iovec_t resumption_token;
static quicly_transport_parameters_t resumed_transport_params;

/**
 * list of requests to be processed, terminated by reqs[N].path == NULL
 */
struct {
	const char* path;
	int to_file;
} *reqs = {};

static void send_str(quicly_stream_t* stream, const char* s)
{
	fprintf(stderr, "send_str \n");
	quicly_streambuf_egress_write(stream, s, strlen(s));
}

static int64_t enqueue_requests_at = 0, request_interval = 0;

static void enqueue_requests(quicly_conn_t* conn)
{
	if (reqs == nullptr)
		return;

	size_t i;
	int ret;

	for (i = 0; reqs[i].path != NULL; ++i) {
		char req[1024], destfile[1024];
		quicly_stream_t* stream;
		ret = quicly_open_stream(conn, &stream, 0);
		assert(ret == 0);
		sprintf(req, "GET %s\r\n", reqs[i].path);
		send_str(stream, req);
		quicly_streambuf_egress_shutdown(stream);

		fprintf(stderr, "enqueue_requests \n");

		//if (reqs[i].to_file && !suppress_output) {
		//	struct st_stream_data_t* stream_data = stream->data;
		//	sprintf(destfile, "%s.downloaded", strrchr(reqs[i].path, '/') + 1);
		//	stream_data->outfp = fopen(destfile, "w");
		//	if (stream_data->outfp == NULL) {
		//		fprintf(stderr, "failed to open destination file:%s:%s\n", reqs[i].path, strerror(errno));
		//		exit(1);
		//	}
		//}
	}
	enqueue_requests_at = INT64_MAX;
}

static void send_packets_default(int fd, struct sockaddr* dest, struct iovec* packets, size_t num_packets)
{
	for (size_t i = 0; i != num_packets; ++i) {
		struct msghdr mess;
		memset(&mess, 0, sizeof(mess));
		mess.msg_name = dest;
		mess.msg_namelen = quicly_get_socklen(dest);
		mess.msg_iov = &packets[i];
		mess.msg_iovlen = 1;
		int ret;
		while ((ret = (int)sendmsg(fd, &mess, 0)) == -1 && errno == EINTR)
			;
		if (ret == -1)
			perror("sendmsg failed");
		else
			fprintf(stderr, "send_packets_default \n");
	}
}

#define MAX_BURST_PACKETS 10

static int send_pending(int fd, quicly_conn_t* conn)
{
	quicly_address_t dest, src;
	struct iovec packets[MAX_BURST_PACKETS];
	uint8_t buf[MAX_BURST_PACKETS * quicly_get_context(conn)->transport_params.max_udp_payload_size];
	size_t num_packets = MAX_BURST_PACKETS;
	int ret;

	if ((ret = quicly_send(conn, &dest, &src, packets, &num_packets, buf, sizeof(buf))) == 0 && num_packets != 0)
		send_packets_default(fd, &dest.sa, packets, num_packets);
	else
		fprintf(stderr, "send_pending error %d\n", ret);

	return ret;
}

static void th_receive(quicly_conn_t* conn, quicly_context_t* ctx, int fd)
{
	struct sockaddr_in local;
	int ret;

	while (1) {
		fd_set readfds;
		struct timeval* tv, tvbuf;
		do {
			int64_t timeout_at = conn != NULL ? quicly_get_first_timeout(conn) : INT64_MAX;
			if (enqueue_requests_at < timeout_at)
				timeout_at = enqueue_requests_at;
			if (timeout_at != INT64_MAX) {
				quicly_context_t* ctx = quicly_get_context(conn);
				int64_t delta = timeout_at - ctx->now->cb(ctx->now);
				if (delta > 0) {
					tvbuf.tv_sec = delta / 1000;
					tvbuf.tv_usec = (delta % 1000) * 1000;
				}
				else {
					tvbuf.tv_sec = 0;
					tvbuf.tv_usec = 0;
				}
				tv = &tvbuf;
			}
			else {
				tv = NULL;
			}
			FD_ZERO(&readfds);
			FD_SET(fd, &readfds);
		} while (select(fd + 1, &readfds, NULL, NULL, tv) == -1 && errno == EINTR);
		if (enqueue_requests_at <= ctx->now->cb(ctx->now))
			enqueue_requests(conn);
		if (FD_ISSET(fd, &readfds)) {
			while (1) {
				uint8_t buf[1500];
				struct msghdr mess;
				struct sockaddr sa;
				struct iovec vec;
				memset(&mess, 0, sizeof(mess));
				mess.msg_name = &sa;
				mess.msg_namelen = sizeof(sa);
				vec.iov_base = buf;
				vec.iov_len = sizeof(buf);
				mess.msg_iov = &vec;
				mess.msg_iovlen = 1;
				ssize_t rret;
				while ((rret = recvmsg(fd, &mess, 0)) == -1 && errno == EINTR)
					;
				if (rret <= 0)
					break;
				size_t off = 0;
				while (off != rret) {
					quicly_decoded_packet_t packet;
					if (quicly_decode_packet(ctx, &packet, buf, rret, &off) == SIZE_MAX)
						break;
					quicly_receive(conn, NULL, &sa, &packet);
				}
			}
		}
	}
}


shared_quic socket::quic::connect()
{
	struct sockaddr_in local;

	hs_properties.client.negotiated_protocols.list = negotiated_protocols.list;
	hs_properties.client.negotiated_protocols.count = negotiated_protocols.count;
	// TODO: load session file. See load_session() call.

	sockaddr_in dst_addr = m_udp.dst_addr();
	int ret = quicly_connect(&m_conn, &m_ctx, m_udp.host(), (sockaddr*) &dst_addr, NULL, &next_cid, resumption_token, &hs_properties, &resumed_transport_params);
	assert(ret == 0);
	++next_cid.master_id;

	enqueue_requests(m_conn);
	send_pending(m_udp.id(), m_conn);

	m_rcvth = ::async(::launch::async, th_receive, m_conn, &m_ctx, m_udp.id());

	return shared_from_this();
}

size_t socket::quic::read(const mutable_buffer& buffer, int timeout_ms)
{
	const size_t udp_read = m_udp.read(buffer, timeout_ms);

	// TODO: Add QUIC decode.

	return udp_read;
}

int socket::quic::write(const const_buffer& buffer, int timeout_ms)
{
	ptls_iovec_t datagram = ptls_iovec_init(buffer.data(), buffer.size());
	quicly_send_datagram_frames(m_conn, &datagram, 1);

	int ret = send_pending(m_udp.id(), m_conn);
	if (ret != 0) {
		quicly_free(m_conn);
		m_conn = NULL;
		fprintf(stderr, "quicly_send returned %d\n", ret);
	}

	//const size_t udp_write = m_udp.write(buffer, timeout_ms);

	// TODO: Add QUIC encode.

	return buffer.size();
}
