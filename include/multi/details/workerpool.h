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
	 * deque is empty.
	 */
	class WorkerPool
	{
	public:
		WorkerPool();
		WorkerPool(const WorkerPool&) = delete;
		~WorkerPool();

		bool start(int threadCount, std::function<void(size_t)> onCreate = nullptr);
		void stop();

		void submit(Task&& task);

		// gen(i) is called count times; Gen must be invocable as Task(size_t).
		template <class Gen>
		void submitBatch(size_t count, Gen&& gen);

		// Steal one task from any worker deque for external-caller participation.
		bool tryStealAny(Task* task);

		bool isActive() const { return m_opLock.isActive(); }
		size_t threadCount() const { return m_workerCount; }

	private:
		void workerMain(size_t workerIndex);
		void workerNotify(size_t workerIndex);
		bool tryGetTask(size_t workerIndex, Task* task);

		// Push to worker idx; runs inline if deque is full. True if pushed (caller notifies).
		bool pushOrRun(size_t idx, Task& task);
		void runTask(Task& task) noexcept;

	private:
		// Worker is non-movable (mutex/condvar), so unique_ptr<Worker[]> not vector.
		struct alignas(CACHE_LINE_SIZE) Worker
		{
			WorkStealDeque deque;
			std::mutex mutex;
			std::condition_variable cv;
			std::thread thread;
		};
		std::unique_ptr<Worker[]> m_workers;
		size_t m_workerCount = 0;

		// Hot, cache-line-isolated atomics.
		alignas(CACHE_LINE_SIZE) OperationLock m_opLock;
		alignas(CACHE_LINE_SIZE) std::atomic<size_t> m_nextWorker;
		alignas(CACHE_LINE_SIZE) std::atomic<size_t> m_nextVictim;
	};
} // namespace multi::details

#include "multi/details/workerpool.inl"
