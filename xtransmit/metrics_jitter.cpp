#include <cstdlib>
#include <iostream>

#include "metrics_jitter.hpp"

using namespace std;
using namespace std::chrono;
using namespace xtransmit::metrics;

void jitter::submit_sample(const time_point& timestamp, const time_point& arrival_time)
{
	// RFC 3550 defines an algorithm for jitter calculation that is based on
	// the concept of the Relative Transit Time. See section 6.4.1 of the RFC
	// (https://datatracker.ietf.org/doc/html/rfc3550#section-6.4.1)for the detailed explanation.
	const steady_clock::duration delay = arrival_time - timestamp;
	if (m_prev_delay != m_prev_delay.zero())
	{
		const uint64_t relative_transit_time = abs(duration_cast<microseconds>(delay - m_prev_delay).count());
		m_jitter = (m_jitter * 15 + relative_transit_time) / 16;
	}
	m_prev_delay = delay;
}
