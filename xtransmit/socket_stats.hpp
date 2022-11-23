#pragma once
#include <atomic>
#include <chrono>
#include <future>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>
#include <map>

// xtransmit
#include "socket.hpp"


namespace xtransmit
{
namespace socket
{

class stats_writer
{
public:
	stats_writer(const std::string& filename, const std::string& format, const std::chrono::milliseconds& interval);
	~stats_writer();

public:
	void add_socket(std::shared_ptr<socket::isocket> sock);
	void remove_socket(SOCKET sockid);
	void clear();
	void stop();

private:
	std::future<void> launch();

private:
	using shared_sock = std::shared_ptr<socket::isocket>;
	std::atomic<bool> m_stop;
	std::ofstream m_logfile;
	std::string m_format;
	std::map<SOCKET, shared_sock> m_sock;
	std::future<void> m_stat_future;
	const std::chrono::milliseconds m_interval;
	std::mutex m_lock;
};

} // namespace socket
} // namespace xtransmit

