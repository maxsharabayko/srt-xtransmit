#include <cstdio>
#include <cstring>
#include <vector>
#include <iostream>
#include <thread>
#include <functional>
#include <signal.h>

// Third party libraries
#include "CLI/CLI.hpp"
#include "spdlog/spdlog.h"

// SRT libraries
#include "apputil.hpp"
#include "uriparser.hpp"
#include "srt_node.hpp"
#include "logsupport.hpp"
#include "verbose.hpp"

#include "srt_socket.hpp"

#include "forward.h"
#include "generate.hpp"
#include "receive.hpp"
#include "route.hpp"
#include "file-send.hpp"
#include "file-receive.hpp"

using namespace std;

atomic_bool force_break(false);

void OnINT_ForceExit(int)
{
	cerr << "\n-------- REQUESTED INTERRUPT!\n";
	force_break = true;
	srt_cleanup();
}

struct NetworkInit
{
	NetworkInit()
	{
		// This is mainly required on Windows to initialize the network system,
		// for a case when the instance would use UDP. SRT does it on its own, independently.
		if (!SysInitializeNetwork())
			throw std::runtime_error("Can't initialize network!");
		srt_startup();
	}

	// Symmetrically, this does a cleanup; put into a local destructor to ensure that
	// it's called regardless of how this function returns.
	~NetworkInit()
	{
		SysCleanupNetwork();
		srt_cleanup();
	}
};

string create_srt_logfa_description()
{
	map<int, string> revmap;
	for (auto entry : SrtLogFAList())
		revmap[entry.second] = entry.first;

	// Each group on a new line
	stringstream ss;
	ss << "SRT log functional areas: [";
	int en10 = 0;
	for (auto entry : revmap)
	{
		ss << " " << entry.second;
		if (entry.first / 10 != en10)
		{
			ss << endl;
			en10 = entry.first / 10;
		}
	}
	ss << " ]";
	return ss.str();
}

const char* srt_clock_type_str()
{
	// srt_clock_type() API function was added in SRT v1.4.3
#define HAS_CLOCK_TYPE (SRT_VERSION_VALUE >= SRT_MAKE_VERSION(1, 4, 3))

#if HAS_CLOCK_TYPE
	const int clock_type = srt_clock_type();

	switch (clock_type)
	{
	case SRT_SYNC_CLOCK_STDCXX_STEADY:
		return "CXX11_STEADY";
	case SRT_SYNC_CLOCK_GETTIME_MONOTONIC:
		return "GETTIME_MONOTONIC";
	case SRT_SYNC_CLOCK_WINQPC:
		return "WIN_QPC";
	case SRT_SYNC_CLOCK_MACH_ABSTIME:
		return "MACH_ABSTIME";
	case SRT_SYNC_CLOCK_POSIX_GETTIMEOFDAY:
		return "POSIX_GETTIMEOFDAY";
	default:
		break;
	}
	
	return "UNKNOWN VALUE";
#else
	return "[n/a prior to v1.4.3]";
#endif
}

