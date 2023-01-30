#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace xtransmit
{
using namespace std;
using namespace std::chrono;

class task
{
public:
	explicit task(std::function<void()>&& f)
		: f(std::move(f))
	{
	}

	function<void()> f;
};

class scheduler
{
public:
	explicit scheduler(unsigned int max_n_tasks = 4)
		: done_(false)
		, thread_(&scheduler::timer_loop, this)
	{
	}

	scheduler(const scheduler&) = delete;

	scheduler(scheduler&&) noexcept = delete;

	scheduler& operator=(const scheduler&) = delete;

	scheduler& operator=(scheduler&&) noexcept = delete;

	~scheduler()
	{
		done_ = true;
		sync_.mtx.lock();
		sync_.cv.notify_one();
		sync_.mtx.unlock();
		if (thread_.joinable())
			thread_.join();
	}

	template <typename Callable, typename... Args>
	void schedule_on(const steady_clock::time_point time, Callable&& f, Args&&... args)
	{
		shared_ptr<task> t =
			make_shared<task>(bind(forward<Callable>(f), forward<Args>(args)...));
		add_task(time, move(t));
	}

	template <typename Callable, typename... Args>
	void schedule_in(const steady_clock::duration time, Callable&& f, Args&&... args)
	{
		schedule_on(steady_clock::now() + time, forward<Callable>(f), forward<Args>(args)...);
	}

private:
	atomic<bool> done_;
	struct
	{
		mutex              mtx;
		condition_variable cv;
	} sync_;

	multimap<steady_clock::time_point, shared_ptr<task>> tasks_;
	mutex                                                lock_;
	thread                                               thread_;

	void timer_loop()
	{
		while (!this->done_)
		{
			this->manage_tasks();

			if (this->tasks_.empty())
			{
				unique_lock<mutex> lock(sync_.mtx);
				sync_.cv.wait(lock, [this]() { return done_ || !tasks_.empty(); });
			}
			else
			{
				const auto         time_of_first_task = (*tasks_.begin()).first;
				unique_lock<mutex> lock(sync_.mtx);
				sync_.cv.wait_until(lock, time_of_first_task);
			}
		}
	}


	void add_task(const steady_clock::time_point time, shared_ptr<task> t)
	{
		lock_guard<mutex> l(lock_);
		tasks_.emplace(time, move(t));
		sync_.cv.notify_one();
	}

	void manage_tasks()
	{
		lock_guard<mutex> l(lock_);

		auto end_of_tasks_to_run = tasks_.upper_bound(steady_clock::now());
		if (end_of_tasks_to_run != tasks_.begin())
		{
			// for all tasks_ that have been triggered
			for (auto i = tasks_.begin(); i != end_of_tasks_to_run; ++i)
			{
				auto& task = (*i).second;
				task->f();
			}

			// remove the completed tasks_
			tasks_.erase(tasks_.begin(), end_of_tasks_to_run);
		}
	}
};

} // namespace xtransmit
