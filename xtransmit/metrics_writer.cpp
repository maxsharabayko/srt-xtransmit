#include <thread>
#include "metrics_writer.hpp"

// submodules
#include "spdlog/spdlog.h"

using namespace std;
using namespace std::chrono;

namespace xtransmit
{
namespace metrics
{

metrics_writer::metrics_writer(const std::string& filename, const std::chrono::milliseconds& interval)
	: m_interval(interval)
{
	if (!filename.empty())
	{
		m_file.open(filename, ios::out);
		if (!m_file)
		{
			const auto msg = fmt::format("[METRICS] Failed to open file for output. Path: {0}.", filename);
			spdlog::critical(msg);
			throw runtime_error(msg);
		}
		m_file << validator::stats_csv_header() << flush;
	}
}

metrics_writer::~metrics_writer() { stop(); }

void metrics_writer::add_validator(shared_validator v, SOCKET id)
{
	if (!v)
	{
		spdlog::error("[METRICS] No validator supplied.");
		return;
	}

	m_lock.lock();
	m_validators.emplace(make_pair(id, std::move(v)));
	m_lock.unlock();

	spdlog::trace("[METRICS] Added validator {}.", id);

	if (m_metrics_future.valid())
		return;

	m_stop_token     = false;
	m_metrics_future = launch();
}

void metrics_writer::remove_validator(SOCKET id)
{
	m_lock.lock();
	const size_t n = m_validators.erase(id);
	m_lock.unlock();

	if (n == 1)
	{
		spdlog::trace("[METRICS] Removed validator {}.", id);
	}
	else
	{
		spdlog::trace("[METRICS] Removing validator {}: not found, num removed: {}.", id, n);
	}
}

void metrics_writer::clear()
{
	std::lock_guard l(m_lock);
	m_validators.clear();
}

void metrics_writer::stop()
{
	m_stop_token = true;
	if (m_metrics_future.valid())
		m_metrics_future.wait();
}

future<void> metrics_writer::launch()
{
	auto print_metrics = [](map<SOCKET, shared_validator>& validators, ofstream& fout, mutex& stats_lock)
	{
		const bool         print_to_file = fout.is_open();
		scoped_lock<mutex> lock(stats_lock);

		for (auto& it : validators)
		{
			if (!it.second)
			{
				// Skip empty (will be erased in a separate loop below).
				continue;
			}

			auto* v = it.second.get();
			if (v == nullptr)
			{
				spdlog::warn("[METRICS] Removing validator {}. Reason: nullptr.", it.first);
				it.second.reset();
				continue;
			}

			if (print_to_file)
				fout << v->stats_csv() << flush;
			else
				spdlog::info("[METRICS] @{}: {}", it.first, v->stats());
		}

		auto delete_empty = [&validators]()
		{
			auto it = find_if(validators.begin(),
							  validators.end(),
							  [](pair<int, shared_validator> const& p)
							  {
								  return !p.second; // true if empty
							  });
			if (it == validators.end())
				return false;

			validators.erase(it);
			return true;
		};

		// Check if there are empty sockets to delete.
		// Cannot do it in the above for loop because this modifies the container.
		while (delete_empty())
		{
		}

		return;
	};

	auto metrics_func = [&print_metrics](map<SOCKET, shared_validator>& validators,
										 ofstream&                   out,
										 const milliseconds          interval,
										 mutex&                      stats_lock,
										 const atomic_bool&          stop_stats)
	{
		while (!stop_stats)
		{
			print_metrics(validators, out, stats_lock);

			// No lock on stats_lock while sleeping
			this_thread::sleep_for(interval);
		}
	};

	return async(
		::launch::async, metrics_func, ref(m_validators), ref(m_file), m_interval, ref(m_lock), ref(m_stop_token));
}

} // namespace metrics
} // namespace xtransmit