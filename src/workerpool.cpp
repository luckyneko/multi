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
#include <limits>
#include <stdexcept>
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
		// Set at the top of workerMain; identifies the calling thread as worker
		// N (or SIZE_MAX for non-workers). Lets submit/submitBatch route
		// nested-spawn pushes through the local Chase-Lev fast path. Exposed via
		// currentWorkerIndex() for the templated submitBatch in the header.
		thread_local std::size_t g_workerIndex = std::numeric_limits<std::size_t>::max();

#ifdef MULTI_ENABLE_TEST_HOOKS
		std::atomic<std::size_t> g_failStartAfterThreadCreations{
			std::numeric_limits<std::size_t>::max()};

		bool shouldFailThreadCreationForTest()
		{
			std::size_t remaining = g_failStartAfterThreadCreations.load(std::memory_order_relaxed);
			while (remaining != std::numeric_limits<std::size_t>::max())
			{
				if (remaining == 0)
				{
					g_failStartAfterThreadCreations.store(
						std::numeric_limits<std::size_t>::max(),
						std::memory_order_relaxed);
					return true;
				}
				if (g_failStartAfterThreadCreations.compare_exchange_weak(
						remaining,
						remaining - 1,
						std::memory_order_relaxed,
						std::memory_order_relaxed))
					return false;
			}
			return false;
		}
#endif

		// Best-effort thread naming for debuggers/profilers; failures are
		// silently ignored. Always called from workerMain because the macOS
		// backend only names the *current* thread. Keep names short: the
		// "multi-N" format fits Linux's 16-byte limit for any worker count.
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

	std::size_t WorkerPool::currentWorkerIndex()
	{
		return g_workerIndex;
	}

	WorkerPool::WorkerPool()
		: m_workers()
		, m_workerCount(0)
		, m_threads()
		, m_active(false)
		, m_nextWorker(0)
		, m_nextVictim(0)
		, m_opsInFlight(0)
	{
	}

#ifdef MULTI_ENABLE_TEST_HOOKS
	void WorkerPool::failNextStartAfterThreadCreations(size_t successfulCreations)
	{
		g_failStartAfterThreadCreations.store(successfulCreations, std::memory_order_relaxed);
	}
