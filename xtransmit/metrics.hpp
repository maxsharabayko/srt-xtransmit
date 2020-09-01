#pragma once
#include <chrono>
#include <limits>
#include <memory>
#include <string>
#include <numeric>
#include <vector>

// submodules
#include "spdlog/spdlog.h"

#include "metrics_jitter.hpp"  // RFC 3550
#include "metrics_reorder.hpp" // RFC 4737
#include "metrics_latency.hpp"

namespace xtransmit
{
namespace metrics
{
	using namespace std;
	using namespace std::chrono;

	//#define LOG_SC_METRICS "METRIC "

	void write_sysclock_timestamp(vector<char>& payload);
	system_clock::time_point read_sysclock_timestamp(const vector<char>& payload);
	void write_steadyclock_timestamp(vector<char>& payload);
	steady_clock::time_point read_stdclock_timestamp(const vector<char>& payload);
	void write_packet_seqno(vector<char>& payload, uint64_t seqno);
	uint64_t read_packet_seqno(const vector<char>& payload);

	class generator
	{
	public:
		generator(bool enable_metrics)
			 :m_enable_metrics(enable_metrics)
		{}

	public:
		inline void generate_payload(vector<char>& payload)
		{
			iota(payload.begin(), payload.end(), static_cast<char>(m_seqno));
			if (!m_enable_metrics)
				return;

			write_packet_seqno(payload, m_seqno++);
			write_steadyclock_timestamp(payload);
			write_sysclock_timestamp(payload);
		}

	private:
		const bool m_enable_metrics;
		uint64_t m_seqno = 0;
	};

	class validator
	{
	public:
		validator() {}

	public:
		// TODO: sysclock diff mismatch with stdclock diff
		// TODO: latency measurements
		inline void validate_packet(const vector<char>& payload)
		{
			const auto sys_time_now = system_clock::now();
			const auto std_time_now = steady_clock::now();
			
			const uint64_t pktseqno = read_packet_seqno(payload);
			const auto std_time     = read_stdclock_timestamp(payload);
			const auto sys_time     = read_sysclock_timestamp(payload);

			m_jitter.new_sample(std_time, std_time_now);
			m_reorder.submit_sample(pktseqno);
			m_latency.submit_sample(sys_time, sys_time_now);
		}

		std:: string stats();
		std::string stats_csv(bool only_header = false);

	private:
		steady_clock::time_point m_next_time = steady_clock::now() + seconds(1);

		jitter_trace m_jitter;
		reorder m_reorder;
		latency m_latency;
	};
}
}
