/*
 *  Created by LuckyNeko on 17/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "multi/details/operationlock.h"
#include "multi/details/platform.h"
#include "multi/details/workstealdeque.h"

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <functional>
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
	 * deque is empty. A push wakes its target if the target is parked;
	 * otherwise (the target is busy, or the push was the owner's own) it wakes
	 * one other parked worker, if any, to steal it.
	 */
	MULTI_BEGIN_CACHE_ALIGNED
	class WorkerPool
	{
	public:
		WorkerPool();
		WorkerPool(const WorkerPool&) = delete;
		~WorkerPool();

		bool start(int threadCount, std::function<void(size_t)> onCreate = nullptr);
		void stop();

		void submit(Task&& task);

		// Queue task on a worker's deque and wake per the parking protocol.
		// False, with task untouched, when nothing was queued: the pool is not
		// running, or the target deque is full. submit() runs the task inline
		// in those cases; a caller that must not nest (RecipeJob) calls this
		// and runs the task itself, later.
		bool trySubmit(Task& task);

		// gen(i) is called count times; Gen must be invocable as Task(size_t).
		template <class Gen>
		void submitBatch(size_t count, Gen&& gen);

		// Steal one task from any worker deque for external-caller participation.
		bool tryStealAny(Task* task);

		bool isActive() const { return m_opLock.isActive(); }
		size_t threadCount() const { return m_workerCount; }

	private:
		void workerMain(size_t workerIndex);
		bool tryGetTask(size_t workerIndex, Task* task);
		bool tryGetTaskBeforePark(size_t workerIndex, Task* task);

		// The only wake site: lock/unlock the worker's mutex, then notify. Claims
		// the worker (clears its parked flag) if it is parked, and returns
		// whether it did; an unparked worker is not waiting, so gets no notify.
		bool workerNotify(size_t workerIndex);

		// Claim and wake one parked worker, scanning from start. False if none.
		bool wakeOneParked(size_t start);

		// Call after a push, before deciding whom to wake. The seq_cst fence
		// pairs with the one in workerMain (see the parking protocol in
		// workerpool.cpp); with nobody parked this is the whole cost of a wake.
		bool anyParkedAfterPush() const
		{
			std::atomic_thread_fence(std::memory_order_seq_cst);
			return m_parkedCount.load(std::memory_order_relaxed) != 0;
		}

		// Push to worker idx without running anything. False, with task
		// untouched, if the deque is full.
		bool tryPush(size_t idx, Task& task);

		// tryPush, or run the task inline if the deque is full. True if pushed.
		bool pushOrRun(size_t idx, Task& task);

		// After a push to worker idx: wake whoever should take the task.
		void wakeAfterPush(size_t idx);
		void runTask(Task& task) noexcept;

	private:
		// Worker is non-movable (mutex/condvar), so unique_ptr<Worker[]> not vector.
		struct alignas(CACHE_LINE_SIZE) Worker
		{
			WorkStealDeque deque;
			std::mutex mutex;
			std::condition_variable cv;
			std::thread thread;
			// True while registered as parked; the worker waits on cv only then.
			// Written only under mutex. Read under it to claim (workerNotify), or
			// relaxed without it as a scan hint (wakeOneParked).
			std::atomic<bool> parked{false};
		};
		std::unique_ptr<Worker[]> m_workers;
		size_t m_workerCount = 0;

		// Hot, cache-line-isolated atomics.
		alignas(CACHE_LINE_SIZE) OperationLock m_opLock;
		alignas(CACHE_LINE_SIZE) std::atomic<size_t> m_nextWorker;
		alignas(CACHE_LINE_SIZE) std::atomic<size_t> m_nextVictim;
		// Workers with parked set. Each worker's +1/-1 happens under its own
		// mutex, so the count never under-reports a registered worker.
		alignas(CACHE_LINE_SIZE) std::atomic<size_t> m_parkedCount;
	};
	MULTI_END_CACHE_ALIGNED
} // namespace multi::details

#include "multi/details/workerpool.inl"
