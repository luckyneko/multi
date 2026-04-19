/*
 *  Created by LuckyNeko on 15/03/2020.
 *  Copyright 2020 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "multi/context.h"

#include <atomic>
#include <exception>
#include <memory>
#include <mutex>
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
			try
			{
				task();
				promise->set_value();
			}
			catch (...)
			{
				promise->set_exception(std::current_exception());
			}
		};
		m_workerPool.submit(std::move(task));
		return Handle(std::move(hdl), this);
	}

	// Try run a stolen task
	bool Context::tryRunSteal()
	{
		Task stolen;
		if (m_workerPool.tryStealAny(&stolen))
		{
			try
			{
				stolen();
			}
			catch (...)
			{
			}
			return true;
		}
		return false;
	}

	void Context::runQueueJob(std::vector<Task>&& tasks)
	{
		// Insert counter + exception capture into tasks. The first task to
		// throw wins; later exceptions are discarded (matches std::async's
		// "first failure propagates" behaviour for parallel work).
		std::atomic<size_t> remaining(tasks.size());
		auto firstException = std::make_shared<std::exception_ptr>();
		auto excOnce = std::make_shared<std::once_flag>();
		for (size_t i = 0; i < tasks.size(); ++i)
		{
			tasks[i] = [&remaining, firstException, excOnce, task = std::move(tasks[i])]()
			{
				try
				{
					task();
				}
				catch (...)
				{
					std::call_once(*excOnce, [&]()
								   { *firstException = std::current_exception(); });
				}
				remaining.fetch_sub(1, std::memory_order_release);
			};
		}
		m_workerPool.submitBatch(std::move(tasks));

		// Caller participates by stealing work while waiting
		while (remaining.load(std::memory_order_acquire) > 0)
		{
			if (!tryRunSteal())
				std::this_thread::yield();
		}

		if (*firstException)
			std::rethrow_exception(*firstException);
	}
} // namespace multi
