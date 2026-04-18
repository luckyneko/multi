/*
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "multi/details/workerpool.h"

#include <cassert>

namespace multi
{
	WorkerPool::WorkerPool()
		: m_workers()
		, m_threads()
		, m_nextWorker(0)
		, m_active(false)
	{
	}

	WorkerPool::~WorkerPool()
	{
		assert(m_threads.empty());
		assert(m_workers.empty());
	}

	void WorkerPool::start(size_t threadCount)
	{
		assert(!m_active.load(std::memory_order_relaxed));
		if (m_active.load(std::memory_order_relaxed))
			return;

		if (threadCount == 0)
			return;

		m_active.store(true, std::memory_order_relaxed);
		m_nextWorker.store(0, std::memory_order_relaxed);

		m_workers.reserve(threadCount);
		for (size_t i = 0; i < threadCount; ++i)
			m_workers.push_back(std::make_unique<Worker>());

		m_threads.reserve(threadCount);
		for (size_t i = 0; i < threadCount; ++i)
			m_threads.emplace_back(&WorkerPool::workerMain, this, i);
	}

	void WorkerPool::stop()
	{
		// Set m_active false then barrier+notify each worker so none can miss the
		// state change between their predicate check and entering wait().
		m_active.store(false, std::memory_order_relaxed);
		for (auto& worker : m_workers)
		{
			worker->mutex.lock();
			worker->mutex.unlock();
			worker->cv.notify_one();
		}

		for (auto& thread : m_threads)
			thread.join();
		m_threads.clear();
		m_workers.clear();
	}

	void WorkerPool::submit(Task&& task)
	{
		if (!isActive())
		{
			task();
			return;
		}

		// Push task
		size_t idx = m_nextWorker.fetch_add(1, std::memory_order_relaxed) % m_workers.size();
		Worker* worker = m_workers[idx].get();
		worker->deque.push(std::move(task));

		// Notify Worker
		worker->mutex.lock();
		worker->mutex.unlock();
		worker->cv.notify_one();
	}

	void WorkerPool::submitBatch(std::vector<Task>&& tasks)
	{
		if (tasks.empty())
			return;

		if (!isActive())
		{
			for (auto& task : tasks)
				task();
			return;
		}

		// Push work
		size_t workerCount = m_workers.size();
		size_t base = m_nextWorker.fetch_add(tasks.size(), std::memory_order_relaxed);
		for (size_t i = 0; i < tasks.size(); ++i)
		{
			size_t idx = (base + i) % workerCount;
			m_workers[idx]->deque.push(std::move(tasks[i]));
		}

		// Barrier+notify each worker that received tasks. With per-worker condvars
		// we target exactly the workers with new work rather than broadcasting.
		size_t wakeCount = tasks.size() < workerCount ? tasks.size() : workerCount;
		for (size_t i = 0; i < wakeCount; ++i)
		{
			Worker* w = m_workers[(base + i) % workerCount].get();
			w->mutex.lock();
			w->mutex.unlock();
			w->cv.notify_one();
		}
	}

	bool WorkerPool::tryStealAny(Task* task)
	{
		for (size_t i = 0; i < m_workers.size(); ++i)
		{
			if (m_workers[i]->deque.steal(task))
				return true;
		}
		return false;
	}

	void WorkerPool::workerMain(size_t workerIndex)
	{
		Worker* self = m_workers[workerIndex].get();
		Task task;
		while (m_active.load(std::memory_order_acquire))
		{
			// Sleep
			{
				std::unique_lock<std::mutex> lk(self->mutex);
				self->cv.wait(lk, [&]()
							  { return tryGetTask(workerIndex, &task) || !m_active.load(std::memory_order_relaxed); });
			}

			// Run all tasks
			while (task)
			{
				task();
				task = nullptr;
				tryGetTask(workerIndex, &task);
			}
		}
	}

	bool WorkerPool::tryGetTask(size_t workerIndex, Task* task)
	{
		// 1. Try own deque first (LIFO - temporal locality)
		if (m_workers[workerIndex]->deque.pop(task))
			return true;

		// 2. Try stealing from other workers (starting after self to spread contention)
		size_t workerCount = m_workers.size();
		for (size_t i = 1; i < workerCount; ++i)
		{
			size_t victim = (workerIndex + i) % workerCount;
			if (m_workers[victim]->deque.steal(task))
				return true;
		}

		return false;
	}
} // namespace multi
