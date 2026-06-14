/*
 *  Created by LuckyNeko on 17/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "multi/details/workstealdeque.h"

#include <cassert>

namespace multi::details
{
	bool WorkStealDeque::tryPushLocal(Task&& task)
	{
		// Both pushes move only on success, so if local is full `task` is still
		// intact for the overflow fallback to move (see ChaseLevDeque /
		// MpmcQueue).
		if (m_local.tryPushBottom(task))
			return true;
		return m_overflow.tryPush(task);
	}

	bool WorkStealDeque::tryPushRemote(Task&& task)
	{
		return m_overflow.tryPush(task);
	}

	bool WorkStealDeque::pop(Task* task)
	{
		if (m_local.sizeHint() < REFILL_LOW)
			refillFromOverflow();

		if (m_local.tryPopBottom(task))
			return true;

		return m_overflow.tryPop(task);
	}

	bool WorkStealDeque::steal(Task* task)
	{
		if (m_local.tryStealTop(task))
			return true;
		return m_overflow.tryPop(task);
	}

	std::size_t WorkStealDeque::sizeHint() const
	{
		return m_local.sizeHint() + m_overflow.sizeHint();
	}

	bool WorkStealDeque::empty() const
	{
		return m_local.sizeHint() == 0 && m_overflow.sizeHint() == 0;
	}

	void WorkStealDeque::refillFromOverflow()
	{
		Task t;
		for (std::size_t i = 0; i < REFILL_BATCH; ++i)
		{
			// Owner is the sole writer to local's bottom and stealers only
			// shrink it, so observing space here guarantees the tryPushBottom
			// below succeeds — no risk of popping a task we can't place.
			if (m_local.sizeHint() >= LOCAL_CAP)
				return;
			if (!m_overflow.tryPop(&t))
				return;
			[[maybe_unused]] const bool pushed = m_local.tryPushBottom(t);
			assert(pushed && "owner observed space, so local push cannot fail");
		}
	}
} // namespace multi::details
