#include "multi/details/jobqueue.h"

namespace multi
{
	void JobQueue::push(const Job& jb)
	{
		std::lock_guard<std::mutex> lk(m_lock);
		m_queue.push(jb);
	}

	bool JobQueue::pop(Job* jb)
	{
		std::lock_guard<std::mutex> lk(m_lock);
		while (!m_queue.empty())
		{
			auto& front = m_queue.front();
			if (front.hasWork())
			{
				*jb = m_queue.front();
				return true;
			}
			m_queue.pop();
		}
		return false;
	}
} // namespace multi