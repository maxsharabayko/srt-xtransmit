#pragma once
#include <atomic>
#include <string>

// Third party libraries
#include "CLI/CLI.hpp"


namespace xtransmit {
namespace forward {

	struct config
	{
		int message_size = 1456;
		bool planck = false;	// Default settings for Planck project
		bool one_way = false;
	};


	void run(const std::string& src_url, const std::string& dst_url,
		const config& cfg, const std::atomic_bool& force_break);

	CLI::App* add_subcommand(CLI::App& app, config& cfg, std::string& src_url, std::string& dst_url);


}	// namespace forward
}	// namespace xtransmit
