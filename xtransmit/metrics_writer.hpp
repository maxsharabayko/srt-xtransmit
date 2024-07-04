#pragma once
#include <atomic>
#include <chrono>
#include <future>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>
#include <map>

#include "metrics.hpp"
#include "socket.hpp"


namespace xtransmit
{
namespace metrics
{

class metrics_writer
{
public:
	metrics_writer(const std::string& filename, const std::chrono::milliseconds& interval);
	~metrics_writer();

public:
	using shared_sock = std::shared_ptr<socket::isocket>;
	using shared_validator = std::shared_ptr<validator>;
	void add_validator(shared_validator v, SOCKET id);
	void remove_validator(SOCKET id);
	void clear();
	void stop();

private:
	std::future<void> launch();

	std::atomic<bool> m_stop_token;
	std::ofstream m_file;
	std::map<SOCKET, shared_validator> m_validators;
	std::future<void> m_metrics_future;
	const std::chrono::milliseconds m_interval;
	std::mutex m_lock;
};

} // namespace socket
} // namespace xtransmit

