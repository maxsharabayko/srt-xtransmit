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
//#include "testmedia.hpp"
#include "srt_node.hpp"
#include "logging.h"
#include "logsupport.hpp"
#include "verbose.hpp"

#include "srt_socket.hpp"


#include "generate.hpp"
#include "receive.hpp"




using namespace std;



const srt_logging::LogFA SRT_LOGFA_FORWARDER = 10;
srt_logging::Logger g_applog(SRT_LOGFA_FORWARDER, srt_logger_config, "SRT.fwd");


const size_t s_message_size = 8 * 1024 * 1024;

atomic_bool force_break(false);
atomic_bool interrup_break(false);


void OnINT_ForceExit(int)
{
	cerr << "\n-------- REQUESTED INTERRUPT!\n";
	force_break = true;
	interrup_break = true;
}



shared_ptr<SrtNode> create_node(const char *uri, bool is_caller)
{
	UriParser urlp(uri);
	urlp["transtype"] = string("file");
	urlp["messageapi"] = string("true");
	urlp["mode"] = is_caller ? string("caller") : string("listener");

	// If we have this parameter provided, probably someone knows better
	if (!urlp["sndbuf"].exists())
		urlp["sndbuf"] = to_string(3 * (s_message_size * 1472 / 1456 + 1472));
	if (!urlp["rcvbuf"].exists())
		urlp["rcvbuf"] = to_string(3 * (s_message_size * 1472 / 1456 + 1472));

	return shared_ptr<SrtNode>(new SrtNode(urlp));
}


void fwd_route(shared_ptr<SrtNode> src, shared_ptr<SrtNode> dst, SRTSOCKET dst_sock, const string&& description)
{
	vector<char> message_rcvd(s_message_size);

	while (!force_break)
	{
		int connection_id = 0;
		const int recv_res = src->Receive(message_rcvd.data(), message_rcvd.size(), &connection_id);
		if (recv_res <= 0)
		{
			if (recv_res == 0 && connection_id == 0)
				break;

			g_applog.Error() << description << "ERROR: Receiving message resulted with " << recv_res
				<< " on conn ID " << connection_id << "\n";
			g_applog.Error() << srt_getlasterror_str();

			break;
		}

		if (recv_res > (int)message_rcvd.size())
		{
			g_applog.Error() << description << "ERROR: Size of the received message " << recv_res
				<< " exeeds the buffer size " << message_rcvd.size();
			g_applog.Error() << " on connection: " << connection_id << "\n";
			break;
		}

		if (recv_res < 50)
		{
			g_applog.Debug() << description << "RECEIVED MESSAGE on conn ID " << connection_id << ": "
				<< string(message_rcvd.data(), recv_res).c_str();
		}
		else if (message_rcvd[0] >= '0' && message_rcvd[0] <= 'z')
		{
			g_applog.Debug() << description << "RECEIVED MESSAGE length " << recv_res << " on conn ID " << connection_id << " (first character): "
				<< message_rcvd[0];
		}


		g_applog.Debug() << description << "Forwarding message to: " << dst->GetBindSocket();
		const int send_res = dst->Send(message_rcvd.data(), recv_res, dst_sock);
		if (send_res <= 0)
		{
			g_applog.Error() << description << "ERROR: Sending message resulted with " << send_res
				<< " on conn ID " << dst->GetBindSocket() << ". Error message: "
				<< srt_getlasterror_str();

			break;
		}

		if (force_break)
		{
			g_applog.Debug() << description << "Breaking on request";
			break;
		}
	}

	if (!force_break)
	{
		g_applog.Debug() << description << "Force reconnection";
		force_break = true;
	}

	src->Close();
	dst->Close();
}



