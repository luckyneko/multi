/*
 *  Created by LuckyNeko on 15/03/2020.
 *  Copyright 2020 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "multi/context.h"

#include <atomic>
#include <memory>
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
		m_workerPool.submit([task, promise]()
							{
								task();
								promise->set_value();
							});
		return Handle(std::move(hdl));
	}

	void Context::runQueueJob(std::vector<Task>&& tasks)
	{
		auto remaining = std::make_shared<std::atomic<size_t>>(tasks.size());

		std::vector<Task> wrapped;
		wrapped.reserve(tasks.size());
		for (auto& t : tasks)
		{
			wrapped.emplace_back([remaining, t = std::move(t)]()
								 {
									 t();
									 remaining->fetch_sub(1, std::memory_order_release);
								 });
		}

		m_workerPool.submitBatch(std::move(wrapped));

		// Caller participates by stealing work while waiting
		Task stolen;
		while (remaining->load(std::memory_order_acquire) > 0)
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