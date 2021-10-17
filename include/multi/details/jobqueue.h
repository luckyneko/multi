/*
 *  Created by LuckyNeko on 11/03/2020.
 *  Copyright 2020 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#ifndef _MULTI_JOBQUEUE_H_
#define _MULTI_JOBQUEUE_H_

#include "multi/details/job.h"
#include <mutex>
#include <queue>

namespace multi
{
	/*
	 * JobQueue
	 * Queue of Jobs
	 */
	class JobQueue
	{
	public:
		inline void push(const Job& jb)
		{
			std::lock_guard<std::mutex> lk(m_lock);
			m_queue.push(jb);
		}

		bool pop(Job* jb)
		{
			std::lock_guard<std::mutex> lk(m_lock);
			while (!m_queue.empty())
			{
				auto& front = m_queue.front();
				if (!front.hasWork())
				{
					m_queue.pop();
				}
				else
				{
					*jb = m_queue.front();
					return true;
				}
			}
			return false;
		}

	private:
		std::queue<Job> m_queue;
		std::mutex m_lock;
	};
} // namespace multi

#endif // _MULTI_JOBQUEUE_H_