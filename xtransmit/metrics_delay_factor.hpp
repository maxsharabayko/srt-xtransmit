#pragma once
#include <chrono>

namespace xtransmit
{
namespace metrics
{

class delay_factor
{
	typedef std::chrono::steady_clock::time_point time_point;
	typedef std::chrono::steady_clock::duration duration;

public:
	delay_factor()
	{}

public:
	/// Submit new sample for Time-Stamped Delay Factor (TS-DF) metric update.
	/// See EBU TECH 3337 for details (https://tech.ebu.ch/docs/tech/tech3337.pdf).
	/// @param [in] timestamp  packet timestamp extracted from the payload (monotonic clock)
	/// @param [in] arrival_time  the time packet is received by receiver (monotonic clock)
	void submit_sample(const time_point& timestamp, const time_point& arrival_time);

	// Reset values at the end of the measurement period.
	void reset();

	/// Get current Delay Factor value, in microseconds. This value is reset at
	/// the start of the measurement period.
	int64_t get_delay_factor() const { return m_relative_transit_time_max - m_relative_transit_time_min; }

private:
	bool m_is_reference_packet = true;
	duration m_reference_delay = duration::zero();  // Transmission delay of the reference packet.
	int64_t m_relative_transit_time_min = std::numeric_limits<int64_t>::max();
	int64_t m_relative_transit_time_max = std::numeric_limits<int64_t>::min();
};


} // namespace metrics
} // namespace xtransmit
