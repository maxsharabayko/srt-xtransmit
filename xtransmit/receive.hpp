#pragma once
#include <atomic>
#include <string>
#include <vector>

// Third party libraries
#include "CLI/CLI.hpp"


namespace xtransmit {
	namespace receive {

		struct config
		{
			bool print_notifications	= false;		// Print notifications about the messages received
			bool send_reply				= false;
			bool reconnect				= false;
			bool enable_metrics			= false;
			unsigned metrics_freq_ms	= 1000;
			std::string metrics_file;
			int max_connections			= 1;		// Maximum number of connections on a socket
			int message_size			= 1316;
			int stats_freq_ms			= 0;
			std::string stats_file;
			std::vector<std::string> inputs;
		};



		void run(const std::vector<std::string>& urls, const config& cfg,
			const std::atomic_bool& force_break);

		CLI::App* add_subcommand(CLI::App& app, config& cfg, std::vector<std::string>& src_urls);


	} // namespace receive
} // namespace xtransmit

