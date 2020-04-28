/*
 *  Created by LuckyNeko on 15/03/2020.
 *  Copyright 2013 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#ifndef _MULTI_JOBNODE_H_
#define _MULTI_JOBNODE_H_

#include "multi/details/function.h"
#include <atomic>

namespace multi
{
	class JobNode
	{
	public:
		JobNode(multi::JobNode* parent, multi::Function&& task, multi::JobNode* next = nullptr);
		JobNode(const JobNode&) = delete;
		~JobNode() = default;

		multi::JobNode* run();

	private:
		enum State
		{
			NONE,
			RUNNING,
			WAITING,
			COMPLETE
		};

		multi::JobNode* m_parent;
		multi::Function m_func;
		std::atomic<int> m_numChildren;
		std::atomic<State> m_state;
		multi::JobNode* m_next;
	};
} // namespace multi

#endif // _MULTI_JOBNODE_H_