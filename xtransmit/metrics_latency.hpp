#pragma once
#include <chrono>
#include <limits>

namespace xtransmit
{
namespace metrics
{

class latency
{
	typedef std::chrono::system_clock::time_point time_point;
	typedef std::chrono::system_clock::duration duration;

public:
	latency()
	{}

public:
	/// Submit new sample for latency (transmission delay) metric update.
	/// @param [in] timestamp  packet timestamp extracted from the payload (system clock)
	/// @param [in] arrival_time  the time packet is received by receiver (system clock)
	void submit_sample(const time_point& timestamp, const time_point& arrival_time);

	// Reset values at the end of the measurement period.
	void reset();

	/// Get current latency value, in microseconds. Min and max values are
	/// minimum and maximum latencies registered during mesurement period.
	/// Avg is the smoothed delay (the same coefficient of 1/16 as in RFC 3550 is applied)
	/// which isn't reset at the start of the new measurement period.
	long long get_latency_min() const { return m_latency_min; }
	long long get_latency_max() const { return m_latency_max; }
	long long get_latency_avg() const { return m_latency_avg; }

private:
	long long m_latency_min = std::numeric_limits<long long>::max();
	long long m_latency_max = std::numeric_limits<long long>::min();
	long long m_latency_avg = -1;
};


} // namespace metrics
} // namespace xtransmit
