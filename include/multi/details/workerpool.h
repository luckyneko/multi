/*
 *  Created by LuckyNeko on 17/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "multi/details/constants.h"
#include "multi/details/workstealdeque.h"

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace multi::details
{
	/*
	 * WorkerPool
	 * Thread pool with per-worker steal deques. Tasks are distributed
	 * round-robin on submit; an idle worker steals from others when its own
	 * deque is empty.
	 */
	class WorkerPool
	{
	public:
		WorkerPool();
		WorkerPool(const WorkerPool&) = delete;
		~WorkerPool();

		void start(int threadCount);
		void stop();

		// Submit a single task (round-robin to a worker deque).
		void submit(Task&& task);

		// Submit a batch of tasks, distributing across worker deques.
		void submitBatch(std::vector<Task>&& tasks);

		// Generator-based overload: gen(i) is invoked count times to produce
		// each Task, pushed directly without an intermediate vector. Gen must be
		// invocable as Task(size_t).
		template <class Gen>
		void submitBatch(size_t count, Gen&& gen);

		// Steal a task from any worker deque (external caller participation).
		// Returns true if a task was obtained.
		bool tryStealAny(Task* task);

		bool isActive() const { return m_active.load(std::memory_order_relaxed); }
		size_t threadCount() const { return m_threads.size(); }

#ifdef MULTI_ENABLE_TEST_HOOKS
		// Test hook: fail the next start() after this many successful worker
		// thread creations, then clear the hook.
		static void failNextStartAfterThreadCreations(size_t successfulCreations);
#endif

		// Read-only access to a worker's deque, for tests/benchmarks probing
		// local-vs-overflow routing. The assert catches an out-of-range index.
		const WorkStealDeque& dequeOf(size_t idx) const
		{
			assert(idx < m_workerCount && "WorkerPool::dequeOf: idx out of range");
			return m_workers[idx].deque;
		}

		// Returns the calling thread's worker index, or SIZE_MAX if the caller
		// is not a worker. Backed by a thread_local set in workerMain.
		static size_t currentWorkerIndex();

	private:
		// Each Worker sits on its own cache line
		struct alignas(CACHE_LINE_SIZE) Worker
		{
			WorkStealDeque deque;
			std::mutex mutex;
			std::condition_variable cv;
		};

		void workerMain(size_t workerIndex);
		bool tryGetTask(size_t workerIndex, Task* task);

		bool tryEnterOperation();
		void leaveOperation() noexcept;

		class OperationGuard
		{
		public:
			explicit OperationGuard(WorkerPool& pool)
				: m_pool(&pool)
				, m_entered(pool.tryEnterOperation())
			{
			}

			~OperationGuard()
			{
				if (m_entered)
					m_pool->leaveOperation();
			}

			OperationGuard(const OperationGuard&) = delete;
			OperationGuard& operator=(const OperationGuard&) = delete;

			bool entered() const noexcept { return m_entered; }

		private:
			WorkerPool* m_pool;
			bool m_entered;
		};

		// Spin-yield until the task is pushed onto worker idx's deque, or until
		// shutdown is observed — in which case the task runs inline. Returns
		// true if pushed (caller should notify), false if it ran inline.
		bool pushWithRetry(size_t idx, Task& task);

		// Lock/unlock the worker's mutex before notifying its condvar. The
		// barrier ensures the worker has either already entered wait() (and is
		// woken) or hasn't yet checked its predicate (and will see the new
		// state) — without it a notify in that window would be lost.
		static void fencedNotify(Worker& w);

	private:
		// One heap-allocated array (one allocation regardless of N), directly
		// indexed. Worker is non-movable (holds a mutex/condvar), so this isn't
		// a std::vector<Worker>; capacity is fixed at start(), tracked by
		// m_workerCount.
		std::unique_ptr<Worker[]> m_workers;
		size_t m_workerCount = 0;
		std::vector<std::thread> m_threads;

		// Hot, cache-line-isolated atomics.
		alignas(CACHE_LINE_SIZE) std::atomic<bool> m_active;
		alignas(CACHE_LINE_SIZE) std::atomic<size_t> m_nextWorker;
		alignas(CACHE_LINE_SIZE) std::atomic<size_t> m_nextVictim;
		// Public operations currently touching worker storage (submitters and
		// external stealers). stop() flips m_active then spins on this reaching
		// zero before clearing m_workers, so concurrent ops cannot UAF it.
		alignas(CACHE_LINE_SIZE) std::atomic<size_t> m_opsInFlight;
	};
} // namespace multi::details

#include "multi/details/workerpool.inl"
