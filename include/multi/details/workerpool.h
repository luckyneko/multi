/*
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "multi/details/constants.h"
#include "multi/details/workstealdeque.h"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace multi
{
	/*
	 * WorkerPool
	 * Thread pool with per-worker steal deques.
	 * Tasks are distributed round-robin on submit and workers steal
	 * from each other when their local deque is empty.
	 */
	class WorkerPool
	{
	public:
		WorkerPool();
		WorkerPool(const WorkerPool&) = delete;
		~WorkerPool();

		void start(size_t threadCount);
		void stop();

		// Submit a single task (round-robin to a worker deque)
		void submit(Task&& task);

		// Submit a batch of tasks, distributing across worker deques
		void submitBatch(std::vector<Task>&& tasks);

		// Generator-based submitBatch: gen(i) is invoked count times to produce
		// each Task, immediately pushed without an intermediate vector. Used by
		// Context::runQueueJob to skip materialising a wrapper vector. Gen must
		// be invocable as Task(size_t).
		template <class Gen>
		void submitBatch(size_t count, Gen&& gen);

		// Try to steal a task from any worker deque (for external caller participation)
		// Returns true if a task was obtained
		bool tryStealAny(Task* task);

		bool isActive() const { return m_active.load(std::memory_order_relaxed); }
		size_t threadCount() const { return m_threads.size(); }

		// Observability: read-only access to a worker's deque. Used by tests
		// and benchmarks to probe local-vs-overflow routing decisions.
		const WorkStealDeque& dequeOf(size_t idx) const { return m_workers[idx]->deque; }

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

		// Spin-yield until the task is pushed onto worker idx's deque, or until
		// shutdown is observed — in which case the task is run inline. Returns
		// true if pushed (caller should notify), false if ran inline.
		bool pushWithRetry(size_t idx, Task& task);

		// Lock then immediately unlock the worker's mutex before notifying its
		// condvar. The lock/unlock acts as a barrier: it ensures the worker has
		// either already entered wait() (and will be woken by notify_one) or
		// has not yet checked its predicate (and will see the new state when it
		// does). Without this, a notify sent between the predicate check and
		// the wait() call would be lost.
		static void fencedNotify(Worker& w);

	private:
		std::vector<std::unique_ptr<Worker>> m_workers;
		std::vector<std::thread> m_threads;

		// Align 'Hot' Variables
		alignas(CACHE_LINE_SIZE) std::atomic<bool> m_active;
		alignas(CACHE_LINE_SIZE) std::atomic<size_t> m_nextWorker;
		alignas(CACHE_LINE_SIZE) std::atomic<size_t> m_nextVictim;
		// Count of submitters currently inside the push region (between the
		// post-increment isActive() check and the matching fetch_sub). stop()
		// flips m_active and then spins on this counter reaching zero before
		// joining/clearing m_workers, so a concurrent submit can't UAF the
		// worker storage.
		alignas(CACHE_LINE_SIZE) std::atomic<size_t> m_inFlight;
	};
} // namespace multi

#include "multi/details/workerpool.inl"
