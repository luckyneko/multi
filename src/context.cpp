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

		void* memory;
		while (m_memoryPool.pop(&memory))
			delete memory;
	}

	size_t Context::threadCount() const
	{
		return m_threadPool.threadCount();
	}

	Handle Context::async(Task&& task)
	{
		auto promise = std::make_shared<std::promise<void>>();
		auto hdl = promise->get_future();
		auto promiseNode = allocJobNode(nullptr, [promise](Job&) { promise->set_value(); });
		queueJobNode(allocJobNode(nullptr, std::move(task), promiseNode));
		return std::move(hdl);
	}

	void Context::queueJobNode(JobNode* node)
	{
		m_threadPool.queue([this, node]() {
			auto activeNode = node;
			while (activeNode != nullptr)
			{
				auto oldNode = activeNode;
				activeNode = activeNode->run(this);
				if (activeNode)
					deallocJobNode(oldNode);
			}
		});
	}

	JobNode* Context::allocJobNode(JobNode* parent, Task&& task, JobNode* next)
	{
		void* memory;
		if (m_memoryPool.pop(&memory))
			return new (memory) JobNode(parent, std::forward<Task>(task), next);
		return new JobNode(parent, std::forward<Task>(task), next);
	}

	void Context::deallocJobNode(JobNode* node)
	{
		node->~JobNode();
		m_memoryPool.push(node);
	}
} // namespace multi