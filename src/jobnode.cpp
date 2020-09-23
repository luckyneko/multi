/*
 *  Created by LuckyNeko on 15/03/2020.
 *  Copyright 2020 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "multi/details/jobnode.h"
#include "multi/job.h"
#include <assert.h>

namespace multi
{
	JobNode::JobNode(JobNode* parent, Task&& task, JobNode* next)
		: m_parent(parent)
		, m_task(std::move(task))
		, m_numChildren(0)
		, m_state(State::none)
		, m_next(next)
	{
		if (m_parent)
			m_parent->m_numChildren++;
	}

	void JobNode::runJob(Context* context)
	{
		JobNode* active = this;
		while (active)
			active = active->runNode(context);
	}

	JobNode* JobNode::runNode(Context* context)
	{
		JobNode* next = nullptr;
		assert(m_numChildren >= 0);

		// Run Node
		State noState = State::none;
		if (m_state.compare_exchange_strong(noState, State::running))
		{
			Job jb(context, this);
			m_task.run(jb);
			m_state = State::waiting;
		}

		// Complete Node when all children complete
		State waitState = State::waiting;
		if (m_numChildren == 0 && m_state.compare_exchange_strong(waitState, State::complete))
		{
			if (m_parent)
				m_parent->m_numChildren--;
			next = (m_next != nullptr) ? m_next : m_parent;
			delete this;
		}

		return next;
	}
} // namespace multi