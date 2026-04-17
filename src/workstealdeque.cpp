/*
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "multi/details/workstealdeque.h"

#include <algorithm>

namespace multi
{
	void WorkStealDeque::push(Task&& task)
	{
		std::lock_guard<std::mutex> lk(m_mutex);
		m_deque.push_back(std::move(task));
	}

	bool WorkStealDeque::pop(Task* task)
	{
		std::lock_guard<std::mutex> lk(m_mutex);
		if (m_deque.empty())
			return false;
		*task = std::move(m_deque.back());
		m_deque.pop_back();
		return true;
	}

	bool WorkStealDeque::steal(Task* task)
	{
		std::lock_guard<std::mutex> lk(m_mutex);
		if (m_deque.empty())
			return false;
		*task = std::move(m_deque.front());
		m_deque.pop_front();
		return true;
	}

	size_t WorkStealDeque::sizeHint() const
	{
		std::lock_guard<std::mutex> lk(m_mutex);
		return m_deque.size();
	}

	bool WorkStealDeque::empty() const
	{
		std::lock_guard<std::mutex> lk(m_mutex);
		return m_deque.empty();
	}
} // namespace multi
