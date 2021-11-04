/*
 *  Created by LuckyNeko on 02/10/2021.
 *  Copyright 2021 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#ifndef _MULTI_CONTEXT_H_
#define _MULTI_CONTEXT_H_

#include "multi/details/threadpool.h"
#include "multi/handle.h"
#include "multi/task.h"

namespace multi
{
	/*
	 * Context
	 * Holds multi-wide state.
	 */
	class Context
	{
	public:
		Context() = default;
		Context(const Context&) = delete;
		~Context() = default;

		void start(size_t threadCount);
		void stop();
		size_t threadCount() const;

		// Launch task onto a thread
		// @return Handle to wait on parallel task.
		Handle async(Task&& task);

		// Launch parallel tasks
		template <typename... TASKS>
		void parallel(TASKS... tasks);

		// Launch task for each item
		// ITER is an iterator
		// FUNC is function void(T) or void(T&)
		template <typename ITER, typename FUNC>
		void each(ITER begin, ITER end, FUNC&& func);
		template <typename ITER, typename FUNC>
		void each(size_t taskCount, ITER begin, ITER end, FUNC&& func);

		// Launch task for each idx with step from begin < end
		// IDX is a POD-type
		// FUNC is void(IDX)
		template <typename IDX, typename FUNC>
		void range(IDX begin, IDX end, IDX step, FUNC&& func);
		template <typename IDX, typename FUNC>
		void range(size_t taskCount, IDX begin, IDX end, IDX step, FUNC&& func);

	private:
		template <typename... TASKS>
		void appendTasks(std::vector<Task>& taskList, Task&& task, TASKS... tasks) const;
		inline void appendTasks(std::vector<Task>& taskList, Task&& task) const;

		void runQueueJob(std::vector<Task>&& tasks);

	private:
		ThreadPool m_threadPool;
	};
} // namespace multi

#include "multi/details/context.inl"

#endif // _MULTI_CONTEXT_H_