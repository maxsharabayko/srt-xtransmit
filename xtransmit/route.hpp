#pragma once
#include <atomic>
#include <string>

// Third party libraries
#include "CLI/CLI.hpp"


namespace xtransmit {
	namespace route {

		struct config
		{
			int message_size = 1456;
			bool bidir = false;
			bool reconnect = false;
			int stats_freq_ms = 0;
			std::string stats_file;
			std::string stats_format = "csv";
		};


		void run(const std::vector<std::string>& src_urls, const std::vector<std::string>& dst_urls,
			const config& cfg, const std::atomic_bool& force_break);

		CLI::App* add_subcommand(CLI::App& app, config& cfg,
			std::vector<std::string>& src_urls, std::vector<std::string>& dst_urls);


	}	// namespace forward
}	// namespace xtransmit

