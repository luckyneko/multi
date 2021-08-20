/*
 *  Created by LuckyNeko on 14/08/2021.
 *  Copyright 2021 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "multi/details/localjobqueue.h"
#include "multi/details/jobnode.h"

namespace multi
{
	void LocalJobQueue::queueJobNode(JobNode* node)
	{
		m_queue.push(node);
	}

	JobNode* LocalJobQueue::allocJobNode(JobNode* parent, Task&& task, JobNode* next)
	{
		return new JobNode(parent, std::move(task), next);
	}

	void LocalJobQueue::run()
	{
		while (!m_queue.empty())
		{
			m_queue.front()->runJob(this);
			m_queue.pop();
		}
	}
} // namespace multi