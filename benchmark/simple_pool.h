/*
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 *
 *  A deliberately simple persistent thread pool: fixed worker count, single
 *  mutex-guarded FIFO queue, condvar wake-up. Serves as a baseline against
 *  which to compare multi (a work-stealing / caller-assists pool) and
 *  std::async (no pool at all). Not deadlock-safe for deep recursion: if
 *  all worker threads block in wait_all(), the pool stalls.
 */

#ifndef _BENCH_SIMPLE_POOL_H_
#define _BENCH_SIMPLE_POOL_H_

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

class SimplePool
{
public:
	using Task = std::function<void()>;

	explicit SimplePool(size_t threadCount)
		: m_stop(false)
		, m_pending(0)
	{
		m_threads.reserve(threadCount);
		for (size_t i = 0; i < threadCount; ++i)
			m_threads.emplace_back([this]() { workerLoop(); });
	}

	SimplePool(const SimplePool&) = delete;
	SimplePool& operator=(const SimplePool&) = delete;

	~SimplePool()
	{
		{
			std::lock_guard<std::mutex> lk(m_mutex);
			m_stop = true;
		}
		m_wake.notify_all();
		for (auto& t : m_threads)
			t.join();
	}

	void submit(Task task)
	{
		{
			std::lock_guard<std::mutex> lk(m_mutex);
			m_queue.push(std::move(task));
			++m_pending;
		}
		m_wake.notify_one();
	}

	void wait_all()
	{
		std::unique_lock<std::mutex> lk(m_mutex);
		m_done.wait(lk, [this]() { return m_pending == 0; });
	}

	size_t threadCount() const { return m_threads.size(); }

private:
	void workerLoop()
	{
		for (;;)
		{
			Task task;
			{
				std::unique_lock<std::mutex> lk(m_mutex);
				m_wake.wait(lk, [this]() { return m_stop || !m_queue.empty(); });
				if (m_stop && m_queue.empty())
					return;
				task = std::move(m_queue.front());
				m_queue.pop();
			}
			task();
			{
				std::lock_guard<std::mutex> lk(m_mutex);
				if (--m_pending == 0)
					m_done.notify_all();
			}
		}
	}

	std::vector<std::thread> m_threads;
	std::queue<Task> m_queue;
	std::mutex m_mutex;
	std::condition_variable m_wake;
	std::condition_variable m_done;
	bool m_stop;
	size_t m_pending;
};

#endif // _BENCH_SIMPLE_POOL_H_
