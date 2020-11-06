#pragma once
#include <chrono>
#include <string.h>
#include <iostream>
#include <iomanip>	// std::put_time
#include <sstream>	// std::stringstream, std::stringbuf


// Note: std::put_time is supported only in GCC 5 and higher
#if !defined(__GNUC__) || defined(__clang__) || (__GNUC__ >= 5)
#define HAS_PUT_TIME
#endif


#ifdef HAS_PUT_TIME
// Follows ISO 8601
inline std::string print_timestamp_now()
{
	using namespace std;
	using namespace std::chrono;

	const auto systime_now = system_clock::now();
	const time_t time_now = system_clock::to_time_t(systime_now);
	// Ignore the error from localtime, as zeroed tm_now is acceptable.
	tm tm_now = {};
#ifdef _WIN32
	localtime_s(&tm_now, &time_now);
#else
	localtime_r(&time_now, &tm_now);
#endif

	stringstream ss;
	ss << std::put_time(&tm_now, "%FT%T.") << std::setfill('0') << std::setw(6);

	const auto since_epoch = systime_now.time_since_epoch();
	const seconds s = duration_cast<seconds>(since_epoch);
	ss << duration_cast<microseconds>(since_epoch - s).count();
	ss << std::put_time(&tm_now, "%z");

	return ss.str();
};

#endif // HAS_PUT_TIME
