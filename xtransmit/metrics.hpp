#pragma once
#include <algorithm>
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

	class drift
	{
		typedef std::chrono::system_clock::time_point time_point_sys;
		typedef std::chrono::system_clock::duration duration_sys;
		typedef std::chrono::steady_clock::time_point time_point_std;
		typedef std::chrono::steady_clock::duration duration_std;
	public:
		void submit_sample(const time_point_sys& sample_systime, const time_point_sys& current_systime,
			const time_point_std& sample_stdtime, const time_point_std& current_stdtime)
		{
			if (m_first_stdtime_remote == time_point_std()
				|| m_first_systime_remote == time_point_sys())
			{
				m_first_stdtime_remote = sample_stdtime;
				m_first_systime_remote = sample_systime;
				m_first_stdtime_local  = current_stdtime;
				m_first_systime_local  = current_systime;
				return;
			}

			const auto elapsed_std_remote = sample_stdtime - m_first_stdtime_remote;
			const auto elapsed_std_local  = current_stdtime - m_first_stdtime_local;
			const auto elapsed_sys_remote = sample_systime - m_first_systime_remote;
			const auto elapsed_sys_local  = current_systime - m_first_systime_local;

			const long long delta_local_us  = std::chrono::duration_cast<std::chrono::microseconds>
				(elapsed_std_local - elapsed_sys_local).count();
			const long long delta_remote_us = std::chrono::duration_cast<std::chrono::microseconds>
				(elapsed_std_remote - elapsed_sys_remote).count();
			const long long delta_std_us  = std::chrono::duration_cast<std::chrono::microseconds>
				(elapsed_std_local - elapsed_std_remote).count();
			const long long delta_sys_us  = std::chrono::duration_cast<std::chrono::microseconds>
				(elapsed_sys_local - elapsed_sys_remote).count();

			m_stddiff_us_max = max(m_stddiff_us_min, delta_std_us);
			m_stddiff_us_min = min(m_stddiff_us_min, delta_std_us);
			m_stddiff_us_avg = m_stddiff_us_avg != 0
				? (m_stddiff_us_avg * 15 + delta_std_us) / 16
				: delta_std_us;
			
			m_sysdiff_us_max = max(m_sysdiff_us_min, delta_sys_us);
			m_sysdiff_us_min = min(m_sysdiff_us_min, delta_sys_us);
			m_sysdiff_us_avg = m_sysdiff_us_avg != 0
				? (m_sysdiff_us_avg * 15 + delta_sys_us) / 16
				: delta_sys_us;

			m_localdiff_us_max = max(m_localdiff_us_min, delta_local_us);
			m_localdiff_us_min = min(m_localdiff_us_min, delta_local_us);
			m_localdiff_us_avg = m_localdiff_us_avg != 0
				? (m_localdiff_us_avg * 15 + delta_local_us) / 16
				: delta_local_us;

			m_remotediff_us_max = max(m_remotediff_us_min, delta_remote_us);
			m_remotediff_us_min = min(m_remotediff_us_min, delta_remote_us);
			m_remotediff_us_avg = m_remotediff_us_avg != 0
				? (m_remotediff_us_avg * 15 + delta_remote_us) / 16
				: delta_remote_us;
		}

	public:
		time_point_sys m_first_systime_remote;
		time_point_std m_first_stdtime_remote;
		time_point_sys m_first_systime_local;
		time_point_std m_first_stdtime_local;

		long long m_stddiff_us_min = std::numeric_limits<long long>::max();
		long long m_stddiff_us_max = std::numeric_limits<long long>::min();
		long long m_stddiff_us_avg = 0;
		long long m_sysdiff_us_min = std::numeric_limits<long long>::max();
		long long m_sysdiff_us_max = std::numeric_limits<long long>::min();
		long long m_sysdiff_us_avg = 0;
		
		long long m_localdiff_us_min = std::numeric_limits<long long>::max();
		long long m_localdiff_us_max = std::numeric_limits<long long>::min();
		long long m_localdiff_us_avg = 0;

		long long m_remotediff_us_min = std::numeric_limits<long long>::max();
		long long m_remotediff_us_max = std::numeric_limits<long long>::min();
		long long m_remotediff_us_avg = 0;
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
			m_drift.submit_sample(sys_time, sys_time_now, std_time, std_time_now);
		}

		std:: string stats();
		std::string stats_csv(bool only_header = false);

	private:
		steady_clock::time_point m_next_time = steady_clock::now() + seconds(1);

		jitter_trace m_jitter;
		reorder m_reorder;
		latency m_latency;
		drift m_drift;
	};
}
}
