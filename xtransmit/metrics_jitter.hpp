#pragma once
#include <chrono>

namespace xtransmit
{
namespace metrics
{

class jitter
{
	typedef std::chrono::steady_clock::time_point time_point;
	typedef std::chrono::steady_clock::duration duration;

public:
	jitter()
	{}

public:
	/// Submit new sample for jitter metric (RFC 3550) update.
	/// @param [in] timestamp  packet timestamp extracted from the payload (monotonic clock)
	/// @param [in] arrival_time  the time packet is received by receiver (monotonic clock)
	void submit_sample(const time_point& timestamp, const time_point& arrival_time);

	/// Get current jitter value, in microseconds. This is a smoothed average (as per RFC 3550)
	/// which isn't reset at the start of the new measurement period.
	uint64_t get_jitter() const { return (uint64_t) m_jitter; }

private:
	duration m_prev_delay = duration::zero();
	double m_jitter = 0.0;
};


} // namespace metrics
} // namespace xtransmit
