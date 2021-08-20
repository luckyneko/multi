/*
 *  Created by LuckyNeko on 15/03/2020.
 *  Copyright 2020 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#ifndef _MULTI_JOBNODE_H_
#define _MULTI_JOBNODE_H_

#include "multi/task.h"
#include <atomic>

namespace multi
{
	class Job;
	class iContext;

	/*
	* JobNode
	* A sequential linked-list like job tree.
	* Can launch parallel children, or sequential next nodes.
	*/
	class JobNode
	{
	public:
		JobNode(JobNode* parent, Task&& task, JobNode* next = nullptr);
		JobNode(const JobNode&) = delete;
		~JobNode() = default;

		// Run Job
		void runJob(iContext* queue);

		// Run JobNode
		// If context is nullptr then it will run single threaded
		// @return next node that needs to run or parent if none.
		JobNode* runNode(iContext* queue);

		// Set Next JobNode
		// Can only run when no next is set & state is none.
		void setNext(JobNode* next);

		inline JobNode* getParent() const { return m_parent; }

	private:
		enum class State
		{
			none,
			running,
			waiting,
			complete
		};

		JobNode* m_parent;
		Task m_task;
		std::atomic<int> m_numChildren;
		std::atomic<State> m_state;
		JobNode* m_next;
	};
} // namespace multi

#endif // _MULTI_JOBNODE_H_