#include <cstdlib>
#include <iostream>

#include "metrics_jitter.hpp"

using namespace std;
using namespace std::chrono;
using namespace xtransmit::metrics;

void jitter_trace::new_sample(const time_point& sample_time, const time_point& current_time)
{
	// RFC 3550 suggests to calculate the relative transit time.
	// The relative transit time is the difference between a packet's
	// timestamp and the receiver's clock at the time of arrival,
	// measured in the same units.
	const steady_clock::duration delay = current_time - sample_time;
	if (m_prev_delay != m_prev_delay.zero())
	{
		const uint64_t di = abs(duration_cast<microseconds>(delay - m_prev_delay).count());
		m_jitter = (m_jitter * 15 + di) / 16;
	}
	m_prev_delay = delay;
}
