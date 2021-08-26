/*
 *  Created by LuckyNeko on 20/08/2021.
 *  Copyright 2021 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#ifndef _MULTI_ICONTEXT_H_
#define _MULTI_ICONTEXT_H_

#include <utility>

namespace multi
{
    class Task;
	class JobNode;

	/*
	* iContext
	* A generic interface to Allocate & Queue JobNodes
	*/
	class iContext
	{
	public:
		virtual ~iContext() = default;

        template <typename... TASKS>
		JobNode* allocJobNode(JobNode* parent, Task&& task, TASKS... tasks)
        {
            auto next = allocJobNode(parent, std::forward<TASKS>(tasks)...);
            return allocJobNode(parent, std::forward<Task>(task), next);
        }

		virtual JobNode* allocJobNode(JobNode* parent, Task&& task, JobNode* next = nullptr) = 0;
		virtual void deallocJobNode(JobNode* node) = 0;
		virtual void queueJobNode(JobNode* node) = 0;
	};
} // namespace multi

#endif // _MULTI_ICONTEXT_H_