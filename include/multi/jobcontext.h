/*
 *  Created by LuckyNeko on 13/05/2020.
 *  Copyright 2013 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#ifndef _MULTI_JOBCONTEXT_H_
#define _MULTI_JOBCONTEXT_H_

#include "multi/details/meta.h"
#include "multi/order.h"
#include "multi/task.h"

namespace multi
{
	class Context;
	class Job;
	class JobNode;

	class JobContext
	{
	public:
		JobContext(Context* context = nullptr, JobNode* parent = nullptr);
		JobContext(const JobContext&) = delete;

		template <typename... FUNCS>
		void add(Order order, FUNCS... funcs);
		void add(Job&& job);

		template <typename ITER, typename FUNC>
		void each(Order order, ITER begin, ITER end, FUNC&& func);

		template <typename ITER, typename FUNC>
		void range(Order order, ITER begin, ITER end, ITER step, FUNC&& func);

		template <typename FUNC, typename PRED>
		void until(PRED&& pred, FUNC&& func);

	private:
		template <typename... TASKS>
		JobNode* allocJobNode(Task&& task, TASKS... tasks)
		{
			return allocJobNode(
				std::forward<Task>(task),
				allocJobNode(std::forward<TASKS>(tasks)...));
		}
		JobNode* allocJobNode(Task&& task, JobNode* next = nullptr);

		template <typename... TASKS>
		void runTasks(TASKS... tasks)
		{
			doOnAll([this](Task&& task) { task(*this); }, std::forward<TASKS>(tasks)...);
		}

		template <typename... TASKS>
		void queueTasks(TASKS... tasks)
		{
			doOnAll([this](Task&& task) {
				queueJobNode(allocJobNode(std::forward<Task>(task)));
			},
					std::forward<TASKS>(tasks)...);
		}

		void queueJobNode(JobNode* node);

	private:
		Context* m_context;
		JobNode* m_parent;
	};

#include "multi/details/jobcontext.inl"
} // namespace multi

#endif // _MULTI_JOBCONTEXT_H_