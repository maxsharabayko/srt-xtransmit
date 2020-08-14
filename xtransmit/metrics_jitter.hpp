#pragma once
#include <chrono>

namespace xtransmit
{
namespace metrics
{

class jitter_trace
{
    typedef std::chrono::steady_clock::time_point time_point;
    typedef std::chrono::steady_clock::duration duration;

public:
    jitter_trace()
    {}

public:
	/// Submit new sample for jitter update.
	/// @param [in] sample_time  the timestamp of the sample
	/// @param [in] current_time current time to compare the timestamp with
    void new_sample(const time_point& sample_time, const time_point& current_time);

	/// Get curent jitter value.
    uint64_t jitter() const { return m_jitter; }

private:
    duration m_prev_delay = duration::zero();
    uint64_t m_jitter = 0;
};


} // namespace metrics
} // namespace xtransmit
