#pragma once
#include <algorithm> // std::max
#include <chrono>

namespace xtransmit
{
namespace metrics
{

class reorder
{
public:
    reorder() {}

public:
	struct stats
	{
		uint64_t expected_seqno = 0;
		uint64_t pkts_processed = 0;
		uint64_t pkts_lost = 0;
		uint64_t pkts_reordered = 0;
		uint64_t reorder_dist = 0;
	};

public:
	/// Submit new sample for reorder update.
	/// @param [in] pkt_seqno newly arrived packet sequence number
    void submit_sample(const uint64_t pkt_seqno)
	{
		++m_stats.pkts_processed;

		if (pkt_seqno == m_stats.expected_seqno)
		{
			++m_stats.expected_seqno;
			return;
		}

		// Sequence discontinuity (loss)
		if (pkt_seqno > m_stats.expected_seqno)
		{
			const uint64_t lost = pkt_seqno - m_stats.expected_seqno;
			m_stats.pkts_lost += lost;
			//spdlog::warn(LOG_SC_RFC4737 "Detected loss of {} packets", lost);
			m_stats.expected_seqno = pkt_seqno + 1;
		}
		else // Packet reordering: pkt_seqno < m_seqno
		{
			++m_stats.pkts_reordered;
			const uint64_t reorder_dist = pkt_seqno - m_stats.expected_seqno;
			m_stats.reorder_dist = std::max(m_stats.reorder_dist, reorder_dist);

			//spdlog::warn(LOG_SC_RFC4737 "Detected reordered packet, dist {}", reorder_dist);
		}
	}

	/// Get curent jitter value.
    uint64_t pkts_lost() const { return m_stats.pkts_lost; }

	stats get_stats() const { return m_stats; }

private:
	stats m_stats;
    // uint64_t m_expected_seqno = 0;
	// uint64_t m_pkts_processed = 0;
	// uint64_t m_pkts_lost = 0;
	// uint64_t m_pkts_reordered = 0;
	// uint64_t m_reorder_dist = 0;
};


} // namespace metrics
} // namespace xtransmit
