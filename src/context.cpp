/*
 *  Created by LuckyNeko on 15/03/2020.
 *  Copyright 2020 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "multi/context.h"
#include "multi/details/recipejob.h"
#include "multi/recipe.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <thread>
#include <utility>

namespace multi::details
{
	void WaitBackoff::checkSpinTime()
	{
		// The streak's clock starts at its first check, WAIT_CLOCK_INTERVAL
		// attempts in, so a short wait never reads the clock at all.
		const auto now = std::chrono::steady_clock::now();
		if (m_idlePolls == WAIT_CLOCK_INTERVAL)
			m_idleSince = now;
		else if (now - m_idleSince >= WAIT_SPIN_TIME)
			m_sleep = WAIT_SLEEP_MIN;
	}

	void WaitBackoff::sleep()
	{
		std::this_thread::sleep_for(m_sleep);
		m_sleep = std::min(m_sleep * 2, std::chrono::microseconds(WAIT_SLEEP_MAX));
	}
} // namespace multi::details

namespace multi
{
	bool Context::start(int threadCount)
	{
		return m_workerPool.start(threadCount);
	}

	void Context::stop()
	{
		m_workerPool.stop();
	}

	size_t Context::threadCount() const
	{
		return m_workerPool.threadCount();
	}

	RecipeHandle Context::async(Recipe&& recipe)
	{
		auto job = std::make_shared<details::RecipeJob>(
			std::move(recipe),
			[this](details::Task&& task)
			{ m_workerPool.submit(std::move(task)); });
		auto future = job->getFuture();
		job->start();
		return RecipeHandle(Handle<>(std::move(future)), std::move(job));
	}

	bool Context::tryRunSteal()
	{
		details::Task stolen;
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
} // namespace multi
