#include <cstdlib>
#include <iostream>

#include "metrics_delay_factor.hpp"

using namespace std;
using namespace std::chrono;
using namespace xtransmit::metrics;

void delay_factor::submit_sample(const time_point& timestamp, const time_point& arrival_time)
{
	const steady_clock::duration delay = arrival_time - timestamp;

	if (!m_is_reference_packet)
	{
		const int64_t relative_transit_time = duration_cast<microseconds>(delay - m_reference_delay).count();
		m_relative_transit_time_max = max(m_relative_transit_time_max, relative_transit_time);
		m_relative_transit_time_min = min(m_relative_transit_time_min, relative_transit_time);
	}
	else
	{
		m_reference_delay = delay;
		m_is_reference_packet = false;
	}
}

void delay_factor::reset()
{
	m_is_reference_packet = true;
	m_reference_delay = duration::zero();
	m_relative_transit_time_min = std::numeric_limits<int64_t>::max();
	m_relative_transit_time_max = std::numeric_limits<int64_t>::min();
}
