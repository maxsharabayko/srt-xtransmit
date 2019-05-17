#pragma once
#include <atomic>
#include <string>


namespace xtransmit {
	namespace receive {

		struct config
		{
			bool print_notifications	= false;		// Print notifications about the messages received
			bool send_reply				= false;
			int max_connections			= 1;		// Maximum number of connections on a socket
			int message_size			= 1316;
			int stats_freq_ms			= 0;
			std::string stats_file;
		};



		void receive_main(const std::string& url, const config& cfg,
			const std::atomic_bool& force_break);


	} // namespace receive
} // namespace xtransmit

