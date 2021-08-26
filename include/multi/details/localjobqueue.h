/*
 *  Created by LuckyNeko on 14/08/2021.
 *  Copyright 2021 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#ifndef _MULTI_LOCALJOBQUEUE_H_
#define _MULTI_LOCALJOBQUEUE_H_

#include "multi/details/icontext.h"
#include <queue>

namespace multi
{
	/*
	* LocalJobQueue
	* A local queue for queueing jobs
	*/
	class LocalJobQueue : public iContext
	{
	public:
		LocalJobQueue() = default;
		virtual ~LocalJobQueue() = default;

		JobNode* allocJobNode(JobNode* parent, Task&& task, JobNode* next = nullptr) final;
		void deallocJobNode(JobNode* node) final;
		void queueJobNode(JobNode* node) final;

		void run();

	private:
		std::queue<JobNode*> m_queue;
	};
} // namespace multi

#endif // _MULTI_LOCALJOBQUEUE_H_