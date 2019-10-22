#pragma once
#include <atomic>
#include <string>


//#define ENABLE_FILE
#ifdef ENABLE_FILE

namespace xtransmit {
	namespace file {

		struct config
		{
			std::string src_path;
		};


		void send(const std::string& dst_url, const config& cfg,
			const std::atomic_bool& force_break);


	} // namespace generate
} // namespace xtransmit

#endif
