/// Connection handling.
#include "thread_io.hpp"

namespace xtransmit
{

#define LOG_IO "IO "

using std::async;


void io_dispatch::add(shared_sock_t sock, const std::atomic_bool& force_break, int epoll_flags, on_read_fn_t read_fn)
{
	const SRTSOCKET id = sock->id();
	m_sockets.emplace(id, io_ctx(std::move(sock), read_fn ));
	const int res = srt_epoll_add_usock(m_epoll_io, id, &epoll_flags);
	if (res != SRT_SUCCESS)
	{
		spdlog::error(LOG_IO "Error adding @{} to IO epoll.", id);
	}

	if (m_worker.valid())
		return;

	m_stop_requested = false;
	m_worker = launch();
}

void io_dispatch::stop()
{
	spdlog::info(LOG_IO "stop dispatching.");
	m_stop_requested = true;
	if (m_worker.valid())
		m_worker.wait();
}

future<void> io_dispatch::launch()
{
	// epoll polling loop

	auto th_fn = [this]()
	{
		// Socket readiness to accept a new connection is notified with READ event.
		// Socket readiness for connection is checked by polling on WRITE allowed sockets.
		// See also: https://github.com/Haivision/srt/pull/1831
		// System sockets can be polled using lrfds and lwfds arguments of the srt_epoll_wait.
		constexpr int POLL_TIMEOUT_MS = 100;
		constexpr int MAX_POLL_EVENTS = 1;
		int           rd_len = MAX_POLL_EVENTS;
		int           wr_len = MAX_POLL_EVENTS;
		SRTSOCKET     rd_ready[MAX_POLL_EVENTS] = { SRT_INVALID_SOCK };
		SRTSOCKET     wr_ready[MAX_POLL_EVENTS] = { SRT_INVALID_SOCK };

		while (true)
		{
			const int epoll_res = srt_epoll_wait(m_epoll_io, rd_ready, &rd_len, wr_ready, &wr_len, POLL_TIMEOUT_MS, 0, 0, 0, 0);
			if (m_stop_requested)
			{
				spdlog::info(LOG_IO "dispatch stop requested.");
				break;
			}
			if (epoll_res <= 0)
			{
				const auto e = srt_getlasterror(nullptr);
				if (e == SRT_ETIMEOUT)
					continue;
				spdlog::error(LOG_IO "Epoll error {}.", srt_getlasterror_str());
				break;	
			}

			if (rd_len == 1 && wr_len == 1)
			{
				// Handle error event.
				spdlog::error(LOG_IO "Error reported on @{} is not expected.", wr_ready[0]);
				continue;
			}

			if (rd_len == 1 && rd_ready[0] != SRT_INVALID_SOCK)
			{
				// Handle reading.
				try {
					//spdlog::info(LOG_IO "Signalled read-ready on @{}.", rd_ready[0]);
					auto& s = m_sockets.at(rd_ready[0]);
					s.read_fn(s.sock);
				}
				catch (const std::out_of_range&)
				{
					spdlog::error(LOG_IO "Signalled read-ready on @{}, but not found.", rd_ready[0]);
				}
			}

			if (wr_len == 1 && wr_ready[0] != SRT_INVALID_SOCK)
			{
				// Handle writing.
				spdlog::error(LOG_IO "Write-ready on @{} is not expected.", wr_ready[0]);
			}
		}

		spdlog::warn(LOG_IO "dispatch finishing.");
	};

	return async(std::launch::async, th_fn);
}


} // namespace xtransmit
