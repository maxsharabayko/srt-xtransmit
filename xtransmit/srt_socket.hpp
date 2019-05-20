#pragma once
#include <memory>
#include <exception>
#include <future>
#include <string>
#include <vector>
#include <map>

// OpenSRT
#include "srt.h"
#include "udt.h"
#include "uriparser.hpp"



namespace xtransmit {
namespace srt {


class socket_exception : public std::exception
{

public:

	socket_exception(const std::string&& err)
		: m_error_msg(err)
	{

	}

public:

	virtual const char* what() const throw()
	{
		return m_error_msg.c_str();
	}

private:

	const std::string m_error_msg;
};


class socket
	: public std::enable_shared_from_this<socket>
{
	using string			= std::string;
	using shared_socket		= std::shared_ptr<socket>;

public:

	socket(const UriParser& src_uri);

	~socket();

	socket(const int sock, bool blocking);

private:


public:

	std::future<shared_socket>	async_connect();
	std::future<shared_socket>	async_accept();

	shared_socket				connect();
	shared_socket				accept();
	void						listen();

public:

	void configure(const std::map<string, string> &options);

	int configure_pre(SRTSOCKET sock);
	int configure_post(SRTSOCKET sock);

public:

	std::future<shared_socket>  async_read(std::vector<char> &buffer);
	void async_write();

	/**
	 * @returns The number of bytes received.
	 *
	 * @throws socket_exception Thrown on failure.
	 */
	size_t read(std::vector<char>& buffer, int timeout_ms = -1);
	void write(const std::vector<char>& buffer, int timeout_ms = -1);


public:

	int id() const { return m_bind_socket; }
	int statistics(SRT_TRACEBSTATS &stats);


	const std::string statistics_csv(bool print_header);


private:

	static void raise_exception(UDT::ERRORINFO& udt_error, const string&& src);

private:

	int m_bind_socket	= SRT_INVALID_SOCK;
	int m_epoll_connect	= -1;
	int m_epoll_io		= -1;

	bool m_blocking_mode = false;
	string m_host;
	int m_port;
	std::map<string, string> m_options; // All other options, as provided in the URI


};

} // namespace srt
} // namespace xtransmit

