/*
 *  Created by LuckyNeko on 15/03/2020.
 *  Copyright 2020 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "multi/context.h"

#include <atomic>
#include <thread>

namespace multi
{
	void Context::start(size_t threadCount)
	{
		m_workerPool.start(threadCount);
	}

	void Context::stop()
	{
		m_workerPool.stop();
	}

	size_t Context::threadCount() const
	{
		return m_workerPool.threadCount();
	}

	Handle Context::async(Task&& task)
	{
		auto promise = std::make_shared<std::promise<void>>();
		auto hdl = promise->get_future();
		auto wrappedTask = [task, promise]()
		{
			task();
			promise->set_value();
		};
		m_workerPool.submit(std::move(wrappedTask));
		return Handle(std::move(hdl));
	}

	void Context::runQueueJob(std::vector<Task>&& tasks)
	{
		// Stack-allocated counter: safe because runQueueJob blocks (via the
		// spin loop below) until every task has decremented it to zero, so
		// the counter is always alive when workers access it.
		std::atomic<size_t> remaining(tasks.size());
		std::vector<Task> wrapped;
		wrapped.reserve(tasks.size());
		for (auto& t : tasks)
		{
			auto wrappedTask = [&remaining, t = std::move(t)]()
			{
				t();
				remaining.fetch_sub(1, std::memory_order_release);
			};
			wrapped.emplace_back(std::move(wrappedTask));
		}
		m_workerPool.submitBatch(std::move(wrapped));

		// Caller participates by stealing work while waiting
		Task stolen;
		while (remaining.load(std::memory_order_acquire) > 0)
		{
			if (m_workerPool.tryStealAny(&stolen))
			{
				stolen();
				stolen = nullptr;
			}
			else
			{
				std::this_thread::yield();
			}
		}
	}
} // namespace multi
