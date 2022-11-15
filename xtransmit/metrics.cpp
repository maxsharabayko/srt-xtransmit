#include <array>
#include <sstream>
#include <iomanip> // put_time
#include <iostream> //cerr
#include "metrics.hpp"
#include "misc.hpp"

#include "md5.h" // srtcore

using namespace xtransmit::metrics;
using namespace std;
using namespace std::chrono;


/// Enabling metrics functionality is possible if certain fields are transmitted
/// within the packet payload. The structure of the payload is the following:
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
/// 32 |                              Length                           |
///    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
/// 40 |                           MD5 Checksum                        |
///    |                                                               |
///    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
/// 56 |                         Remaining payload                     |
///
///    |<-------------------------- 64 bits -------------------------->|
///
///
/// TODO: Consider using "%d.%m.%Y.%H:%M:%S.microseconds" as the SYSTIME format

namespace xtransmit
{
namespace metrics
{

static const ptrdiff_t PKT_SEQNO_BYTE_OFFSET     =  0;
static const ptrdiff_t SYS_TIMESTAMP_BYTE_OFFSET =  8;
static const ptrdiff_t STD_TIMESTAMP_BYTE_OFFSET = 16;
static const ptrdiff_t PKT_LENGTH_BYTE_OFFSET    = 32;
static const ptrdiff_t PKT_MD5_BYTE_OFFSET       = 40;
static const ptrdiff_t PKT_MD5_BYTE_LEN          = 16;

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

void write_packet_length(vector<char>& payload, uint64_t length)
{
	uint64_t* ptr = reinterpret_cast<uint64_t*>(payload.data() + PKT_LENGTH_BYTE_OFFSET);
	*ptr = length;
}

uint64_t read_packet_length(const vector<char>& payload)
{
	const uint64_t length = *reinterpret_cast<const uint64_t*>(payload.data() + PKT_LENGTH_BYTE_OFFSET);
	return length;
}

void write_packet_checksum(vector<char>& payload)
{
	using namespace srt;
	md5_state_t s;
	md5_init(&s);

	md5_append(&s, (const md5_byte_t*) payload.data(), (int)PKT_MD5_BYTE_OFFSET);

	const ptrdiff_t skip = PKT_MD5_BYTE_OFFSET + PKT_MD5_BYTE_LEN;
	md5_append(&s, (const md5_byte_t*)payload.data() + skip, (int)(payload.size() - skip));
	md5_finish(&s, (md5_byte_t*) (payload.data() + PKT_MD5_BYTE_OFFSET));
}

bool validate_packet_checksum(const vector<char>& payload)
{
	using namespace srt;
	md5_state_t s;
	md5_init(&s);

	md5_append(&s, (const md5_byte_t*)payload.data(), (int)PKT_MD5_BYTE_OFFSET);

	const ptrdiff_t skip = PKT_MD5_BYTE_OFFSET + PKT_MD5_BYTE_LEN;
	md5_append(&s, (const md5_byte_t*)payload.data() + skip, (int)(payload.size() - skip));

	array<md5_byte_t, PKT_MD5_BYTE_LEN> result;
	md5_finish(&s, result.data());

	const md5_byte_t* ptr = (md5_byte_t*) (payload.data() + PKT_MD5_BYTE_OFFSET);
	const int cmpres = std::memcmp(ptr, result.data(), result.size());

	return cmpres == 0;
}

std:: string validator::stats()
{
	std::stringstream ss;

	auto latency_str = [](long long val, long long na_val) -> string {
		if (val == na_val)
			return "n/a";
		return to_string(val);
	};

	const auto latency_min = m_latency.get_latency_min();
	const auto latency_max = m_latency.get_latency_max();
	
	ss << "Latency, us: avg ";
	ss << latency_str(m_latency.get_latency_avg(), -1) << ", min ";
	ss << latency_str(latency_min, numeric_limits<long long>::max()) << ", max ";
	ss << latency_str(latency_max, numeric_limits<long long>::min());
	ss << ". Jitter: " << m_jitter.get_jitter() << "us. ";
	ss << "Delay Factor: " << m_delay_factor.get_delay_factor() << "us. ";
	const auto stats = m_reorder.get_stats();
	ss << "Pkts: rcvd " << stats.pkts_processed << ", reordered " << stats.pkts_reordered;
	ss << " (dist " << stats.reorder_dist;
	ss << "), lost " << stats.pkts_lost;
	const auto intgr_stats = m_integrity.get_stats();
	ss << ", MD5 err " << intgr_stats.pkts_wrong_checksum;
	ss << ", bad len " << intgr_stats.pkts_wrong_len << '.';

	m_latency.reset();
	m_delay_factor.reset();

	return ss.str();
}

string validator::stats_csv(bool only_header)
{
	stringstream ss;

	if (only_header)
	{
#ifdef HAS_PUT_TIME
		ss << "Timepoint,";
#endif
		ss << "usLatencyMin,";
		ss << "usLatencyMax,";
		ss << "usLatencyAvg,";
		ss << "usJitter,";
		ss << "usDelayFactor,";
		ss << "pktReceived,";
		ss << "pktLost,";
		ss << "pktReordered,";
		ss << "pktReorderDist,";
		ss << "pktChecksumError,";
		ss << "pktLengthError";
		ss << '\n';
	}
	else
	{
#ifdef HAS_PUT_TIME
		ss << print_timestamp_now() << ',';
#endif

		// Empty string (N/A) on default-initialized latency min and max values.
		auto latency_str = [](long long val, long long na_val) -> string {
			if (val == na_val)
				return "";
			return to_string(val);
		};

		const auto latency_min = m_latency.get_latency_min();
		ss << latency_str(latency_min, numeric_limits<long long>::max()) << ',';
		const auto latency_max = m_latency.get_latency_max();
		ss << latency_str(latency_max, numeric_limits<long long>::min()) << ',';
		ss << latency_str(m_latency.get_latency_avg(), -1) << ',';
		ss << m_jitter.get_jitter() << ',';
		ss << m_delay_factor.get_delay_factor() << ',';
		const auto stats = m_reorder.get_stats();
		ss << stats.pkts_processed << ',';
		ss << stats.pkts_lost << ',';
		ss << stats.pkts_reordered << ',';
		ss << stats.reorder_dist << ',';
		const auto intgr_stats = m_integrity.get_stats();
		ss << intgr_stats.pkts_wrong_checksum << ',';
		ss << intgr_stats.pkts_wrong_len;
		ss << '\n';

		m_latency.reset();
		m_delay_factor.reset();
	}

	return ss.str();
}

} // namespace metrics
} // namespace xtransmit
