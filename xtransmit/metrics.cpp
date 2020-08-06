#include <sstream>
#include <iomanip> // put_time
#include <iostream> //cerr
#include "metrics.hpp"

using namespace xtransmit::metrics;
using namespace std::chrono;


/// Enabling the metrics functionality expects certain fields to be transmitted in the payload.
/// The structure of the payload is the following:
///
///     0         1         2         3         4         5         6
///     0 2 4 6 8 0 2 4 6 8 0 2 4 6 8 0 2 4 6 8 0 2 4 6 8 0 2 4 6 8 0 2 4
///    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
///  0 |                      Packet Sequence Number                   |
///    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
///  8 |                      System Clock Timestamp                   |
///    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
/// 16 |                     Monotonic Clock Timestamp                 |
///    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
/// 32 |                         Remaining payload                     |
///
///    |<-------------------------- 64 bits -------------------------->|
///
///
/// TODO: Consider using "%d.%m.%Y.%H:%M:%S.microseconds" as the SYSTIME format

namespace xtransmit {
namespace metrics {

const ptrdiff_t PKT_SEQNO_BYTE_OFFSET     =  0;
const ptrdiff_t SYS_TIMESTAMP_BYTE_OFFSET =  8;
const ptrdiff_t STD_TIMESTAMP_BYTE_OFFSET = 16;

void write_sysclock_timestamp(vector<char>& payload)
{
	const auto systime_now = system_clock::now();
	const auto elapsed_us  = duration_cast<microseconds>(systime_now.time_since_epoch());
	// std::cerr << "Writing elapsed_us " << elapsed_us.count() << endl;
	*(reinterpret_cast<int64_t*>(payload.data() + SYS_TIMESTAMP_BYTE_OFFSET)) = elapsed_us.count();
}

system_clock::time_point read_sysclock_timestamp(const vector<char>& payload)
{
	const int64_t elapsed_us = *(reinterpret_cast<const int64_t*>(payload.data() + SYS_TIMESTAMP_BYTE_OFFSET));

	// auto in_time_t = std::chrono::system_clock::to_time_t(system_clock::time_point() + microseconds(elapsed_us));
	// std::stringstream ss;
	// ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %X");
	// in_time_t = std::chrono::system_clock::to_time_t(system_clock::time_point() + microseconds(elapsed_us));
	// ss << " now: " << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %X");
	// std::cerr << "Recovered elapsed_us " << elapsed_us << endl;
	// std::cerr << ss.str() << endl;

	const auto sys_timestamp = system_clock::time_point() + microseconds(elapsed_us);
	return sys_timestamp;
}

void write_steadyclock_timestamp(vector<char>& payload)
{
	const auto stdtime_now = steady_clock::now();
	const auto elapsed_us  = duration_cast<microseconds>(stdtime_now.time_since_epoch());
	*(reinterpret_cast<int64_t*>(payload.data() + STD_TIMESTAMP_BYTE_OFFSET)) = elapsed_us.count();
}

steady_clock::time_point read_stdclock_timestamp(const vector<char>& payload)
{
	const int64_t elapsed_us = *(reinterpret_cast<const int64_t*>(payload.data() + STD_TIMESTAMP_BYTE_OFFSET));
	const auto std_timestamp = steady_clock::time_point() + microseconds(elapsed_us);
	return std_timestamp;
}

void write_packet_seqno(vector<char>& payload, uint64_t seqno)
{
	uint64_t* ptr = reinterpret_cast<uint64_t*>(payload.data() + PKT_SEQNO_BYTE_OFFSET);
	*ptr = seqno;
}

uint64_t read_packet_seqno(const vector<char>& payload)
{
	const uint64_t seqno = *reinterpret_cast<const uint64_t*>(payload.data() + PKT_SEQNO_BYTE_OFFSET);
	return seqno;
}

std:: string validator::stats()
{
	std::stringstream ss;
	
	ss << "Latency, us (min/max/avg): " << m_latency.get_latency_min() << "/" << m_latency.get_latency_max() << "/" << m_latency.get_latency_avg();
	ss << ". Jitter: " << m_jitter.jitter() << "us. ";
	const auto stats = m_reorder.get_stats();
	ss << "Pkts: rcvd " << stats.pkts_processed << ", reordered " << stats.pkts_reordered;
	ss << " (dist " << stats.reorder_dist;
	ss << "), lost " << stats.pkts_lost;

	m_latency.reset();
	return ss.str();
}

} // namespace metrics
} // namespace xtransmit