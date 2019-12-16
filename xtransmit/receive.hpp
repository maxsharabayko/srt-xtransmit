#pragma once
#include <atomic>
#include <string>

// Third party libraries
#include "CLI/CLI.hpp"


namespace xtransmit {
	namespace receive {

		struct config
		{
			bool print_notifications	= false;		// Print notifications about the messages received
			bool send_reply				= false;
			bool check_timestamp		= false;
			int max_connections			= 1;		// Maximum number of connections on a socket
			int message_size			= 1316;
			int stats_freq_ms			= 0;
			std::string stats_file;
		};



		void run(const std::string& url, const config& cfg,
			const std::atomic_bool& force_break);

		CLI::App* add_subcommand(CLI::App& app, config& cfg, std::string& src_url);


	} // namespace receive
} // namespace xtransmit