#endif

	WorkerPool::~WorkerPool()
	{
		// Debug asserts stop() was called explicitly; release stops defensively
		// so OS threads don't leak on user error.
		assert(m_threads.empty());
		assert(m_workerCount == 0);
		if (!m_threads.empty())
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

	void WorkerPool::start(int threadCount)
	{
		// Surface double-start rather than silently no-op'ing against a pool
		// that doesn't match the requested threadCount.
		if (m_active.load(std::memory_order_relaxed))
			throw std::logic_error("multi::details::WorkerPool::start called while pool is already active");

		// Negative threadCount means hardware concurrency minus the caller.
		if (threadCount < 0)
			threadCount = std::thread::hardware_concurrency() - 1;

		if (threadCount <= 0)
			return;

		// One over-aligned allocation for the whole array (Worker is
		// alignas(CACHE_LINE_SIZE)), keeping the cache-line discipline intact.
		auto workers = std::make_unique<Worker[]>(threadCount);
		std::vector<std::thread> threads;
		threads.reserve(threadCount);

		m_workers = std::move(workers);
		m_workerCount = threadCount;
		m_nextWorker.store(0, std::memory_order_relaxed);
		m_nextVictim.store(0, std::memory_order_relaxed);
		m_opsInFlight.store(0, std::memory_order_relaxed);
		m_active.store(true, std::memory_order_relaxed);

		try
		{
			for (int i = 0; i < threadCount; ++i)
			{
#ifdef MULTI_ENABLE_TEST_HOOKS
				if (shouldFailThreadCreationForTest())
					throw std::runtime_error("multi::details::WorkerPool::start test-injected thread creation failure");
#endif
				threads.emplace_back(&WorkerPool::workerMain, this, i);
			}
		}
		catch (...)
		{
			m_active.store(false, std::memory_order_seq_cst);
			for (size_t i = 0; i < m_workerCount; ++i)
				fencedNotify(m_workers[i]);
			for (auto& thread : threads)
			{
				if (thread.joinable())
					thread.join();
			}
			m_threads.clear();
			m_workers.reset();
			m_workerCount = 0;
			m_opsInFlight.store(0, std::memory_order_relaxed);
			throw;
		}

		m_threads = std::move(threads);
	}

	void WorkerPool::fencedNotify(Worker& w)
	{
		w.mutex.lock();
		w.mutex.unlock();
		w.cv.notify_one();
	}

	void WorkerPool::stop()
	{
		// Flip m_active, then wait for any operation already past its
		// isActive() check to leave the worker-storage region. Operations
		// fetch_add m_opsInFlight before re-checking m_active; the seq_cst on
		// both sides gives a Dekker-style order, so either they observe
		// !m_active and bail, or we observe their increment and spin.
		m_active.store(false, std::memory_order_seq_cst);
		while (m_opsInFlight.load(std::memory_order_seq_cst) > 0)
			std::this_thread::yield();

		// Safe now: no submitter/stealer can touch m_workers. Wake workers (the
		// do-while drain in workerMain runs anything pushed before the flip).
		for (size_t i = 0; i < m_workerCount; ++i)
			fencedNotify(m_workers[i]);

		for (auto& thread : m_threads)
			thread.join();

		// A worker can exit on !m_active with an empty-deque snapshot just
		// before a late submitter pushes into its deque. The in-flight counter
		// waits for that submitter, but the worker is already gone — so drain
		// any leftover task here (m_opsInFlight == 0 makes the deques stable).
		Task leftover;
		for (size_t i = 0; i < m_workerCount; ++i)
		{
			while (m_workers[i].deque.pop(&leftover))
			{
				try
				{
					leftover();
				}
				catch (...)
				{
				}
				leftover = {};
			}
		}

		m_threads.clear();
		m_workers.reset();
		m_workerCount = 0;
	}

	bool WorkerPool::tryEnterOperation()
	{
		if (!isActive())
			return false;

		m_opsInFlight.fetch_add(1, std::memory_order_seq_cst);
		if (!isActive())
		{
			m_opsInFlight.fetch_sub(1, std::memory_order_seq_cst);
			return false;
		}
		return true;
	}

	void WorkerPool::leaveOperation() noexcept
	{
		m_opsInFlight.fetch_sub(1, std::memory_order_seq_cst);
	}

	bool WorkerPool::pushWithRetry(size_t idx, Task& task)
	{
		// Caller must already hold an m_opsInFlight slot — that's what keeps
		// m_workers[idx] alive against a concurrent stop() (which spins on the
		// counter before clearing m_workers).
		Worker* worker = &m_workers[idx];
		const bool isSelf = (idx == currentWorkerIndex());
		for (;;)
		{
			const bool pushed = isSelf
									? worker->deque.tryPushLocal(std::move(task))
									: worker->deque.tryPushRemote(std::move(task));
			if (pushed)
				return true;
			// Overflow ring full. If shutdown started while spinning, bail to
			// inline so the post-stop drain doesn't deadlock on us.
			if (!isActive())
			{
				task();
				return false;
			}
			std::this_thread::yield();
		}
	}

	void WorkerPool::submit(Task&& task)
	{
		OperationGuard op(*this);
		if (!op.entered())
		{
			task();
			return;
		}

		const size_t idx = m_nextWorker.fetch_add(1, std::memory_order_relaxed) % m_workerCount;
		if (pushWithRetry(idx, task) && idx != currentWorkerIndex())
			fencedNotify(m_workers[idx]);
	}

	void WorkerPool::submitBatch(std::vector<Task>&& tasks)
	{
		if (tasks.empty())
			return;

		OperationGuard op(*this);
		if (!op.entered())
		{
			for (auto& task : tasks)
				task();
			return;
		}

		const size_t workerCount = m_workerCount;
		const size_t base = m_nextWorker.fetch_add(tasks.size(), std::memory_order_relaxed);
		const size_t self = currentWorkerIndex();

		for (size_t i = 0; i < tasks.size(); ++i)
		{
			const size_t idx = (base + i) % workerCount;
			pushWithRetry(idx, tasks[i]);
		}

		// Round-robin from `base` means the first min(count, workerCount) slots
		// cover every distinct worker that got a task. A spurious notify (e.g.
		// to a worker that bailed inline on shutdown) is harmless.
		const size_t wakeCount = tasks.size() < workerCount ? tasks.size() : workerCount;
		for (size_t i = 0; i < wakeCount; ++i)
		{
			const size_t idx = (base + i) % workerCount;
			if (idx != self)
				fencedNotify(m_workers[idx]);
		}
	}

	bool WorkerPool::tryStealAny(Task* task)
	{
		OperationGuard op(*this);
		if (!op.entered())
			return false;

		const size_t workerCount = m_workerCount;
		if (workerCount == 0)
			return false;

		// Rotate the scan start across calls so concurrent stealers don't all
		// hammer worker 0 first, matching tryGetTask's self-rotating scan.
		const size_t base = m_nextVictim.fetch_add(1, std::memory_order_relaxed) % workerCount;
		for (size_t i = 0; i < workerCount; ++i)
		{
			if (m_workers[(base + i) % workerCount].deque.steal(task))
				return true;
		}
		return false;
	}

	void WorkerPool::workerMain(size_t workerIndex)
	{
		g_workerIndex = workerIndex;

		// Name the thread for debugger/profiler ergonomics; "multi-N" fits
		// Linux's 16-byte limit and is greppable in top -H / Activity Monitor.
		char name[16];
		std::snprintf(name, sizeof(name), "multi-%zu", workerIndex);
		setCurrentThreadName(name);

		Worker* self = &m_workers[workerIndex];
		Task task;
		// do-while, not while: if stop() runs before this thread is scheduled,
		// m_active is already false, but we must still run the wait/drain block
		// once to pop tasks submitted before stop() — stop()'s drain contract.
		do
		{
			{
				std::unique_lock<std::mutex> lk(self->mutex);
				self->cv.wait(lk, [&]()
							  { return tryGetTask(workerIndex, &task) || !m_active.load(std::memory_order_relaxed); });
			}

			// A throwing task must not kill the worker — async/batch wrappers
			// capture their own exceptions; raw submits are swallowed here.
			while (task)
			{
				try
				{
					task();
				}
				catch (...)
				{
				}
				task = {};
				tryGetTask(workerIndex, &task);
			}
		} while (m_active.load(std::memory_order_acquire));
	}

	bool WorkerPool::tryGetTask(size_t workerIndex, Task* task)
	{
		// Own deque first (LIFO, temporal locality).
		if (m_workers[workerIndex].deque.pop(task))
			return true;

		// Then steal from others, starting after self to spread contention.
		size_t workerCount = m_workerCount;
		for (size_t i = 1; i < workerCount; ++i)
		{
			size_t victim = (workerIndex + i) % workerCount;
			if (m_workers[victim].deque.steal(task))
				return true;
		}

		return false;
	}
} // namespace multi::details
