#include <atomic>
#include <chrono>
#include <ctime>
#include <future>
#include <iomanip>
#include <memory>
#include <thread>
#include <vector>

// submodules
#include "spdlog/spdlog.h"

// xtransmit
#include "socket_stats.hpp"
#include "srt_socket.hpp"
#include "udp_socket.hpp"
#include "receive.hpp"

// OpenSRT
#include "apputil.hpp"
#include "uriparser.hpp"

#include "handshake.h"

using namespace std;
using namespace xtransmit;
using namespace xtransmit::receive;
using namespace std::chrono;

using shared_srt  = std::shared_ptr<socket::srt>;
using shared_sock = std::shared_ptr<socket::isocket>;

#define LOG_SC_RECEIVE "RECEIVE "

void read_timestamp(const vector<char>& buffer)
{
	// Note: std::put_time is supported only in GCC 5 and higher
#if !defined(__GNUC__) || defined(__clang__) || (__GNUC__ >= 5)
	const time_t    send_time    = *(reinterpret_cast<const time_t*>   (buffer.data()));
	const long long send_time_us = *(reinterpret_cast<const long long*>(buffer.data() + 8));
	const std::tm* tm_send = std::localtime(&send_time);
	if (!tm_send)
	{
		spdlog::error(LOG_SC_RECEIVE "Failed to extract send time");
		return;
	}

	const auto systime_now = system_clock::now();
	const auto delay = systime_now - (system_clock::from_time_t(send_time) + microseconds(send_time_us));

	::stringstream send_time_str; 
	send_time_str << std::put_time(tm_send, "%T.") << std::setfill('0') << std::setw(6) << send_time_us;;

	spdlog::info(LOG_SC_RECEIVE "packet send time {} delay {} ms",
		send_time_str.str(),
		duration_cast<milliseconds>(delay).count());

#else
	static bool printwarn = true;
	if (!printwarn)
		return;

	spdlog::error(LOG_SC_RECEIVE "The --timestamp feature requires GCC 5.0 abd higher, sorry.");
	printwarn = false;
#endif
}


void trace_message(const size_t bytes, const vector<char> &buffer, int conn_id)
{
	::cout << "RECEIVED MESSAGE length " << bytes << " on conn ID " << conn_id;

#if 0
	if (bytes < 50)
	{
		::cout << ":\n";
		::cout << string(buffer.data(), bytes).c_str();
	}
	else if (buffer[0] >= '0' && buffer[0] <= 'z')
	{
		::cout << " (first character):";
		::cout << buffer[0];
	}
#endif
	::cout << endl;

	//CHandShake hs;
	//if (hs.load_from(buffer.data(), buffer.size()) < 0)
	//	return;
	//
	//::cout << "SRT HS: " << hs.show() << endl;
}

void run_pipe(shared_sock src, const config &cfg, const atomic_bool &force_break)
{
	socket::isocket &sock = *src.get();

	vector<char> buffer(cfg.message_size);
	try
	{
		while (!force_break)
		{
			const size_t bytes = sock.read(mutable_buffer(buffer.data(), buffer.size()), -1);

			if (bytes == 0)
			{
				::cerr << "src->read returned 0\n";
				continue;
			}

			if (cfg.print_notifications)
				trace_message(bytes, buffer, sock.id());
			if (cfg.check_timestamp)
				read_timestamp(buffer);

			if (cfg.send_reply)
			{
				const string out_message("Message received");
				sock.write(const_buffer(out_message.data(), out_message.size()));

				if (cfg.print_notifications)
					::cerr << "Reply sent on conn ID " << sock.id() << "\n";
			}
		}
	}
	catch (const socket::exception &e)
	{
		spdlog::warn(LOG_SC_RECEIVE "{}", e.what());
	}

	if (force_break)
	{
		spdlog::info(LOG_SC_RECEIVE "interrupted by request!");
	}
;
}

void xtransmit::receive::run(const string &src_url, const config &cfg, const atomic_bool &force_break)
{
	const UriParser uri(src_url);

	shared_sock socket;
	shared_sock connection;

	try
	{
		const bool write_stats = cfg.stats_file != "" && cfg.stats_freq_ms > 0;
		unique_ptr<socket::stats_writer> stats = write_stats
			? make_unique<socket::stats_writer>(cfg.stats_file, milliseconds(cfg.stats_freq_ms))
			: nullptr;

		if (uri.proto() == "udp")
		{
			connection = make_shared<socket::udp>(uri);
		}
		else
		{
			socket = make_shared<socket::srt>(uri);
			socket::srt* s = static_cast<socket::srt*>(socket.get());
			const bool  accept = s->mode() == socket::srt::LISTENER;
			if (accept)
				s->listen();
			connection = accept ? s->accept() : s->connect();
		}

		stats->add_socket(connection);
		run_pipe(connection, cfg, force_break);
	}
	catch (const socket::exception & e)
	{
		cerr << e.what() << endl;
	}
}

CLI::App* xtransmit::receive::add_subcommand(CLI::App& app, config& cfg, string& src_url)
{
	const map<string, int> to_ms{ {"s", 1000}, {"ms", 1} };

	CLI::App* sc_receive = app.add_subcommand("receive", "Receive data (SRT, UDP)")->fallthrough();
	sc_receive->add_option("src", src_url, "Source URI");
	sc_receive->add_option("--msgsize", cfg.message_size, "Size of a buffer to receive message payload");
	sc_receive->add_option("--statsfile", cfg.stats_file, "output stats report filename");
	sc_receive->add_option("--statsfreq", cfg.stats_freq_ms, "output stats report frequency (ms)")
		->transform(CLI::AsNumberWithUnit(to_ms, CLI::AsNumberWithUnit::CASE_SENSITIVE));
	sc_receive->add_flag("--printmsg", cfg.print_notifications, "print message into to stdout");
	sc_receive->add_flag("--timestamp", cfg.check_timestamp, "Check a timestamp in the message payload");
	sc_receive->add_flag("--twoway", cfg.send_reply, "Both send and receive data");

	return sc_receive;
}



