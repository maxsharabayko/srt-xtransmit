#pragma once
#include <algorithm> // std::max
#include <chrono>

namespace xtransmit
{
namespace metrics
{

class integrity
{
public:
	integrity() {}

public:
	struct stats
	{
		uint64_t pkts_wrong_len = 0;
		uint64_t pkts_wrong_checksum = 0;
	};

public:
	/// Submit new sample for integrity update.
	/// @param [in] pkt_seqno newly arrived packet sequence number
	/// @param [in] expected_len expected payload length
	/// @param [in] actual_len actual payload length
	/// @param [in] is_valid_checksum true if the checksum is correct
	void submit_sample(const uint64_t pkt_seqno, const uint64_t expected_len, const uint64_t actual_len, const bool is_valid_checksum)
	{
		const bool is_correct_length = expected_len == actual_len;
		if (!is_correct_length)
		{
			++m_stats.pkts_wrong_len;
			spdlog::warn("[METRICS] Incorrect length of packet seqno {}. Expected {}, got {}.",
				pkt_seqno, expected_len, actual_len);
		}

		if (!is_valid_checksum)
		{
			++m_stats.pkts_wrong_checksum;
			spdlog::warn("[METRICS] Incorrect checksum of packet seqno {}.", pkt_seqno);
		}
	}

	stats get_stats() const { return m_stats; }

private:
	stats m_stats;
};


} // namespace metrics
} // namespace xtransmit
