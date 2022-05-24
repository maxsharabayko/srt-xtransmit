#include <thread>
#include <iostream>
#include <iterator> // std::ostream_iterator

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


socket::quic::quic(const UriParser &src_uri)
	: m_udp(src_uri)
{
}

socket::quic::~quic()
{
}

void socket::quic::listen()
{
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

shared_quic socket::quic::connect()
{
	raise_exception("connect not implemented");

	return shared_from_this();
}

size_t socket::quic::read(const mutable_buffer &buffer, int timeout_ms)
{
	raise_exception("read not implemented");

	return 0;
}

int socket::quic::write(const const_buffer &buffer, int timeout_ms)
{
	raise_exception("write not implemented");

	return 0;
}


