#pragma once
#include <atomic>
#include <string>


#define ENABLE_FILE
#ifdef ENABLE_FILE

namespace xtransmit::file
{

	struct rcvconfig
	{
		std::string dst_path;
		size_t      segment_size = 1456 * 1000;
		int stats_freq_ms = 0;
		std::string stats_file;
	};


	void receive(const std::string& src_url, const rcvconfig& cfg,
		const std::atomic_bool& force_break);


} // namespace xtransmit::file

#endif

