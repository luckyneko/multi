/*
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#ifndef _MULTI_WORKERPOOL_H_
#define _MULTI_WORKERPOOL_H_

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

		// Try to steal a task from any worker deque (for external caller participation)
		// Returns true if a task was obtained
		bool tryStealAny(Task* task);

		inline bool isActive() const { return m_active.load(std::memory_order_relaxed); }
		inline size_t threadCount() const { return m_threads.size(); }

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

	private:
		std::vector<std::unique_ptr<Worker>> m_workers;
		std::vector<std::thread> m_threads;
		std::atomic<size_t> m_nextWorker;
		std::atomic<bool> m_active;
	};
} // namespace multi

#endif // _MULTI_WORKERPOOL_H_
