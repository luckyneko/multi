/*
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "multi/details/workstealdeque.h"

namespace multi
{
	bool WorkStealDeque::tryPushLocal(Task&& task)
	{
		if (m_local.tryPushBottom(std::move(task)))
			return true;
		return m_overflow.tryPush(std::move(task));
	}

	bool WorkStealDeque::tryPushRemote(Task&& task)
	{
		return m_overflow.tryPush(std::move(task));
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
		if (m_local.trySteal(task))
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
			// shrink local. If we observe space here, the subsequent
			// tryPushBottom is guaranteed to succeed — no risk of popping a
			// task we then can't place.
			if (m_local.sizeHint() >= LOCAL_CAP)
				return;
			if (!m_overflow.tryPop(&t))
				return;
			m_local.tryPushBottom(std::move(t));
		}
	}
} // namespace multi
