/*
 *  Created by LuckyNeko on 15/03/2020.
 *  Copyright 2020 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "multi/context.h"
#include "multi/details/jobnode.h"

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
		auto promiseNode = new JobNode(nullptr, [promise](Job) { promise->set_value(); });
		queueJobNode(new JobNode(nullptr, std::move(task), promiseNode));
		return hdl;
	}

	void Context::queueJobNode(JobNode* node)
	{
		m_threadPool.queue(std::bind(&JobNode::runJob, node, this));
	}
} // namespace multi