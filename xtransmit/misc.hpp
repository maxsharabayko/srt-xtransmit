#pragma once
#include <chrono>
#include <string.h>
#include <iostream>
#include <iomanip>	// std::put_time
#include <memory>
#include <sstream>	// std::stringstream, std::stringbuf

// xtransmit
#include "socket.hpp"
#include "netaddr_any.hpp"
#include "srt_socket.hpp"
#include "udp_socket.hpp"


namespace xtransmit {

// Note: std::put_time is supported only in GCC 5 and higher
#if !defined(__GNUC__) || defined(__clang__) || (__GNUC__ >= 5)
#define HAS_PUT_TIME
#endif


#ifdef HAS_PUT_TIME
// Follows ISO 8601
inline std::string print_timestamp_now()
{
	using namespace std;
	using namespace std::chrono;

	const auto systime_now = system_clock::now();
	const time_t time_now = system_clock::to_time_t(systime_now);
	// Ignore the error from localtime, as zeroed tm_now is acceptable.
	tm tm_now = {};
#ifdef _WIN32
	localtime_s(&tm_now, &time_now);
#else
	localtime_r(&time_now, &tm_now);
#endif

	stringstream ss;
	ss << std::put_time(&tm_now, "%FT%T.") << std::setfill('0') << std::setw(6);

	const auto since_epoch = systime_now.time_since_epoch();
	const seconds s = duration_cast<seconds>(since_epoch);
	ss << duration_cast<microseconds>(since_epoch - s).count();
	ss << std::put_time(&tm_now, "%z");

	return ss.str();
};

#endif // HAS_PUT_TIME


struct stats_config
{
	int         stats_freq_ms = 0;
	std::string stats_file;
	std::string stats_format = "csv";
};

/// Connection establishment config
struct conn_config
{
	bool        reconnect = false; // Try to reconnect broken connections.
	int         max_conns = 1;     // SRT Caller: the number of client connections to initiate.
	                               // SRT Listener: the number of allowed clients to accept.
	int         concurrent_streams = 1;   // Maximum number of concurrent streams allowed.
	bool        close_listener = false; // Close listener after all connection have been accepted.
};


typedef std::shared_ptr<socket::isocket> shared_sock_t;

/// @brief Create SRT or UDP socket connection.
/// @param [in] uris connection URIs
/// @param [in,out] listeting_sock existing listening socket if any.
/// @return socket connection or nullptr
/// @throws socket::exception
shared_sock_t create_connection(const std::vector<UriParser>& uris, shared_sock_t& listeting_sock);

/// @brief Create SRT or UDP socket connection.
/// The same as with two arguments, but using a temporal listeting_sock variable.
/// @param [in] uris connection URIs
/// @return socket connection or nullptr
/// @throws socket::exception
inline shared_sock_t create_connection(const std::vector<UriParser>& uris)
{
	shared_sock_t temp_sock;
	return create_connection(uris, temp_sock);
}


typedef std::function<void(shared_sock_t, std::function<void (int conn_id)> const & on_done, const std::atomic_bool&)> processing_fn_t;

/// @brief Creates stats writer if needed, establishes a connection, and runs `processing_fn`.
/// @param urls a list of URLs to to establish a connection
/// @param cfg_stats 
/// @param cfg_conn
/// @param force_break 
/// @param processing_fn 
void common_run(const std::vector<std::string>& urls,
				const stats_config&             cfg_stats,
				const conn_config&              cfg_conn,
				const std::atomic_bool&         force_break,
				processing_fn_t&                processing_fn);

/// @brief Create netaddr_any from host and port values.
netaddr_any create_addr(const std::string& host, unsigned short port, int pref_family = AF_UNSPEC);

} // namespace xtransmit
