#pragma once
#if ENABLE_FILE_TRANSFER
#include <atomic>
#include <string>

// Third party libraries
#include "CLI/CLI.hpp"


namespace xtransmit::file::send
{

	struct config
	{
		std::string src_path;
		size_t      segment_size = 1456 * 1000;
		bool        only_print = false;	// Do not transfer, just enumerate files and print to stdout
		int stats_freq_ms = 0;
		std::string stats_file;
	};


	void run(const std::string& dst_url, const config& cfg,
		const std::atomic_bool& force_break);

	CLI::App* add_subcommand(CLI::App& app, config& cfg, std::string& dst_url);


} // namespace xtransmit::file::send


#endif // ENABLE_FILE_TRANSFER
