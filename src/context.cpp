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

#include <memory>
#include <thread>
#include <utility>

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
