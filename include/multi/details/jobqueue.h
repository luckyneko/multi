/*
 *  Created by LuckyNeko on 14/08/2021.
 *  Copyright 2021 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#ifndef _MULTI_JOBQUEUE_H_
#define _MULTI_JOBQUEUE_H_

#include <queue>

namespace multi
{
	class JobNode;

	/*
	* iJobQueue
	* A generic interface to Queue JobNodes
	*/
	class iJobQueue
	{
	public:
		virtual ~iJobQueue() = default;

		virtual void queueJobNode(JobNode* node) = 0;
	};

	/*
	* LocalJobQueue
	* A local queue for queueing jobs
	*/
	class LocalJobQueue : public iJobQueue
	{
	public:
		LocalJobQueue() = default;
		virtual ~LocalJobQueue() = default;

		void queueJobNode(JobNode* node) final;

		bool popNode(JobNode** node);

		void run();

	private:
		std::queue<JobNode*> m_queue;
	};
} // namespace multi

#endif // _MULTI_JOBQUEUE_H_