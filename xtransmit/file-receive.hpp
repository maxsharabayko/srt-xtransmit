#pragma once
#if ENABLE_FILE_TRANSFER
#include <atomic>
#include <string>

// Third party libraries
#include "CLI/CLI.hpp"


namespace xtransmit::file::receive
{

	struct config
	{
		std::string dst_path;
		size_t      segment_size = 1456 * 1000;
		int stats_freq_ms = 0;
		std::string stats_file;
	};


	void run(const std::string& src_url, const config& cfg,
		const std::atomic_bool& force_break);

	CLI::App* add_subcommand(CLI::App& app, config& cfg, std::string& src_url);


} // namespace xtransmit::file

#endif // ENABLE_FILE_TRANSFER
