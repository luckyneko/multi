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
		void parallel(TASKS... tasks)
		{
			const size_t count = sizeof...(TASKS);
			std::vector<Task> taskList;
			taskList.reserve(count);
			appendTasks(taskList, std::forward<TASKS>(tasks)...);
			runQueueJob(std::move(taskList));
		}

		// Launch task for each item
		template <typename ITER, typename FUNC>
		void each(ITER begin, ITER end, FUNC&& func)
		{
			std::vector<Task> taskList;
			taskList.reserve(end - begin);
			for (ITER it = begin; it != end; ++it)
				taskList.push_back(std::bind(func, *it));
			runQueueJob(std::move(taskList));
		}

		// Launch task for each idx with step
		template <typename IDX, typename FUNC>
		void range(IDX begin, IDX end, IDX step, FUNC&& func)
		{
			std::vector<Task> taskList;
			taskList.reserve((end - begin) / step);
			for (IDX i = begin; i < end; i += step)
				taskList.push_back(std::bind(func, i));
			runQueueJob(std::move(taskList));
		}

	private:
		template <typename... TASKS>
		void appendTasks(std::vector<Task>& taskList, Task&& task, TASKS... tasks)
		{
			taskList.push_back(std::move(task));
			appendTasks(taskList, std::forward<TASKS>(tasks)...);
		}
		inline void appendTasks(std::vector<Task>& taskList, Task&& task)
		{
			taskList.push_back(std::move(task));
		}

		void runQueueJob(std::vector<Task>&& tasks);

	private:
		ThreadPool m_threadPool;
	};
} // namespace multi

#endif // _MULTI_CONTEXT_H_