int main(int argc, char** argv)
{
	using namespace xtransmit;

	CLI::App app("SRT xtransmit tool. SRT library v" SRT_VERSION_STRING);
	app.set_config("--config");
	app.set_help_all_flag("--help-all", "Expand all help");

	spdlog::set_pattern("%H:%M:%S.%f %^[%L]%$ %v");
	app.add_flag_function(
		"--verbose,-v",
		[](size_t) {
			Verbose::on = true;
			spdlog::set_level(spdlog::level::trace);
		},
		"enable verbose output");

	app.add_flag_function(
		"--handle-sigint",
		[](size_t) {
			signal(SIGINT, OnINT_ForceExit);
			signal(SIGTERM, OnINT_ForceExit);
		},
		"Handle Ctrl+C interrupt");

	app.add_option(
		"--loglevel",
		[](CLI::results_t val) {
			srt_logging::LogLevel::type lev = SrtParseLogLevel(val[0]);
			srt_setloglevel(lev);

			// Uncovered spdlog levels:
			// debug = SPDLOG_LEVEL_DEBUG
			// off   = SPDLOG_LEVEL_OFF
			switch (lev)
			{
			case srt_logging::LogLevel::fatal:
				spdlog::set_level(spdlog::level::critical);
				break;
			case srt_logging::LogLevel::error:
				spdlog::set_level(spdlog::level::err);
				break;
			case srt_logging::LogLevel::warning:
				spdlog::set_level(spdlog::level::warn);
				break;
			case srt_logging::LogLevel::note:
				spdlog::set_level(spdlog::level::info);
				break;
			case srt_logging::LogLevel::debug:
				spdlog::set_level(spdlog::level::trace);
				break;
			default:
				break;
			}

			spdlog::info("Log level set to {}", val[0]);
			return true;
		},
		"log level [debug, error, note, info, fatal]");

	const string logfa_desc = create_srt_logfa_description();
	app.add_option(
		"--logfa",
		[](CLI::results_t val) {
			set<srt_logging::LogFA> fas = SrtParseLogFA(val[0]);
			srt_resetlogfa(nullptr, 0);
			for (set<srt_logging::LogFA>::iterator i = fas.begin(); i != fas.end(); ++i)
				srt_addlogfa(*i);

			spdlog::info("SRT log FAs enabled: {}", val[0]);
			return true;
		},
		logfa_desc);

	app.add_flag_function(
		"--version",
		[](size_t) {
			cerr << "SRT library v" << SRT_VERSION_STRING << " clock " << srt_clock_type_str() << endl;
		},
		"Show version info");

	string src, dst;

	generate::config cfg_generate;
	CLI::App*        sc_generate = generate::add_subcommand(app, cfg_generate, dst);

	xtransmit::receive::config cfg_receive;
	CLI::App*                  sc_receive = receive::add_subcommand(app, cfg_receive, src);

	xtransmit::route::config cfg_route;
	CLI::App*                sc_route = route::add_subcommand(app, cfg_route, src, dst);

#if ENABLE_FILE_TRANSFER
	CLI::App* sc_file = app.add_subcommand("file", "Send/receive a single file or folder contents")->fallthrough();
	xtransmit::file::send::config    cfg_file_send;
	CLI::App*                        sc_file_send = file::send::add_subcommand(*sc_file, cfg_file_send, dst);
	xtransmit::file::receive::config cfg_file_recv;
	CLI::App*                        sc_file_recv = file::receive::add_subcommand(*sc_file, cfg_file_recv, src);
	xtransmit::forward::config       cfg_forward;
	CLI::App*                        sc_forward = xtransmit::forward::add_subcommand(*sc_file, cfg_forward, src, dst);
#endif

	app.require_subcommand(1);
	CLI11_PARSE(app, argc, argv);

	// Startup and cleanup network sockets library
	const NetworkInit nwobject;

	// TODO: Callback for subcommands
	// https://cliutils.gitlab.io/CLI11Tutorial/chapters/an-advanced-example.html
	if (sc_generate->parsed())
	{
		generate::run(dst, cfg_generate, force_break);
		return 0;
	}
	else if (sc_receive->parsed())
	{
		xtransmit::receive::run(src, cfg_receive, force_break);
		return 0;
	}
	else if (sc_route->parsed())
	{
		xtransmit::route::run(src, dst, cfg_route, force_break);
		return 0;
	}
#if ENABLE_FILE_TRANSFER
	else if (sc_file_send->parsed())
	{
		file::send::run(dst, cfg_file_send, force_break);
		return 0;
	}
	else if (sc_file_recv->parsed())
	{
		file::receive::run(src, cfg_file_recv, force_break);
		return 0;
	}
	else if (sc_forward->parsed())
	{
		forward::run(src, dst, cfg_forward, force_break);
	}
#endif
	else
	{
		cerr << "Failed to recognize subcommand" << endl;
	}

	return 0;
}
