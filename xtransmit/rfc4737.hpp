#pragma once
#include <chrono>
#include <limits>
#include <memory>
#include <string>
#include <numeric>
#include <vector>

// submodules
#include "spdlog/spdlog.h"

namespace xtransmit
{
namespace rfc4737
{
#define LOG_SC_RFC4737 "VALIDATOR "


	using namespace std;
	using namespace std::chrono;

	class generator
	{
	public:
		generator() {}

	public:
		inline void generate_packet(vector<char>& message_to_send)
		{
			iota(message_to_send.begin(), message_to_send.end(), (char)m_seqno);
			uint64_t* ptr = reinterpret_cast<uint64_t*>(message_to_send.data());
			*ptr = m_seqno++;
		}

	private:
		uint64_t m_seqno = 0;
	};


	class validator
	{
	public:
		validator() {}

	public:
		inline void validate_packet(const vector<char>& message_received)
		{
			++m_pkts_rcvd;
			// TODO: Use htonl()
			const uint64_t pkt_seqno = *(reinterpret_cast<const uint64_t*>(message_received.data()));

			if (m_next_time <= steady_clock::now())
			{
				spdlog::info(LOG_SC_RFC4737 "Overal pkts received: {}, lost: {}", m_pkts_rcvd, m_pkts_lost);
				m_next_time += seconds(1);
			}

			if (pkt_seqno == m_seqno)
			{
				++m_seqno;
				return;
			}

			if (pkt_seqno > m_seqno)
			{
				const uint64_t lost = pkt_seqno - m_seqno;
				m_pkts_lost += lost;
				spdlog::warn(LOG_SC_RFC4737 "Detected loss of {} packets", lost);
				m_seqno += lost;
			}
			else // pkt_seqno < m_seqno
			{
				const uint64_t reorder_dist = pkt_seqno - m_seqno;
				spdlog::warn(LOG_SC_RFC4737 "Detected reordered packet, dist {}", reorder_dist);
			}

			++m_seqno;
		}

	private:
		steady_clock::time_point m_next_time = steady_clock::now() + seconds(1);
		uint64_t m_seqno = 0;
		uint64_t m_pkts_lost = 0;
		uint64_t m_pkts_rcvd = 0;
	};
}
}
