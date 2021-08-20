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

namespace multi
{
	class iJobQueue;
	class JobNode;

	/*
	 * Job
	 * Handles adding children tasks to a job
	 */
	class Job
	{
	public:
		Job(iJobQueue* queue, JobNode* parent = nullptr);
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
		template <typename... TASKS>
		JobNode* allocJobNode(Task&& task, TASKS... tasks);
		JobNode* allocJobNode(Task&& task, JobNode* next = nullptr);

		JobNode* attachJobNode(JobNode* previous, JobNode* next);
		void addUntil(std::function<bool()> pred, std::function<void(Job)> func);

		template <typename... TASKS>
		void queueTasks(Task&& task, TASKS... tasks);
		inline void queueTasks(Task&& task) { queueJobNode(allocJobNode(std::move(task))); }
		void queueJobNode(JobNode* node);

	private:
		iJobQueue* m_queue;
		JobNode* m_parent;
	};

#include "multi/details/job.inl"
} // namespace multi

#endif // _MULTI_JOB_H_