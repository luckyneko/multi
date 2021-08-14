/*
 *  Created by LuckyNeko on 14/08/2021.
 *  Copyright 2021 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "multi/details/jobqueue.h"
#include "multi/details/jobnode.h"

namespace multi
{
	void LocalJobQueue::queueJobNode(JobNode* node)
	{
		m_queue.push(node);
	}

	bool LocalJobQueue::popNode(JobNode** node)
	{
		if (m_queue.empty())
			return false;
		*node = std::move(m_queue.front());
		m_queue.pop();
		return true;
	}

	void LocalJobQueue::run()
	{
		JobNode* node = nullptr;
		while (popNode(&node))
			node->runJob(this);
	}
} // namespace multi