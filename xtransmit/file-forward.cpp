#include "forward.h"
#include "srt_node.hpp"

// submodules
#include "spdlog/spdlog.h"


using namespace std;
using namespace xtransmit::forward;

#define LOG_SC_FORWARD "FORWARD "

shared_ptr<SrtNode> create_node(const config& cfg, const char* uri, bool is_caller)
{
	UriParser urlp(uri);
	urlp["mode"] = is_caller ? string("caller") : string("listener");
	
	if (cfg.planck)
	{
		urlp["transtype"] = string("file");
		urlp["messageapi"] = string("true");

		// If we have this parameter provided, probably someone knows better
		if (!urlp["sndbuf"].exists())
			urlp["sndbuf"] = to_string(3 * (cfg.message_size * 1472 / 1456 + 1472));
		if (!urlp["rcvbuf"].exists())
			urlp["rcvbuf"] = to_string(3 * (cfg.message_size * 1472 / 1456 + 1472));
	}

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

			spdlog::error(LOG_SC_FORWARD "{} ERROR: Receiving message resulted with {} on conn ID {}. {}",
				description, recv_res, connection_id, srt_getlasterror_str());

			break;
		}

		if (recv_res > (int)message_rcvd.size())
		{
			spdlog::error(LOG_SC_FORWARD "{} ERROR: Size of the received message {} exeeds the buffer size {} on conn ID {}",
				description, recv_res, message_rcvd.size(), connection_id);
			break;
		}

		if (recv_res < 50)
		{
			spdlog::debug(LOG_SC_FORWARD "{} RECEIVED MESSAGE on conn ID {}: {}",
				description, connection_id, string(message_rcvd.data(), recv_res).c_str());
		}
		else if (message_rcvd[0] >= '0' && message_rcvd[0] <= 'z')
		{
			spdlog::debug(LOG_SC_FORWARD "{} RECEIVED MESSAGE length on conn ID {} (first character): {}",
				description, dst->GetBindSocket());
		}

		spdlog::debug(LOG_SC_FORWARD "{} Forwarding message to {} (first character): {}",
			description, recv_res, connection_id, message_rcvd[0]);
		
		const int send_res = dst->Send(message_rcvd.data(), recv_res, dst_sock);
		if (send_res <= 0)
		{
			spdlog::error(LOG_SC_FORWARD "{} ERROR: Sending message resulted with {} on conn ID {}. Error message: {}",
				description, send_res, dst->GetBindSocket(), srt_getlasterror_str());

			break;
		}
	}

	if (force_break)
		spdlog::debug(LOG_SC_FORWARD "{} Breaking on request.", description);
	else
		spdlog::debug(LOG_SC_FORWARD "{} Force reconnection.", description);

	src->Close();
	dst->Close();
}



int start_forwarding(const config& cfg, const char* src_uri, const char* dst_uri, const atomic_bool& force_break)
{
	// Create dst connection
	shared_ptr<SrtNode> dst = create_node(cfg, dst_uri, true);
	if (!dst)
	{
		spdlog::error(LOG_SC_FORWARD "ERROR! Failed to create destination node.");
		return 1;
	}

	shared_ptr<SrtNode> src = create_node(cfg, src_uri, false);
	if (!src)
	{
		spdlog::error(LOG_SC_FORWARD "ERROR! Failed to create source node.");
		return 1;
	}


	// Establish target connection first
	const int sock_dst = dst->Connect();
	if (sock_dst == SRT_INVALID_SOCK)
	{
		spdlog::error(LOG_SC_FORWARD "ERROR! While setting up a caller.");
		return 1;
	}


	if (0 != src->Listen(1))
	{
		spdlog::error(LOG_SC_FORWARD "ERROR! While setting up a listener: {}.", srt_getlasterror_str());
		return 1;
	}

	auto future_src_socket = src->AcceptConnection(force_break);
	const SRTSOCKET sock_src = future_src_socket.get();
	if (sock_src == SRT_ERROR)
	{
		spdlog::error(LOG_SC_FORWARD "Wait for source connection canceled");
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
			spdlog::error(LOG_SC_FORWARD "{} ERROR: waiting undelivered data resulted with {}", desc, srt_getlasterror_str());
		}
		if (undelivered)
		{
			spdlog::error(LOG_SC_FORWARD "{} ERROR: still has {} bytes undelivered", desc, srt_getlasterror_str(), undelivered);
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
	CLI::App* sc_forward = app.add_subcommand("forward", "Bidirectional file forwarding. srt://:<src_port> srt://<dst_ip>:<dst_port>");
	sc_forward->add_option("src", src_url, "Source URI");
	sc_forward->add_option("dst", dst_url, "Destination URI");
	sc_forward->add_flag("--oneway", cfg.one_way, "Forward only from SRT to DST");
	sc_forward->add_flag("--planck", cfg.planck, "Apply default config for SRT Planck use case");

	return sc_forward;
}