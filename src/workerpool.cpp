/*
 *  Created by LuckyNeko on 17/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "multi/details/workerpool.h"

#include <cassert>
#include <cstdio>
#include <mutex>
#include <utility>

#if defined(_WIN32)
#	define WIN32_LEAN_AND_MEAN
#	define NOMINMAX
#	include <windows.h>
#elif defined(__APPLE__) || defined(__linux__) || defined(__ANDROID__)
#	include <pthread.h>
#endif

namespace multi::details
{
	namespace
	{
		// macOS only names the current thread, so must be called from within
		// the thread. Names must fit Linux's 16-byte limit.
		void setCurrentThreadName(const char* name)
		{
#if defined(_WIN32)
			// SetThreadDescription wants UTF-16; worker names are ASCII so a
			// byte-to-wchar copy suffices. Available since Windows 10 1607.
			wchar_t wname[32];
			std::size_t i = 0;
			for (; i + 1 < sizeof(wname) / sizeof(wname[0]) && name[i] != '\0'; ++i)
				wname[i] = static_cast<wchar_t>(static_cast<unsigned char>(name[i]));
			wname[i] = L'\0';
			SetThreadDescription(GetCurrentThread(), wname);
#elif defined(__APPLE__)
			pthread_setname_np(name);
#elif defined(__linux__) || defined(__ANDROID__)
			pthread_setname_np(pthread_self(), name);
#else
			(void)name;
#endif
		}
	} // namespace

	WorkerPool::WorkerPool()
		: m_workers()
		, m_workerCount(0)
		, m_opLock()
		, m_nextWorker(0)
		, m_nextVictim(0)
	{
	}

	WorkerPool::~WorkerPool()
	{
		// Debug asserts stop() was called explicitly; release stops defensively
		// so OS threads don't leak on user error.
		assert(m_workerCount == 0);
		if (m_workerCount > 0)
		{
			try
			{
				stop();
			}
			catch (...)
			{
			}
		}
	}

	bool WorkerPool::start(int threadCount, std::function<void(size_t)> onCreate)
	{
		// Double-start is a programming error: assert in debug, return false in release.
		assert(!m_opLock.isActive());
		if (m_opLock.isActive())
			return false;

		if (threadCount < 0)
			threadCount = std::thread::hardware_concurrency() - 1;
		if (threadCount <= 0)
			return true;

		m_workers = std::make_unique<Worker[]>(threadCount);
		m_workerCount = threadCount;
		m_nextWorker.store(0, std::memory_order_relaxed);
		m_nextVictim.store(0, std::memory_order_relaxed);
		m_opLock.activate();

		try
		{
			for (int i = 0; i < threadCount; ++i)
			{
				if (onCreate)
					onCreate(static_cast<size_t>(i));
				m_workers[i].thread = std::thread(&WorkerPool::workerMain, this, i);
			}
		}
		catch (...)
		{
			stop();
			return false;
		}
		return true;
	}

	void WorkerPool::stop()
	{
		m_opLock.deactivate();

		// Wake workers to drain any tasks pushed before deactivation.
		for (size_t i = 0; i < m_workerCount; ++i)
			workerNotify(i);

		for (size_t i = 0; i < m_workerCount; ++i)
		{
			if (m_workers[i].thread.joinable())
				m_workers[i].thread.join();
		}

		// Drain tasks a late submitter may have pushed after the last worker
		// checked its deque. The op-lock ensures no new pushes occur.
		Task leftover;
		for (size_t i = 0; i < m_workerCount; ++i)
		{
			while (m_workers[i].deque.pop(&leftover))
				runTask(leftover);
		}

		m_workers.reset();
		m_workerCount = 0;
	}

	void WorkerPool::submit(Task&& task)
	{
		std::unique_lock<OperationLock> op(m_opLock, std::try_to_lock);
		if (!op)
		{
			task();
			return;
		}

		const size_t idx = m_nextWorker.fetch_add(1, std::memory_order_relaxed) % m_workerCount;
		const bool isOwner = m_workers[idx].thread.get_id() == std::this_thread::get_id();
		if (pushOrRun(idx, task) && !isOwner)
			workerNotify(idx);
	}

	bool WorkerPool::tryStealAny(Task* task)
	{
		std::unique_lock<OperationLock> op(m_opLock, std::try_to_lock);
		if (!op)
			return false;

		// Rotate start to spread contention across workers.
		const size_t base = m_nextVictim.fetch_add(1, std::memory_order_relaxed) % m_workerCount;
		for (size_t i = 0; i < m_workerCount; ++i)
		{
			if (m_workers[(base + i) % m_workerCount].deque.steal(task))
				return true;
		}
		return false;
	}

	void WorkerPool::workerMain(size_t workerIndex)
	{
		Worker* self = &m_workers[workerIndex];

		char name[16];
		std::snprintf(name, sizeof(name), "multi-%zu", workerIndex);
		setCurrentThreadName(name);

		// do-while: ensures one drain pass even if stop() fires before this
		// thread is scheduled and isActive() is already false.
		Task task;
		do
		{
			{
				std::unique_lock<std::mutex> lk(self->mutex);
				self->cv.wait(lk, [&]()
							  { return tryGetTask(workerIndex, &task) || !m_opLock.isActive(); });
			}

			// A throwing task must not kill the worker — async/batch wrappers
			// capture their own exceptions; raw submits are swallowed here.
			while (task)
			{
				runTask(task);
				tryGetTask(workerIndex, &task);
			}
		} while (m_opLock.isActive(std::memory_order_acquire));
	}

	void WorkerPool::workerNotify(size_t workerIndex)
	{
		// lock/unlock before notify_one closes the race with cv.wait: either we
		// hold the lock when the worker tests its predicate (forcing a re-check
		// before it sleeps) or the worker holds it when we reach notify_one (so
		// the notify is not lost).
		auto& w = m_workers[workerIndex];
		w.mutex.lock();
		w.mutex.unlock();
		w.cv.notify_one();
	}

	bool WorkerPool::tryGetTask(size_t workerIndex, Task* task)
	{
		// Own deque first (LIFO, temporal locality).
		if (m_workers[workerIndex].deque.pop(task))
			return true;

		// Then steal from others, starting after self to spread contention.
		for (size_t i = 1; i < m_workerCount; ++i)
		{
			if (m_workers[(workerIndex + i) % m_workerCount].deque.steal(task))
				return true;
		}

		return false;
	}

	bool WorkerPool::pushOrRun(size_t idx, Task& task)
	{
		// Owner uses the SPSC Chase-Lev path; everyone else uses the MPMC
		// overflow ring. Calling tryPushLocal from a non-owner is a data race.
		Worker& w = m_workers[idx];
		const bool isOwner = w.thread.get_id() == std::this_thread::get_id();
		const bool pushed = isOwner
								? w.deque.tryPushLocal(std::move(task))
								: w.deque.tryPushRemote(std::move(task));
		if (!pushed)
			runTask(task);
		return pushed;
	}

	void WorkerPool::runTask(Task& task) noexcept
	{
		try
		{
			task();
		}
		catch (...)
		{
		}
		task = {}; // reset to empty so callers can test bool(task) after the call
	}
} // namespace multi::details
