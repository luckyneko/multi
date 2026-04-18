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
		task = [task = std::move(task), promise]()
		{
			task();
			promise->set_value();
		};
		m_workerPool.submit(std::move(task));
		return Handle(std::move(hdl));
	}

	void Context::runQueueJob(std::vector<Task>&& tasks)
	{
		// Insert counter into tasks
		std::atomic<size_t> remaining(tasks.size());
		for (size_t i = 0; i < tasks.size(); ++i)
		{
			tasks[i] = [&remaining, task = std::move(tasks[i])]()
			{
				task();
				remaining.fetch_sub(1, std::memory_order_release);
			};
		}
		m_workerPool.submitBatch(std::move(tasks));

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
