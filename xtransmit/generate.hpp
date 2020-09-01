#pragma once
#include <atomic>
#include <string>

// Third party libraries
#include "CLI/CLI.hpp"


namespace xtransmit {
namespace generate {

	struct config
	{
		int sendrate					= 0;
		int num_messages				= -1;
		int duration					= 0;
		int message_size				= 1316; ////8 * 1024 * 1024;
		int stats_freq_ms				= 0;
		bool two_way					= false;
		bool enable_metrics				= false;
		std::string stats_file;
		std::string playback_csv;
	};


	void run(const std::string& dst_url, const config &cfg,
			 const std::atomic_bool& force_break);

	CLI::App* add_subcommand(CLI::App &app, config& cfg, std::string& dst_url);
} // namespace generate
} // namespace xtransmit::generate

