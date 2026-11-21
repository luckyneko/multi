/*
 *  Created by LuckyNeko on 02/10/2021.
 *  Copyright 2021 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "multi/chunkpolicy.h"
#include "multi/details/job.h"
#include "multi/details/task.h"
#include "multi/details/workerpool.h"
#include "multi/handle.h"
#include "multi/recipehandle.h"

#include <chrono>
#include <cstddef>
#include <thread>

namespace multi::details
{
	/*
	 * Waiter backoff, for a thread blocked in Context::waitUntil (and so waitAll /
	 * waitAny) or in a parallel / each / range dispatch. The waiter steals pool
	 * work while it waits. While there is nothing to steal it first spins as it
	 * always has, yielding between steal attempts, for WAIT_SPIN_TIME; after that
	 * it sleeps between attempts, doubling from WAIT_SLEEP_MIN up to
	 * WAIT_SLEEP_MAX. A successful steal goes back to spinning.
	 *
	 * Trade-off: a long wait burns at most about WAIT_SPIN_TIME of a core instead
	 * of the whole wait, and in exchange notices completion, or new work to steal,
	 * up to one sleep late. Work queued onto a waiting worker's own deque can wait
	 * as long, since submitting it wakes only that worker. Waits shorter than
	 * WAIT_SPIN_TIME never sleep: that covers the waits in the benchmarks
	 * (microseconds to a few milliseconds) and a whole 60 Hz frame.
	 *
	 * Worst-case added wake-up latency, once a wait has backed off: WAIT_SLEEP_MAX
	 * plus the OS timer slack on Linux, Android, macOS and iOS (tens of
	 * microseconds). On Windows every sleep rounds up to the system timer tick, so
	 * the cap does not hold there: one tick, ~15.6 ms by default, or 1-2 ms once
	 * the process has called timeBeginPeriod(1). Measured on Windows 11: any
	 * sleep_for from 1 us to 4 ms took ~15.5 ms, and yield() ~0.3 us.
	 */
	inline constexpr std::chrono::milliseconds WAIT_SPIN_TIME{20};
	inline constexpr std::chrono::microseconds WAIT_SLEEP_MIN{50};
	inline constexpr std::chrono::microseconds WAIT_SLEEP_MAX{1000};
	/// Failed steal attempts between clock reads while spinning.
	inline constexpr std::size_t WAIT_CLOCK_INTERVAL = 64;

	/// One waiter's place in the backoff above. Not thread-safe: one per wait.
	class WaitBackoff
	{
	public:
		/// A steal succeeded: spin again.
		void reset() noexcept
		{
			m_idlePolls = 0;
			m_sleep = std::chrono::microseconds::zero();
		}

		/// A steal attempt found nothing: yield while spinning, sleep once backed off.
		void idle()
		{
			if (m_sleep == std::chrono::microseconds::zero())
			{
				std::this_thread::yield();
				if (++m_idlePolls % WAIT_CLOCK_INTERVAL == 0)
					checkSpinTime();
			}
			else
			{
				sleep();
			}
		}

	private:
		void checkSpinTime();
		void sleep();

		std::size_t m_idlePolls = 0;
		std::chrono::steady_clock::time_point m_idleSince;
		std::chrono::microseconds m_sleep = std::chrono::microseconds::zero(); // zero while spinning
	};
} // namespace multi::details

namespace multi
{
	class Recipe;

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
		bool start(int threadCount = -1);

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

		/// Launch a Recipe, consuming its steps.
		RecipeHandle async(Recipe&& recipe);

		/// Run a pack of tasks in parallel and block until all complete.
		template <typename... TASKS>
		void parallel(TASKS&&... tasks);

		/// Launch one task per item in [begin, end).
		/// @tparam FUNC Callable as void(T) or void(T&).
		template <typename ITER, typename FUNC>
		void each(ITER begin, ITER end, FUNC&& func);

		/// Chunked overload: distribute the items across @p chunkPolicy tasks
		/// (an exact count, or multi::Auto / multi::PerItem / multi::Serial).
		template <typename ITER, typename FUNC>
		void each(ChunkPolicy chunkPolicy, ITER begin, ITER end, FUNC&& func);

		/// Range-based overload: iterate a whole container, one task per item.
		/// Equivalent to each(std::begin(c), std::end(c), func) and mirrors the
		/// `for (auto item : c)` shape.
		template <typename CONTAINER, typename FUNC>
		void each(CONTAINER&& c, FUNC&& func);

		/// Chunked range-based overload: distribute @p c across @p chunkPolicy tasks
		/// (an exact count, or multi::Auto / multi::PerItem / multi::Serial).
		template <typename CONTAINER, typename FUNC>
		void each(ChunkPolicy chunkPolicy, CONTAINER&& c, FUNC&& func);

		/// Launch one task per index in [begin, end), stepping by 1.
		/// @tparam IDX Arithmetic type other than bool: signed, unsigned or floating-point.
		/// @tparam FUNC Callable as void(IDX).
		template <typename IDX, typename FUNC>
		void range(IDX begin, IDX end, FUNC&& func);

		/// Step overload. A non-positive @p step dispatches nothing.
		template <typename IDX, typename FUNC>
		void range(IDX begin, IDX end, IDX step, FUNC&& func);

		/// Chunked overload: distribute the indices across @p chunkPolicy tasks
		/// (an exact count, or multi::Auto / multi::PerItem / multi::Serial).
		template <typename IDX, typename FUNC>
		void range(ChunkPolicy chunkPolicy, IDX begin, IDX end, IDX step, FUNC&& func);

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
		/// (atomic counters, external events). With nothing to steal it spins
		/// for details::WAIT_SPIN_TIME, then sleeps between checks, so a long
		/// wait notices @p pred up to details::WAIT_SLEEP_MAX late (a timer tick
		/// on Windows).
		template <class Pred>
		void waitUntil(Pred&& pred);

	private:
		/// Dispatch a Job, blocking until done and rethrowing the first captured
		/// exception. Paths by taskCount(): 0 = no-op, 1 = inline on the caller,
		/// >1 = submit a batch and wait on remaining() via stealUntil().
		/// Templated on the concrete subclass so run(i) resolves without a vtable
		/// hop. AsyncJob does not go through this entry.
		template <class JobT>
		void runQueueJob(JobT& job);

		/// The one wait loop behind waitUntil and runQueueJob: steal pool work
		/// until @p done returns true, backing off per details::WaitBackoff
		/// while there is nothing to steal.
		template <class Done>
		void stealUntil(Done&& done);

		/// Run one stolen task if any worker has work.
		/// @return True if a task was popped and executed (exceptions swallowed).
		bool tryRunSteal();

	private:
		details::WorkerPool m_workerPool;
	};
} // namespace multi

#include "multi/details/context.inl"
