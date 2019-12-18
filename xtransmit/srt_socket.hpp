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

// OpenSRT
#include "srt.h"
#include "udt.h"
#include "uriparser.hpp"



namespace xtransmit {
namespace socket {


class srt
	: public std::enable_shared_from_this<srt>
	, public isocket
{
	using string		= std::string;
	using shared_srt	= std::shared_ptr<srt>;

public:
	explicit srt(const UriParser& src_uri);

	srt(const int sock, bool blocking);

	virtual ~srt();


public:
	std::future<shared_srt> async_connect() noexcept(false);
	std::future<shared_srt> async_accept()  noexcept(false);


	shared_srt				connect();
	shared_srt				accept();

	/**
	 * Start listening on the incomming connection requests.
	 *
	 * May throw a socket_exception.
	 */
	void					listen() noexcept(false);

public:
	void configure(const std::map<string, string> &options);

	int configure_pre(SRTSOCKET sock);
	int configure_post(SRTSOCKET sock);

public:
	std::future<shared_srt>  async_read(std::vector<char> &buffer);
	void async_write();

	/**
	 * @returns The number of bytes received.
	 *
	 * @throws socket_exception Thrown on failure.
	 */
	size_t read(const mutable_buffer& buffer, int timeout_ms = -1) final;
	int   write(const const_buffer&   buffer, int timeout_ms = -1) final;

	enum connection_mode
	{
		FAILURE    =-1,
		LISTENER   = 0,
		CALLER     = 1,
		RENDEZVOUS = 2
	};

	connection_mode mode() const;

	bool is_caller() const final { return m_mode == CALLER; }

public:
	int id() const final { return m_bind_socket; }
	int statistics(SRT_TRACEBSTATS &stats);

	const std::string statistics_csv(bool print_header) final;


private:
	static void raise_exception(const string&& place, UDT::ERRORINFO& udt_error);
	static void raise_exception(const string &&place, const string&& reason);

private:
	int m_bind_socket	= SRT_INVALID_SOCK;
	int m_epoll_connect	= -1;
	int m_epoll_io		= -1;

	connection_mode m_mode = FAILURE;
	bool m_blocking_mode = false;
	string m_host;
	int m_port = -1;
	std::map<string, string> m_options; // All other options, as provided in the URI
};

} // namespace socket
} // namespace xtransmit

