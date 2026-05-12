/*
 *  Created by LuckyNeko on 02/10/2021.
 *  Copyright 2021 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "multi/details/job.h"
#include "multi/details/task.h"
#include "multi/details/workerpool.h"
#include "multi/handle.h"

#include <type_traits>

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
		template <class F>
		Handle async(F&& f);

		// Launch parallel tasks
		template <typename... TASKS>
		void parallel(TASKS&&... tasks);

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
		void range(IDX begin, IDX end, FUNC&& func);
		template <typename IDX, typename FUNC>
		void range(IDX begin, IDX end, IDX step, FUNC&& func);
		template <typename IDX, typename FUNC>
		void range(size_t taskCount, IDX begin, IDX end, IDX step, FUNC&& func);

		// Try run a stolen task
		bool tryRunSteal();

	private:
		// Dispatch a Job. Three paths:
		//   - taskCount() == 0: no-op (invalid inputs or empty range).
		//   - taskCount() == 1: run inline on the caller, no submission.
		//   - taskCount() >  1: submit a batch of count wrappers and spin on
		//     remaining() while participating via tryRunSteal().
		// Rethrows the first captured exception on completion. Templated on
		// the concrete subclass so `job.run(i)` resolves directly without a
		// vtable hop. AsyncJob doesn't go through this entry — it's
		// heap-allocated and dispatched fire-and-forget via WorkerPool::submit.
		template <class JobT>
		void runQueueJob(JobT& job);

	private:
		WorkerPool m_workerPool;
	};
} // namespace multi

#include "multi/details/context.inl"