int start_forwarding(const char *src_uri, const char *dst_uri)
{
	// Create dst connection
	shared_ptr<SrtNode> dst = create_node(dst_uri, true);
	if (!dst)
	{
		g_applog.Error() << "ERROR! Failed to create destination node.";
		return 1;
	}

	shared_ptr<SrtNode> src = create_node(src_uri, false);
	if (!src)
	{
		g_applog.Error() << "ERROR! Failed to create source node.";
		return 1;
	}


	// Establish target connection first
	const int sock_dst = dst->Connect();
	if (sock_dst == SRT_INVALID_SOCK)
	{
		g_applog.Error() << "ERROR! While setting up a caller.";
		return 1;
	}


	if (0 != src->Listen(1))
	{
		g_applog.Error() << "ERROR! While setting up a listener: " << srt_getlasterror_str();
		return 1;
	}

	auto future_src_socket = src->AcceptConnection(force_break);
	const SRTSOCKET sock_src = future_src_socket.get();
	if (sock_src == SRT_ERROR)
	{
		g_applog.Error() << "Wait for source connection canceled";
		return 0;
	}


	thread th_src_to_dst(fwd_route, src, dst, dst->GetBindSocket(), string("[SRC->DST] "));
	thread th_dst_to_src(fwd_route, dst, src, sock_src, string("[DST->SRC] "));

	th_src_to_dst.join();
	th_dst_to_src.join();


	auto wait_undelivered = [](shared_ptr<SrtNode> node, int wait_ms, const string&& desc) {
		const int undelivered = node->WaitUndelivered(wait_ms);
		if (undelivered == -1)
		{
			g_applog.Error() << desc.c_str() << "ERROR: waiting undelivered data resulted with " << srt_getlasterror_str();
		}
		if (undelivered)
		{
			g_applog.Error() << desc.c_str() << "ERROR: still has " << undelivered << " bytes undelivered";
		}

		node.reset();
		return undelivered;
	};

	std::future<int> src_undelivered = async(launch::async, wait_undelivered, src, 3000, string("[SRC] "));
	std::future<int> dst_undelivered = async(launch::async, wait_undelivered, dst, 3000, string("[DST] "));

	src_undelivered.wait();
	dst_undelivered.wait();

	return 0;
}




void print_help()
{
	cout << "Forward messages between source and destination both ways.\n"
		<< "    srt-forwarder srt://:<src_port> srt://<dst_ip>:<dst:port>\n";
}


int forward(const string &src, const string &dst)
{
	// This is mainly required on Windows to initialize the network system,
	// for a case when the instance would use UDP. SRT does it on its own, independently.
	if (!SysInitializeNetwork())
		throw std::runtime_error("Can't initialize network!");

	// Symmetrically, this does a cleanup; put into a local destructor to ensure that
	// it's called regardless of how this function returns.
	struct NetworkCleanup
	{
		~NetworkCleanup()
		{
			SysCleanupNetwork();
		}
	} cleanupobj;


	signal(SIGINT, OnINT_ForceExit);
	signal(SIGTERM, OnINT_ForceExit);

	srt_startup();

	while (!interrup_break)
	{
		force_break = false;
		start_forwarding(src.c_str(), dst.c_str());
	}

	return 0;
}





