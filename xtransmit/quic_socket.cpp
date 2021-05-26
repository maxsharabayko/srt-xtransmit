#include "quic_socket.hpp"
#include "apputil.hpp"
#include "socketoptions.hpp"

// submodules
#include "spdlog/spdlog.h"

#include "quicly/defaults.h"

using namespace std;
using namespace xtransmit;
using shared_quic = shared_ptr<socket::quic>;

extern const ptls_context_t s_tlsctx;

#define LOG_SOCK_QUIC "SOCKET::QUIC "

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
