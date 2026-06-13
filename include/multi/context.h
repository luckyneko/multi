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

namespace multi
{
	/**
	 * @brief Owns a worker pool and implements the dispatch primitives.
	 *
	 * The process-global instance is reached through the free functions in
	 * multi.h; construct a local Context directly to run an isolated pool.
	 */
	class Context
	{
	public:
		Context() = default;
		Context(const Context&) = delete;
		~Context() = default;

		/// Start worker threads. Calling start() on a running pool is undefined
		/// — call stop() first. Passing 0 leaves the pool inactive, so every
		/// dispatch primitive runs inline on the caller.
		/// @param threadCount Worker count; -1 (default) uses
		///        hardware_concurrency() - 1.
		void start(int threadCount = -1);

		/// Stop all workers, draining in-flight tasks first. Blocks until every
		/// worker has exited. No-op on an inactive pool; the pool can be
		/// start()ed again afterwards.
		void stop();

		/// @return Number of running worker threads; 0 when the pool is inactive.
		size_t threadCount() const;

		/// Launch @p f onto the pool.
		/// @tparam F Nullary callable.
		/// @return Handle<R> to wait on, where R is f's return type (void for
		///         value-less tasks).
		template <class F>
		auto async(F&& f);

		/// Variadic fan-out: launch a heterogeneous pack of functors (two or
		/// more), each dispatched via the single-arg async(), returning their
		/// Handles as a tuple (element i typed on fs[i]'s invoke_result). Use
		/// when you need per-task observation; prefer parallel() for
		/// fire-and-block siblings with no result. Each task lives in its own
		/// AsyncJob, so sibling exceptions are isolated per handle.
		template <typename F0, typename F1, typename... Fs>
		auto async(F0&& f0, F1&& f1, Fs&&... fs);

		/// Run a pack of tasks in parallel and block until all complete.
		template <typename... TASKS>
		void parallel(TASKS&&... tasks);

		/// Launch one task per item in [begin, end).
		/// @tparam FUNC Callable as void(T) or void(T&).
		template <typename ITER, typename FUNC>
		void each(ITER begin, ITER end, FUNC&& func);

		/// Chunked overload: distribute the items across @p taskCount tasks.
		template <typename ITER, typename FUNC>
		void each(size_t taskCount, ITER begin, ITER end, FUNC&& func);

		/// Range-based overload: iterate a whole container, one task per item.
		/// Equivalent to each(std::begin(c), std::end(c), func) and mirrors the
		/// `for (auto item : c)` shape.
		template <typename CONTAINER, typename FUNC>
		void each(CONTAINER&& c, FUNC&& func);

		/// Chunked range-based overload: distribute @p c across @p taskCount tasks.
		template <typename CONTAINER, typename FUNC>
		void each(size_t taskCount, CONTAINER&& c, FUNC&& func);

		/// Launch one task per index in [begin, end), stepping by 1.
		/// @tparam IDX Signed arithmetic type.
		/// @tparam FUNC Callable as void(IDX).
		template <typename IDX, typename FUNC>
		void range(IDX begin, IDX end, FUNC&& func);

		/// Step overload.
		template <typename IDX, typename FUNC>
		void range(IDX begin, IDX end, IDX step, FUNC&& func);

		/// Chunked overload: distribute the indices across @p taskCount tasks.
		template <typename IDX, typename FUNC>
		void range(size_t taskCount, IDX begin, IDX end, IDX step, FUNC&& func);

		/// Block (participating in stealing) until every handle in the pack
		/// completes. Each Hs is a Handle<T> (types may differ). Does not
		/// rethrow — observe each handle afterwards. Empty pack is a no-op.
		template <class... Hs>
		void waitAll(const Hs&... hs);

		/// Tuple overload, pairing with the variadic async()'s return.
		template <class... Ts>
		void waitAll(const std::tuple<Handle<Ts>...>& tup);

		/// Block until at least one handle in the pack completes.
		/// @return Zero-based source-order index of the first completed handle.
		///         Requires a non-empty pack (static_assert).
		template <class... Hs>
		std::size_t waitAny(const Hs&... hs);

		/// Tuple overload.
		template <class... Ts>
		std::size_t waitAny(const std::tuple<Handle<Ts>...>& tup);

		/// Block the calling thread until @p pred returns true, helping drain the
		/// pool via work-stealing meanwhile. The primitive behind waitAll /
		/// waitAny; also useful directly when the condition isn't a Handle
		/// (atomic counters, external events).
		template <class Pred>
		void waitUntil(Pred&& pred);

	private:
		/// Dispatch a Job, blocking until done and rethrowing the first captured
		/// exception. Paths by taskCount(): 0 = no-op, 1 = inline on the caller,
		/// >1 = submit a batch and spin on remaining() while work-stealing.
		/// Templated on the concrete subclass so run(i) resolves without a vtable
		/// hop. AsyncJob does not go through this entry.
		template <class JobT>
		void runQueueJob(JobT& job);

		/// Run one stolen task if any worker has work.
		/// @return True if a task was popped and executed (exceptions swallowed).
		bool tryRunSteal();

	private:
		details::WorkerPool m_workerPool;
	};
} // namespace multi

#include "multi/details/context.inl"
