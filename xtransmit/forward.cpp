#include "forward.h"
#include "srt_node.hpp"

// SRT logging
#include "udt.h"	// srt_logger_config
#include "logging.h"
#include "logsupport.hpp"


using namespace std;
using namespace xtransmit::forward;


const srt_logging::LogFA SRT_LOGFA_FORWARDER = 10;
srt_logging::Logger g_fwdlog(SRT_LOGFA_FORWARDER, srt_logger_config, "SRT.fwd");


shared_ptr<SrtNode> create_node(const config& cfg, const char* uri, bool is_caller)
{
	UriParser urlp(uri);
	urlp["transtype"] = string("file");
	urlp["messageapi"] = string("true");
	urlp["mode"] = is_caller ? string("caller") : string("listener");

	// If we have this parameter provided, probably someone knows better
	if (!urlp["sndbuf"].exists())
		urlp["sndbuf"] = to_string(3 * (cfg.message_size * 1472 / 1456 + 1472));
	if (!urlp["rcvbuf"].exists())
		urlp["rcvbuf"] = to_string(3 * (cfg.message_size * 1472 / 1456 + 1472));

	return shared_ptr<SrtNode>(new SrtNode(urlp));
}


void fwd_route(shared_ptr<SrtNode> src, shared_ptr<SrtNode> dst, SRTSOCKET dst_sock,
	const config& cfg, const string&& description, const atomic_bool& force_break)
{
	vector<char> message_rcvd(cfg.message_size);

	while (!force_break)
	{
		int connection_id = 0;
		const int recv_res = src->Receive(message_rcvd.data(), message_rcvd.size(), &connection_id);
		if (recv_res <= 0)
		{
			if (recv_res == 0 && connection_id == 0)
				break;

			g_fwdlog.Error() << description << "ERROR: Receiving message resulted with " << recv_res
				<< " on conn ID " << connection_id << "\n";
			g_fwdlog.Error() << srt_getlasterror_str();

			break;
		}

		if (recv_res > (int)message_rcvd.size())
		{
			g_fwdlog.Error() << description << "ERROR: Size of the received message " << recv_res
				<< " exeeds the buffer size " << message_rcvd.size();
			g_fwdlog.Error() << " on connection: " << connection_id << "\n";
			break;
		}

		if (recv_res < 50)
		{
			g_fwdlog.Debug() << description << "RECEIVED MESSAGE on conn ID " << connection_id << ": "
				<< string(message_rcvd.data(), recv_res).c_str();
		}
		else if (message_rcvd[0] >= '0' && message_rcvd[0] <= 'z')
		{
			g_fwdlog.Debug() << description << "RECEIVED MESSAGE length " << recv_res << " on conn ID " << connection_id << " (first character): "
				<< message_rcvd[0];
		}


		g_fwdlog.Debug() << description << "Forwarding message to: " << dst->GetBindSocket();
		const int send_res = dst->Send(message_rcvd.data(), recv_res, dst_sock);
		if (send_res <= 0)
		{
			g_fwdlog.Error() << description << "ERROR: Sending message resulted with " << send_res
				<< " on conn ID " << dst->GetBindSocket() << ". Error message: "
				<< srt_getlasterror_str();

			break;
		}
	}

	if (force_break)
		g_fwdlog.Debug() << description << "Breaking on request";
	else
		g_fwdlog.Debug() << description << "Force reconnection";

	src->Close();
	dst->Close();
}



int start_forwarding(const config& cfg, const char* src_uri, const char* dst_uri, const atomic_bool& force_break)
{
	// Create dst connection
	shared_ptr<SrtNode> dst = create_node(cfg, dst_uri, true);
	if (!dst)
	{
		g_fwdlog.Error() << "ERROR! Failed to create destination node.";
		return 1;
	}

	shared_ptr<SrtNode> src = create_node(cfg, src_uri, false);
	if (!src)
	{
		g_fwdlog.Error() << "ERROR! Failed to create source node.";
		return 1;
	}


	// Establish target connection first
	const int sock_dst = dst->Connect();
	if (sock_dst == SRT_INVALID_SOCK)
	{
		g_fwdlog.Error() << "ERROR! While setting up a caller.";
		return 1;
	}


	if (0 != src->Listen(1))
	{
		g_fwdlog.Error() << "ERROR! While setting up a listener: " << srt_getlasterror_str();
		return 1;
	}

	auto future_src_socket = src->AcceptConnection(force_break);
	const SRTSOCKET sock_src = future_src_socket.get();
	if (sock_src == SRT_ERROR)
	{
		g_fwdlog.Error() << "Wait for source connection canceled";
		return 0;
	}


	thread th_src_to_dst(fwd_route, src, dst, dst->GetBindSocket(), cfg, string("[SRC->DST] "), std::ref(force_break));
	thread th_dst_to_src(fwd_route, dst, src, sock_src, cfg, string("[DST->SRC] "), std::ref(force_break));

	th_src_to_dst.join();
	th_dst_to_src.join();


	auto wait_undelivered = [&cfg](shared_ptr<SrtNode> node, int wait_ms, const string&& desc) {
		const int undelivered = node->WaitUndelivered(wait_ms);
		if (undelivered == -1)
		{
			g_fwdlog.Error() << desc.c_str() << "ERROR: waiting undelivered data resulted with " << srt_getlasterror_str();
		}
		if (undelivered)
		{
			g_fwdlog.Error() << desc.c_str() << "ERROR: still has " << undelivered << " bytes undelivered";
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


void xtransmit::forward::run(const string& src, const string& dst, const config& cfg, const std::atomic_bool& force_break)
{
	while (!force_break)
	{
		start_forwarding(cfg, src.c_str(), dst.c_str(), force_break);
	}
}


CLI::App* xtransmit::forward::add_subcommand(CLI::App& app, config& cfg, string& src_url, string& dst_url)
{
	CLI::App* sc_forward = app.add_subcommand("forward", "Bidirectional data forwarding. srt-forwarder srt://:<src_port> srt://<dst_ip>:<dst:port>");
	sc_forward->add_option("src", src_url, "Source URI");
	sc_forward->add_option("dst", dst_url, "Destination URI");
	sc_forward->add_flag("--oneway", cfg.one_way, "Forward only from SRT to DST");

	return sc_forward;
}