int main(int argc, char **argv) {

	CLI::App app("SRT xtransmit tool.");
	app.set_help_all_flag("--help-all", "Expand all help");

	app.add_flag_function("--verbose,-v", [](size_t) { Verbose::on = true; }, "enable verbose output");

	app.add_option("--loglevel", [](CLI::results_t val) {
		std::cout << "This option was given " << val.size() << " times." << std::endl;
		srt_logging::LogLevel::type lev = SrtParseLogLevel(val[0]);
		UDT::setloglevel(lev);
		UDT::addlogfa(SRT_LOGFA_FORWARDER);
		Verb() << "Log level set to " << val[0];
		return true;
	}, "log level [debug, error, note, info, fatal]");


	CLI::App* sc_async   = app.add_subcommand("async",   "Check async staff");
	CLI::App *sc_forward = app.add_subcommand("forward", "Bidirectional data forwarding");
	string src, dst;
	sc_forward->add_option("src", src, "Source URI");
	sc_forward->add_option("dst", dst, "Destination URI");
	sc_forward->add_flag("--oneway", "Forward only from SRT to DST");


	xtransmit::generate::config cfg_generate;
	CLI::App* sc_generate = app.add_subcommand("generate", "Send generated data");
	sc_generate->add_option("dst", dst, "Destination URI");
	sc_generate->add_option("--msgsize", cfg_generate.message_size, "Destination URI");
	sc_generate->add_option("--bitrate", cfg_generate.bitrate, "Bitrate to generate");
	sc_generate->add_option("--num", cfg_generate.num_messages, "Number of messages to send (-1 for infinite)");
	sc_generate->add_option("--statsfile", cfg_generate.stats_file, "output stats report filename");
	sc_generate->add_option("--statsfreq", cfg_generate.stats_freq_ms, "output stats report frequency (ms)");
	sc_generate->add_flag("--twoway", cfg_generate.two_way, "Both send and receive data");


	xtransmit::receive::config cfg_receive;
	CLI::App* sc_receive = app.add_subcommand("receive", "Receive data");
	sc_receive->add_option("src", src, "Source URI");
	sc_receive->add_option("--msgsize", cfg_receive.message_size, "Destination URI");
	sc_receive->add_option("--statsfile", cfg_receive.stats_file, "output stats report filename");
	sc_receive->add_option("--statsfreq", cfg_receive.stats_freq_ms, "output stats report frequency (ms)");
	sc_receive->add_flag("--twoway", cfg_receive.send_reply, "Both send and receive data");


	// TODO:
	// CLI::App* sc_echo    = app.add_subcommand("echo",    "Echo back all the packets received on the connection");
	// CLI::App *sc_test    = app.add_subcommand("test",    "Receive/send a test content generated");
	// CLI::App *sc_file    = app.add_subcommand("file",    "Receive/send a file");
	// CLI::App *sc_folder  = app.add_subcommand("folder",  "Receive/send a folder");
	// CLI::App *sc_live    = app.add_subcommand("live",    "Receive/send a live source");

	app.require_subcommand(1);

	//std::string file;
	//start->add_option("-f,--file", file, "File name");

	//CLI::Option *s = stop->add_flag("-c,--count", "Counter");

	CLI11_PARSE(app, argc, argv);


	// This is mainly required on Windows to initialize the network system,
	// for a case when the instance would use UDP. SRT does it on its own, independently.
	if (!SysInitializeNetwork())
		throw std::runtime_error("Can't initialize network!");

	// Symmetrically, this does a cleanup; put into a local destructor to ensure that
	// it's called regardless of how this function returns.
	struct NetworkCleanup
	{
		~NetworkCleanup()
		{
			SysCleanupNetwork();
		}
	} cleanupobj;


	signal(SIGINT, OnINT_ForceExit);
	signal(SIGTERM, OnINT_ForceExit);

	srt_startup();

	//std::cout << "Working on --file from start: " << file << std::endl;
	//std::cout << "Working on --count from stop: " << s->count() << ", direct count: " << stop->count("--count")
	//	<< std::endl;
	//std::cout << "Count of --random flag: " << app.count("--random") << std::endl;
	//for (auto subcom : app.get_subcommands())
	//	std::cout << "Subcommand: " << subcom->get_name() << std::endl;

	// TODO: Callback for subcommands
	// https://cliutils.gitlab.io/CLI11Tutorial/chapters/an-advanced-example.html
	if (sc_forward->parsed())
	{
		//forward(src, dst);
	}
	else if (sc_generate->parsed())
	{
		xtransmit::generate::generate_main(dst, cfg_generate, force_break);
		return 0;
	}
	else if (sc_receive->parsed())
	{
		xtransmit::receive::receive_main(src, cfg_receive, force_break);
		return 0;
	}
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



