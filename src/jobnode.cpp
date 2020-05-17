/*
 *  Created by LuckyNeko on 15/03/2020.
 *  Copyright 2020 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "multi/details/jobnode.h"
#include "multi/jobcontext.h"
#include <assert.h>

namespace multi
{
	JobNode::JobNode(JobNode* parent, Task&& task, JobNode* next)
		: m_parent(parent)
		, m_func(std::move(task))
		, m_numChildren(0)
		, m_state(State::NONE)
		, m_next(next)
	{
		if (m_parent)
			m_parent->m_numChildren++;
	}

	JobNode* JobNode::run(Context* context)
	{
		JobNode* next = nullptr;
		assert(m_numChildren >= 0);

		// Run Node
		State noState = State::NONE;
		if (m_state.compare_exchange_strong(noState, State::RUNNING))
		{
			JobContext jobContext(context, this);
			if (m_func)
				m_func(jobContext);
			m_func = nullptr;
			m_state = State::WAITING;
		}

		// Complete Node when all children complete
		State waitState = State::WAITING;
		if (m_numChildren == 0 && m_state.compare_exchange_strong(waitState, State::COMPLETE))
		{
			if (m_parent)
				m_parent->m_numChildren--;
			next = (m_next != nullptr) ? m_next : m_parent;
			delete this;
		}

		return next;
	}
} // namespace multi