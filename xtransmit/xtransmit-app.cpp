#include <stdio.h>
#include <string.h>
#include <vector>
#include <iostream>
#include <thread>
#include <functional>
#include <signal.h>

// Third party libraries
#include "CLI/CLI.hpp"

// SRT libraries
#include "udt.h"	// srt_logger_config
#include "apputil.hpp"
#include "uriparser.hpp"
//#include "smoother.h" // Specific smoother for testing
//#include "srt-flow-smoother.h"
//#include "testmedia.hpp"
#include "srt_node.hpp"
#include "logging.h"
#include "logsupport.hpp"
#include "verbose.hpp"

#include "srt_socket.hpp"


#include "forward.h"
#include "generate.hpp"
#include "receive.hpp"
#include "file-send.hpp"
#include "file-receive.hpp"


using namespace std;


atomic_bool force_break(false);


void OnINT_ForceExit(int)
{
	cerr << "\n-------- REQUESTED INTERRUPT!\n";
	force_break = true;
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



int main(int argc, char **argv)
{
	using namespace xtransmit;

	CLI::App app("SRT xtransmit tool. SRT library v" SRT_VERSION_STRING);
	app.set_config("--config");
	app.set_help_all_flag("--help-all", "Expand all help");

	app.add_flag_function("--verbose,-v", [](size_t) { Verbose::on = true; }, "enable verbose output");

	app.add_option("--loglevel", [](CLI::results_t val) {
		srt_logging::LogLevel::type lev = SrtParseLogLevel(val[0]);
		UDT::setloglevel(lev);
		Verb() << "Log level set to " << val[0];
		return true;
	}, "log level [debug, error, note, info, fatal]");

	app.add_option("--logfa",
				   [](CLI::results_t val) {
					   set<srt_logging::LogFA>     fas = SrtParseLogFA(val[0]);
					   srt_resetlogfa(nullptr, 0);
					   for (set<srt_logging::LogFA>::iterator i = fas.begin(); i != fas.end(); ++i)
						   srt_addlogfa(*i);
					   Verb() << "Logfa set to " << val[0];
					   return true;
				   },
				   "log functional area [ all, general, bstats, control, data, tsbpd, rexmit ]");

	CLI::App* cmd_version = app.add_subcommand("version", "Show version info")
		->callback([]() { cerr << "SRT library v" << SRT_VERSION_STRING << endl; });

	string src, dst;

	xtransmit::forward::config cfg_forward;
	CLI::App* sc_forward = xtransmit::forward::add_subcommand(app, cfg_forward, src, dst);

	const map<string, int> to_bps{ {"kbps", 1000}, {"Mbps", 1000000}, {"Gbps", 1000000000} };
	const map<string, int> to_ms{ {"s", 1000}, {"ms", 1} };
	const map<string, int> to_sec{{"s", 1}, {"min", 60}, {"mins", 60}};
	const map<string, int> to_bytes{ {"kB", 1000}, {"MB", 1000000}, {"GB", 1000000000}, {"Gb", 1000000000 / 8} };

	// SUBCOMMAND: generate
	generate::config cfg_generate;
	CLI::App* sc_generate = generate::add_subcommand(app, cfg_generate, dst);


	xtransmit::receive::config cfg_receive;
	CLI::App* sc_receive = receive::add_subcommand(app, cfg_receive, src);

#if ENABLE_FILE_TRANSFER
	CLI::App* sc_file = app.add_subcommand("file", "Send/receive a single file or folder contents")->fallthrough();
	xtransmit::file::send::config cfg_file_send;
	CLI::App* sc_file_send = file::send::add_subcommand(*sc_file, cfg_file_send, dst);
	xtransmit::file::receive::config cfg_file_recv;
	CLI::App* sc_file_recv = file::receive::add_subcommand(*sc_file, cfg_file_recv, src);
#endif

	app.require_subcommand(1);

	// Startup and cleanup network sockets library
	const NetworkInit nwobject;

	CLI11_PARSE(app, argc, argv);

	//signal(SIGINT, OnINT_ForceExit);
	//signal(SIGTERM, OnINT_ForceExit);


	// TODO: Callback for subcommands
	// https://cliutils.gitlab.io/CLI11Tutorial/chapters/an-advanced-example.html
	if (sc_forward->parsed())
	{
		forward::run(src, dst, cfg_forward, force_break);
	}
	else if (sc_generate->parsed())
	{
		generate::run(dst, cfg_generate, force_break);
		return 0;
	}
	else if (sc_receive->parsed())
	{
		xtransmit::receive::run(src, cfg_receive, force_break);
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
#endif
	else
	{

		//std::shared_ptr<xtransmit::srt::socket> sock = std::make_shared<xtransmit::srt::socket>();

		/*auto sconnected = sock->async_connect();
		cerr << "Main\n";
		auto socket = sconnected.get();
		cerr << "Connected\n";*/
	}

	return 0;
}



