#include <algorithm>
#include <sstream>
#include <iostream>
#include <iomanip> // put_time
#include "metrics_latency.hpp"


namespace xtransmit {
namespace metrics {

using namespace std;
using namespace chrono;

void latency::submit_sample(const time_point& timestamp, const time_point& arrival_time)
{
	const long long delay = duration_cast<microseconds>(arrival_time - timestamp).count();
	m_latency_max = max(m_latency_max, delay);
	m_latency_min = min(m_latency_min, delay);

	m_latency_avg = m_latency_avg != -1
		? (m_latency_avg * 15 + delay) / 16
		: delay;
}

void latency::reset()
{
	m_latency_min = std::numeric_limits<long long>::max();
	m_latency_max = std::numeric_limits<long long>::min();
}

} // namespace metrics
} // namespace xtransmit
