#pragma once
#include <atomic>
#include <string>


namespace xtransmit {
namespace generate {

	struct config
	{
		int bitrate			= 0;
		int num_messages	= 60;
		int message_size = 1316;////8 * 1024 * 1024;
	};



	void generate_main(const std::string& dst_url, const config &cfg,
						const std::atomic_bool& force_break);


} // namespace generate
} // namespace xtransmit

