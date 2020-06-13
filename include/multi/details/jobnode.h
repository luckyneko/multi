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
	class Context;

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

		// Run JobNode
		// If context is nullptr then it will run single threaded
		// @return next node that needs to run or parent if none.
		JobNode* run(Context* context = nullptr);

	private:
		enum class State
		{
			none,
			running,
			waiting,
			complete
		};

		JobNode* m_parent;
		Task m_func;
		std::atomic<int> m_numChildren;
		std::atomic<State> m_state;
		JobNode* m_next;
	};
} // namespace multi

#endif // _MULTI_JOBNODE_H_