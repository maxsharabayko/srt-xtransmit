#pragma once
#include <chrono>
#include <limits>
#include <memory>
#include <string>
#include <numeric>
#include <vector>

// submodules
#include "spdlog/spdlog.h"

#include "buffer.hpp"
#include "metrics_latency.hpp"      // Transmission Delay
#include "metrics_jitter.hpp"       // Interarrival Jitter (RFC 3550)
#include "metrics_delay_factor.hpp" // Time-Stamped Delay Factor (TS-DF) (EBU TECH 3337)
#include "metrics_reorder.hpp"      // RFC 4737
#include "metrics_integrity.hpp"

namespace xtransmit
{
namespace metrics
{
	using namespace std;
	using namespace std::chrono;

	//#define LOG_SC_METRICS "METRIC "

	void write_sysclock_timestamp(vector<char>& payload);
	system_clock::time_point read_sysclock_timestamp(const const_buffer& payload);
	void write_steadyclock_timestamp(vector<char>& payload);
	steady_clock::time_point read_stdclock_timestamp(const const_buffer& payload);
	void write_packet_seqno(vector<char>& payload, uint64_t seqno);
	uint64_t read_packet_seqno(const const_buffer& payload);
	void write_packet_length(vector<char>& payload, uint64_t length);
	uint64_t read_packet_length(const const_buffer& payload);
	void write_packet_checksum(vector<char>& payload);
	/// @brief Check if the MD5 checksum of the packet is correct.
	/// @param payload the payload
	/// @return true if the checksum is correct, false otherwise.
	bool validate_packet_checksum(const const_buffer& payload);

	class generator
	{
	public:
		explicit generator(bool enable_metrics)
			 :m_enable_metrics(enable_metrics)
		{}

	public:
		inline void generate_payload(vector<char>& payload)
		{
			const int seqno = m_seqno++;
			iota(payload.begin(), payload.end(), static_cast<char>(seqno));
			if (!m_enable_metrics)
				return;

			write_packet_seqno(payload, seqno);
			write_steadyclock_timestamp(payload);
			write_sysclock_timestamp(payload);
			write_packet_length(payload, payload.size());
			write_packet_checksum(payload);
		}

	private:
		const bool m_enable_metrics;
		uint64_t m_seqno = 0;
	};

	class validator
	{
	public:
		validator(int id) : m_id(id) {}

		inline void validate_packet(const const_buffer& payload)
		{
			std::lock_guard<std::mutex> lock(m_mtx);
			const auto sys_time_now = system_clock::now();
			const auto std_time_now = steady_clock::now();
			
			const uint64_t pktseqno  = read_packet_seqno(payload);
			const auto std_timestamp = read_stdclock_timestamp(payload);
			const auto sys_timestamp = read_sysclock_timestamp(payload);
			const uint64_t pktlength = read_packet_length(payload);
			const bool checksum_match = validate_packet_checksum(payload);

			m_integrity.submit_sample(pktseqno, pktlength, payload.size(), checksum_match);
			if (!checksum_match)
			{
				// Do not calculate other metrics, packet payload is corrupted,
				// the embeded metadata is probably invalid.
				m_reorder.inc_pkts_received();
				return;
			}

			m_latency.submit_sample(sys_timestamp, sys_time_now);
			m_jitter.submit_sample(std_timestamp, std_time_now);
			m_delay_factor.submit_sample(std_timestamp, std_time_now);
			m_reorder.submit_sample(pktseqno);
		}

		std::string stats();
		std::string stats_csv();
		static std::string stats_csv_header();

	private:
		const int m_id;
		latency m_latency;
		jitter m_jitter;
		delay_factor m_delay_factor;
		reorder m_reorder;
		integrity m_integrity;
		mutable std::mutex m_mtx;
	};


} // namespace metrics
} // namespace xtransmit
