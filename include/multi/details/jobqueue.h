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
		// Push Job into queue
		void push(const Job& jb);

		// Pop first Job with work
		// Returns true if found
		bool pop(Job* jb);

	private:
		std::queue<Job> m_queue;
		std::mutex m_lock;
	};
} // namespace multi

#endif // _MULTI_JOBQUEUE_H_