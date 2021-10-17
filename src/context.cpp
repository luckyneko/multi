/*
 *  Created by LuckyNeko on 15/03/2020.
 *  Copyright 2020 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "multi/context.h"

namespace multi
{
	void Context::start(size_t threadCount)
	{
		m_threadPool.start(threadCount);
	}

	void Context::stop()
	{
		m_threadPool.stop();
	}

	size_t Context::threadCount() const
	{
		return m_threadPool.threadCount();
	}

	Handle Context::async(Task&& task)
	{
		auto promise = std::make_shared<std::promise<void>>();
		auto hdl = promise->get_future();
		std::vector<Task> tasks;
		tasks.reserve(1);
		tasks.emplace_back([task, promise]()
						   {
							   task();
							   promise->set_value();
						   });
		m_threadPool.queue(std::move(tasks));
		return Handle(std::move(hdl));
	}

	void Context::runQueueJob(std::vector<Task>&& tasks)
	{
		Job jb(std::move(tasks));
		m_threadPool.queue(jb);
		jb.waitRun();
	}
} // namespace multi