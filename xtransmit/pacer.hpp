#pragma once
#include <chrono>
#include <limits>
#include <memory>
#include <string>
#include <numeric>
#include <vector>

// submodules
#include "spdlog/spdlog.h"

// Log entry prefix
#define LOG_SC_PACER "PACER "

namespace xtransmit
{
using namespace std;
using namespace std::chrono;

class ipacer
{
public:
	virtual ~ipacer() = 0;

public:
	virtual long wait(const atomic_bool& force_break) = 0;
};

// Definition of Pure Virtual Destructor
ipacer::~ipacer() {}

class pacer : public ipacer
{
public:
	pacer(int sendrate_bps, int message_size)
		: m_msg_interval_us(calc_msg_interval_us(sendrate_bps, message_size))
		, m_message_size(message_size)
	{
		spdlog::info(LOG_SC_PACER "sendrate {} bps (inter send interval {} us)", sendrate_bps, m_msg_interval_us);
	}

	~pacer() final {}

public:
	inline long wait(const atomic_bool& force_break) final
	{
		const long message_int_us = (steady_clock::now() > (m_start_time + std::chrono::seconds(5)))
			? ((steady_clock::now() > (m_start_time + std::chrono::seconds(10))) ? calc_msg_interval_us(15000000, m_message_size) : m_msg_interval_us)
			: calc_msg_interval_us(1000000, m_message_size);
		const long inter_send_us = m_timedev_us > message_int_us ? 0 : (message_int_us - m_timedev_us);
		const auto next_time     = m_last_snd_time + microseconds(inter_send_us);
		std::chrono::steady_clock::time_point time_now;
		for (;;)
		{
			time_now = steady_clock::now();
			if (time_now >= next_time)
				break;
			if (force_break)
				break;
		}

		m_timedev_us += (long)duration_cast<microseconds>(time_now - m_last_snd_time).count() - message_int_us;
		m_last_snd_time = time_now;

		return calc_bitrate_bps(message_int_us, m_message_size);
	}

	static inline long calc_msg_interval_us(int sendrate_bps, int message_size)
	{
		const long msgs_per_10s = static_cast<long long>(sendrate_bps / 8) * 10 / message_size;
		return msgs_per_10s ? 10000000 / msgs_per_10s : 0;
	}

	static inline long calc_bitrate_bps(const long message_int_us, int message_size)
	{
		return message_size * 8 * (1000000 / message_int_us);
	}

private:
	typedef std::chrono::steady_clock::time_point time_point;
	const long                                    m_msg_interval_us;
	time_point                                    m_last_snd_time = std::chrono::steady_clock::now();
	time_point m_start_time = std::chrono::steady_clock::now();
	long m_timedev_us = 0; ///< Pacing time deviation (microseconds) is used to adjust the pace
	int m_message_size;
};

class csv_pacer : public ipacer
{
public:
	explicit csv_pacer(const std::string& filename)
		: m_srccsv(filename.c_str())
	{
		if (!m_srccsv)
		{
			spdlog::critical("Failed to open input CSV file. Path: {0}", filename);
			throw socket::exception("Failed to open input CSV file. Path " + filename);
		}
	}

	~csv_pacer() final {}

public:
	inline long wait(const atomic_bool& force_break) final
	{
		const steady_clock::time_point next_time_ = next_time();
		for (;;)
		{
			if (steady_clock::now() >= next_time_)
				break;
			if (force_break)
				break;
		}

		return 0;
	}

private:
	steady_clock::time_point next_time()
	{
		if (m_srccsv.eof())
		{
			m_srccsv.clear(); // Need to clear the eof flag
			m_srccsv.seekg(0, m_srccsv.beg);
			m_start = steady_clock::now();
		}

		std::string line;
		if (!std::getline(m_srccsv, line))
			return steady_clock::time_point();
		const double val = stod(line);
		return m_start + microseconds(long(val * 1000000));
	}

private:
	std::ifstream            m_srccsv;
	steady_clock::time_point m_start = steady_clock::now();
};

} // namespace xtransmit
