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
#include <mutex>
#include <thread>
#include <utility>

namespace multi
{
	namespace
	{
		// Shared state for a batch job. Lives on the caller's stack in
		// runQueueJob; tasks reference it by raw pointer. Safe because the
		// caller blocks until remaining == 0.
		struct Job
		{
			std::atomic<size_t> remaining;
			std::once_flag excOnce;
			std::exception_ptr firstException;
			std::vector<Task> tasks;

			explicit Job(std::vector<Task>&& t)
				: remaining(t.size()), tasks(std::move(t))
			{
			}

			void run(size_t i) noexcept
			{
				try
				{
					tasks[i]();
				}
				catch (...)
				{
					std::call_once(excOnce, [&]()
								   { firstException = std::current_exception(); });
				}
				remaining.fetch_sub(1, std::memory_order_release);
			}
		};
	} // namespace

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
		if (tasks.empty())
			return;

		// Job lives on our stack; wrappers capture [&job, i] which SBO-fits
		// in std::function and removes the second heap allocation that the
		// previous per-task wrapping cost.
		Job job(std::move(tasks));
		const size_t count = job.tasks.size();

		std::vector<Task> wrappers;
		wrappers.reserve(count);
		for (size_t i = 0; i < count; ++i)
			wrappers.emplace_back([&job, i]() { job.run(i); });

		m_workerPool.submitBatch(std::move(wrappers));

		// Caller participates by stealing work while waiting
		while (job.remaining.load(std::memory_order_acquire) > 0)
		{
			if (!tryRunSteal())
				std::this_thread::yield();
		}

		if (job.firstException)
			std::rethrow_exception(job.firstException);
	}
} // namespace multi
