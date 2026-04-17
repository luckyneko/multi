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
			m_workers.push_back(new Worker());

		m_threads.reserve(threadCount);
		for (size_t i = 0; i < threadCount; ++i)
			m_threads.emplace_back(&WorkerPool::workerMain, this, i);
	}

	void WorkerPool::stop()
	{
		// Set m_active under the mutex so no worker can miss the state change
		// between its predicate check and entering wait().
		{
			std::lock_guard<std::mutex> lk(m_sleepMutex);
			m_active.store(false, std::memory_order_relaxed);
		}
		m_sleepCV.notify_all();

		for (auto& thread : m_threads)
			thread.join();
		m_threads.clear();

		for (auto* worker : m_workers)
			delete worker;
		m_workers.clear();
	}

	void WorkerPool::submit(Task&& task)
	{
		if (!isActive())
		{
			task();
			return;
		}

		size_t idx = m_nextWorker.fetch_add(1, std::memory_order_relaxed) % m_workers.size();
		m_workers[idx]->deque.push(std::move(task));

		// Lock-unlock before notify ensures any worker that already checked
		// the predicate and found no work has entered wait() before we signal.
		{
			std::lock_guard<std::mutex> lk(m_sleepMutex);
		}
		m_sleepCV.notify_one();
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

		size_t workerCount = m_workers.size();
		size_t base = m_nextWorker.fetch_add(tasks.size(), std::memory_order_relaxed);
		for (size_t i = 0; i < tasks.size(); ++i)
		{
			size_t idx = (base + i) % workerCount;
			m_workers[idx]->deque.push(std::move(tasks[i]));
		}

		// Same lost-notification barrier as submit().
		{
			std::lock_guard<std::mutex> lk(m_sleepMutex);
		}

		size_t wakeCount = tasks.size() < workerCount ? tasks.size() : workerCount;
		if (wakeCount == workerCount)
		{
			m_sleepCV.notify_all();
		}
		else
		{
			for (size_t i = 0; i < wakeCount; ++i)
				m_sleepCV.notify_one();
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
		Task task;
		while (m_active.load(std::memory_order_acquire))
		{
			// Sleep until notified
			std::unique_lock<std::mutex> lk(m_sleepMutex);
			m_sleepCV.wait(lk, [&]()
						   { return tryGetTask(workerIndex, &task) || !m_active.load(std::memory_order_relaxed); });
			lk.unlock();

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
