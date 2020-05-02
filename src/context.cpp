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
	Context::Context()
		: m_threadPool()
	{
	}

	Context::~Context()
	{
	}

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

	Handle Context::async(Function&& func)
	{
		auto promise = std::make_shared<std::promise<void>>();
		auto hdl = promise->get_future();
		queueJobNode(allocJobNode(nullptr, std::move(func), [promise]() { promise->set_value(); }));
		return hdl;
	}

	JobNode* Context::allocJobNode(JobNode* parent, Function&& func, JobNode* next)
	{
		return new JobNode(parent, std::move(func), next);
	}

	thread_local JobNode* s_activeJob = nullptr;
	JobNode*& Context::activeJobNode()
	{
		return s_activeJob;
	}

	void Context::queueJobNode(JobNode* node)
	{
		m_threadPool.queue([this, node]() {
			auto& activeNode = activeJobNode();
			activeNode = node;
			while (activeNode != nullptr)
				activeNode = activeNode->run();
		});
	}
} // namespace multi