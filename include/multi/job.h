/*
 *  Created by LuckyNeko on 13/05/2020.
 *  Copyright 2013 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#ifndef _MULTI_JOB_H_
#define _MULTI_JOB_H_

#include "multi/order.h"
#include "multi/task.h"
#include "multi/details/icontext.h"

namespace multi
{
	class JobNode;

	/*
	 * Job
	 * Handles adding children tasks to a job
	 */
	class Job
	{
	public:
		Job(iContext* context, JobNode* parent = nullptr);
		Job(const Job&) = default;
		~Job() = default;

		// Add subtasks to job
		template <typename... FUNCS>
		void add(Order order, FUNCS... funcs);

		// Add subtask for each item in iterable
		template <typename ITER, typename FUNC>
		void each(Order order, ITER begin, ITER end, FUNC&& func);

		// Add subtask for a range of values
		template <typename ITER, typename FUNC>
		void range(Order order, ITER begin, ITER end, ITER step, FUNC&& func);

		// Add subtask to sequentially run function until predicate returns true
		template <typename FUNC, typename PRED>
		void until(PRED&& pred, FUNC&& func);

	private:
		JobNode* attachJobNode(JobNode* previous, JobNode* next);
		void addUntil(std::function<bool()> pred, std::function<void(Job)> func);

		template <typename... TASKS>
		void queueTasks(Task&& task, TASKS... tasks);
		inline void queueTasks(Task&& task) { m_context->queueJobNode(m_context->allocJobNode(m_parent, std::move(task))); }

	private:
		iContext* m_context;
		JobNode* m_parent;
	};

#include "multi/details/job.inl"
} // namespace multi

#endif // _MULTI_JOB_H_