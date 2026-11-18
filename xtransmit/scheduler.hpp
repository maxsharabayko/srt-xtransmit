#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace xtransmit
{

class task
{
public:
	explicit task(std::function<void()> f)
		: f(std::move(f))
	{
	}

	std::function<void()> f;
};

class scheduler
{
public:
	explicit scheduler(unsigned int max_n_tasks = 4)
		: done_(false)
		, thread_(&scheduler::timer_loop, this)
	{
		(void)max_n_tasks;
	}

	scheduler(const scheduler&) = delete;
	scheduler(scheduler&&) noexcept = delete;
	scheduler& operator=(const scheduler&) = delete;
	scheduler& operator=(scheduler&&) noexcept = delete;

	~scheduler()
	{
		stop();
		sync_.cv.notify_one();
		if (thread_.joinable())
			thread_.join();
	}

	void stop()
	{
		done_.store(true);
		sync_.cv.notify_one();
	}

	template <typename Callable, typename... Args>
	void schedule_on(const std::chrono::steady_clock::time_point time, Callable&& f, Args&&... args)
	{
		auto t = std::make_shared<task>(std::bind(std::forward<Callable>(f), std::forward<Args>(args)...));
		add_task(time, std::move(t));
	}

	template <typename Callable, typename... Args>
	void schedule_in(const std::chrono::steady_clock::duration time, Callable&& f, Args&&... args)
	{
		schedule_on(std::chrono::steady_clock::now() + time, std::forward<Callable>(f), std::forward<Args>(args)...);
	}

private:
	std::atomic<bool> done_;
	struct
	{
		std::mutex              mtx;
		std::condition_variable cv;
	} sync_;

	std::multimap<std::chrono::steady_clock::time_point, std::shared_ptr<task>> tasks_;
	std::thread                                                                  thread_;

	void timer_loop()
	{
		while (!done_.load())
		{
			std::vector<std::shared_ptr<task>> tasks_to_run;
			{
				std::lock_guard<std::mutex> l(sync_.mtx);

				const auto end_of_tasks_to_run = tasks_.upper_bound(std::chrono::steady_clock::now());
				for (auto i = tasks_.begin(); i != end_of_tasks_to_run; ++i)
				{
					tasks_to_run.push_back(i->second);
				}
				tasks_.erase(tasks_.begin(), end_of_tasks_to_run);
			}

			for (auto& scheduled_task : tasks_to_run)
			{
				scheduled_task->f();
			}

			std::unique_lock<std::mutex> lock(sync_.mtx);
			if (tasks_.empty())
			{
				sync_.cv.wait(lock, [this]() { return done_.load() || !tasks_.empty(); });
			}
			else
			{
				sync_.cv.wait_until(lock, tasks_.begin()->first);
			}
		}
	}

	void add_task(const std::chrono::steady_clock::time_point time, std::shared_ptr<task> t)
	{
		std::lock_guard<std::mutex> l(sync_.mtx);
		tasks_.emplace(time, std::move(t));
		sync_.cv.notify_one();
	}

};

} // namespace xtransmit
