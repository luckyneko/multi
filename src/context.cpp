/*
 *  Created by LuckyNeko on 15/03/2020.
 *  Copyright 2020 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "multi/context.h"
#include "multi/details/jobnode.h"
#include "multi/details/localjobqueue.h"

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
		auto promiseNode = allocJobNode(nullptr, [promise](Job) { promise->set_value(); });
		auto newJob = allocJobNode(nullptr, std::move(task), promiseNode);
		if (m_threadPool.isActive())
		{
			queueJobNode(newJob);
		}
		else
		{
			LocalJobQueue queue;
			queue.queueJobNode(newJob);
			queue.run();
		}
		return Handle(std::move(hdl));
	}

	JobNode* Context::allocJobNode(JobNode* parent, Task&& task, JobNode* next)
	{
		return new JobNode(parent, std::move(task), next);
	}

	void Context::queueJobNode(JobNode* node)
	{
		m_threadPool.queue(std::bind(&JobNode::runJob, node, this));
	}
} // namespace multi