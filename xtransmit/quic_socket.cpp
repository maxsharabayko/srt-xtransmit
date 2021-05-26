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

extern const ptls_context_t s_tlsctx;

#define LOG_SOCK_QUIC "SOCKET::QUIC "

struct st_stream_data_t {
	quicly_streambuf_t streambuf;
	FILE* outfp;
};

static const char* session_file = NULL;

static struct {
	ptls_aead_context_t* enc, * dec;
} address_token_aead;

static void on_stop_sending(quicly_stream_t* stream, int err);
static void on_receive_reset(quicly_stream_t* stream, int err);
static void server_on_receive(quicly_stream_t* stream, size_t off, const void* src, size_t len);
static void client_on_receive(quicly_stream_t* stream, size_t off, const void* src, size_t len);

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

	ptls_buffer_init(&buf, "", 0);

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

socket::quic::quic(const UriParser& src_uri)
	: udp(src_uri)
	, tlsctx(s_tlsctx)
{
	ctx = quicly_spec_context;
	ctx.tls = &tlsctx;
	ctx.stream_open = &stream_open;
	ctx.closed_by_remote = &closed_by_remote;
	ctx.save_resumption_token = &save_resumption_token;
	ctx.generate_resumption_token = &generate_resumption_token;

	setup_session_cache(ctx.tls);
	quicly_amend_ptls_context(ctx.tls);

	{
		uint8_t secret[PTLS_MAX_DIGEST_SIZE];
		ctx.tls->random_bytes(secret, ptls_openssl_sha256.digest_size);
		address_token_aead.enc = ptls_aead_new(&ptls_openssl_aes128gcm, &ptls_openssl_sha256, 1, secret, "");
		address_token_aead.dec = ptls_aead_new(&ptls_openssl_aes128gcm, &ptls_openssl_sha256, 0, secret, "");
	}
}

socket::quic::~quic() { }

size_t socket::quic::read(const mutable_buffer& buffer, int timeout_ms)
{
	const size_t udp_read = udp::read(buffer, timeout_ms);

	// TODO: Add QUIC decode.

	return udp_read;
}

int socket::quic::write(const const_buffer& buffer, int timeout_ms)
{
	const size_t udp_write = udp::write(buffer, timeout_ms);

	// TODO: Add QUIC encode.

	return udp_write;
}
