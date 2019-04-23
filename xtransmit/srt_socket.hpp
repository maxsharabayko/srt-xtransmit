#pragma once
#include <memory>
#include <exception>
#include <string>
#include <vector>


#include "continuable/continuable.hpp"



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

public:

	socket();

	//~socket();

public:

	void listen();
	std::future<std::shared_ptr<xtransmit::srt::socket>> async_connect();
	//void async_accept();

public:

	void async_read(std::vector<char> &buffer);
	void async_write();


private:

	int m_bindsocket;
	std::string m_host;
	int m_port;

};

} // namespace srt
} // namespace xtransmit

