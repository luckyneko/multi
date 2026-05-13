/*
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
#	include <windows.h>
#elif defined(__APPLE__) || defined(__linux__) || defined(__ANDROID__)
#	include <pthread.h>
#endif

namespace multi
{
	namespace
	{
		// Set at the top of workerMain; readable by any thread to identify
		// itself as worker N (or as "not a worker" via SIZE_MAX). Used by
		// submit/submitBatch to route nested-spawn pushes through the local
		// Chase-Lev fast path instead of the MPMC overflow ring. Exposed via
		// WorkerPool::currentWorkerIndex() so the templated submitBatch
		// overload (defined in the header) can read it too.
		thread_local std::size_t g_workerIndex = std::numeric_limits<std::size_t>::max();

		// Set the calling thread's name so debuggers/profilers can identify
		// it. Best-effort: failures (unsupported platform, security policy,
		// over-long name) are silently ignored — the name is purely for
		// developer ergonomics, never load-bearing.
		//
		// Each backend either takes a thread handle or operates on the
		// caller — the macOS variant only sets the *current* thread. To
		// keep behaviour uniform we always call from inside workerMain.
		//
		// Name length: Linux/glibc caps at 16 bytes including nul (15 chars
		// usable). macOS allows ~64 chars. Windows has no formal limit but
		// most tools display ~32. The "multi-N" format fits 16 bytes for any
		// plausible worker count (up to "multi-1234567890" = 16 bytes incl.
		// nul); callers are responsible for keeping it short.
		void setCurrentThreadName(const char* name)
		{
#if defined(_WIN32)
			// SetThreadDescription wants UTF-16. Worker names are ASCII so
			// a byte-to-wchar copy is sufficient; no MultiByteToWideChar.
			wchar_t wname[32];
			std::size_t i = 0;
			for (; i + 1 < sizeof(wname) / sizeof(wname[0]) && name[i] != '\0'; ++i)
				wname[i] = static_cast<wchar_t>(static_cast<unsigned char>(name[i]));
			wname[i] = L'\0';
			// Available since Windows 10 1607. CI's windows-2022/-2025
			// runners support it; older Windows silently fails to link if
			// it doesn't — accept that as a non-issue for our supported
			// matrix.
			SetThreadDescription(GetCurrentThread(), wname);
#elif defined(__APPLE__)
			// macOS / iOS form has no thread argument — it sets the
			// *current* thread only. Hence calling from workerMain.
			pthread_setname_np(name);
#elif defined(__linux__) || defined(__ANDROID__)
			// glibc / bionic. Takes a handle so could be called externally,
			// but we stay uniform with the macOS path.
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
		, m_threads()
		, m_active(false)
		, m_nextWorker(0)
		, m_nextVictim(0)
		, m_inFlight(0)
	{
	}

	WorkerPool::~WorkerPool()
	{
		// Debug builds assert the user called stop() explicitly. Release builds
		// fall back to stopping here so OS threads don't leak on user error.
		assert(m_threads.empty());
		assert(m_workers.empty());
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

	void WorkerPool::start(size_t threadCount)
	{
		// Surface double-start in both debug and release. Silently no-op'ing
		// would let users keep submitting against a pool that doesn't match
		// the threadCount they just requested.
		if (m_active.load(std::memory_order_relaxed))
			throw std::logic_error("multi::WorkerPool::start called while pool is already active");

		if (threadCount == 0)
			return;

		m_active.store(true, std::memory_order_relaxed);
		m_nextWorker.store(0, std::memory_order_relaxed);
		m_nextVictim.store(0, std::memory_order_relaxed);

		m_workers.reserve(threadCount);
		for (size_t i = 0; i < threadCount; ++i)
			m_workers.push_back(std::make_unique<Worker>());

		m_threads.reserve(threadCount);
		for (size_t i = 0; i < threadCount; ++i)
			m_threads.emplace_back(&WorkerPool::workerMain, this, i);
	}

	void WorkerPool::fencedNotify(Worker& w)
	{
		w.mutex.lock();
		w.mutex.unlock();
		w.cv.notify_one();
	}

	void WorkerPool::stop()
	{
		// Flip m_active then wait for any concurrent submitter that's already
		// past its isActive() check to leave the push region. Pair: submit
		// fetch_adds m_inFlight before re-checking m_active; the seq_cst on
		// both sides gives a Dekker-style total order, so either the submitter
		// observes !m_active and bails, or we observe its increment and spin.
		m_active.store(false, std::memory_order_seq_cst);
		while (m_inFlight.load(std::memory_order_seq_cst) > 0)
			std::this_thread::yield();

		// Now safe: no submitter can touch m_workers. Wake workers (do-while
		// drain in workerMain runs anything pushed before m_active flipped)
		// and tear down.
		for (auto& worker : m_workers)
			fencedNotify(*worker);

		for (auto& thread : m_threads)
			thread.join();

		// A worker can exit its predicate via !m_active with an empty-deque
		// snapshot (predicate's tryGetTask short-circuits), then a late
		// submitter inside the push region pushes a task into that worker's
		// deque before its own fetch_sub. The in-flight counter keeps stop()
		// waiting until that submitter is done, but the worker is already
		// gone — so the task is still in the deque. Drain it on the
		// stopping thread before clearing m_workers; m_inFlight == 0
		// guarantees the deques are stable.
		Task leftover;
		for (auto& worker : m_workers)
		{
			while (worker->deque.pop(&leftover))
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
		m_workers.clear();
	}

	bool WorkerPool::pushWithRetry(size_t idx, Task& task)
	{
		// Caller must hold a slot in m_inFlight before getting here. That slot
		// is what keeps m_workers[idx] alive against a concurrent stop() —
		// stop() spins on m_inFlight reaching zero before clearing m_workers.
		// Without it, the lock-free deque internals don't help: the Worker
		// storage itself could vanish under us.
		Worker* worker = m_workers[idx].get();
		const bool isSelf = (idx == currentWorkerIndex());
		for (;;)
		{
			const bool pushed = isSelf
				? worker->deque.tryPushLocal(std::move(task))
				: worker->deque.tryPushRemote(std::move(task));
			if (pushed)
				return true;
			// Overflow ring is full. If shutdown started while we were spinning,
			// stop() is waiting on m_inFlight — bail to inline so the post-stop
			// drain doesn't deadlock on us. Tasks ending up here run on the
			// submitter rather than a worker.
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
		// Fast path: never-started or already-stopped pool runs inline on
		// the caller, no push-region claim needed.
		if (!isActive())
		{
			task();
			return;
		}

		// Claim a slot in the push region. After this, stop() will spin on
		// m_inFlight before clearing m_workers, so the m_workers access below
		// is safe even if stop() runs concurrently.
		m_inFlight.fetch_add(1, std::memory_order_seq_cst);
		if (!isActive())
		{
			// Lost the race: stop() flipped m_active between our first check
			// and our fetch_add. Release the slot and fall back to inline.
			m_inFlight.fetch_sub(1, std::memory_order_seq_cst);
			task();
			return;
		}

		const size_t idx = m_nextWorker.fetch_add(1, std::memory_order_relaxed) % m_workers.size();
		if (pushWithRetry(idx, task) && idx != currentWorkerIndex())
			fencedNotify(*m_workers[idx]);

		m_inFlight.fetch_sub(1, std::memory_order_seq_cst);
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

		// Same push-region claim pattern as submit(). See submit() for rationale.
		m_inFlight.fetch_add(1, std::memory_order_seq_cst);
		if (!isActive())
		{
			m_inFlight.fetch_sub(1, std::memory_order_seq_cst);
			for (auto& task : tasks)
				task();
			return;
		}

		const size_t workerCount = m_workers.size();
		const size_t base = m_nextWorker.fetch_add(tasks.size(), std::memory_order_relaxed);
		const size_t self = currentWorkerIndex();

		for (size_t i = 0; i < tasks.size(); ++i)
		{
			const size_t idx = (base + i) % workerCount;
			pushWithRetry(idx, tasks[i]);
		}

		// With round-robin starting at `base`, the first min(count, workerCount)
		// slots cover each distinct worker that received at least one task.
		// Anything beyond that just repeats workers already in the wake set, so
		// notifying those first slots covers every pushed task without a mask.
		// A spurious notify for a worker that took a shutdown-inline bail in
		// pushWithRetry is harmless (worker wakes, sees empty deque and
		// !m_active, exits; or has already exited and notify_one is a no-op).
		const size_t wakeCount = tasks.size() < workerCount ? tasks.size() : workerCount;
		for (size_t i = 0; i < wakeCount; ++i)
		{
			const size_t idx = (base + i) % workerCount;
			if (idx != self)
				fencedNotify(*m_workers[idx]);
		}

		m_inFlight.fetch_sub(1, std::memory_order_seq_cst);
	}

	bool WorkerPool::tryStealAny(Task* task)
	{
		const size_t workerCount = m_workers.size();
		if (workerCount == 0)
			return false;

		// Rotate the scan start across calls so multiple concurrent stealers
		// (e.g. several threads inside Handle::wait or runQueueJob) don't all
		// hammer worker 0's deque mutex first. Per-worker steal-from-victim
		// scans in tryGetTask are already self-rotating; this aligns the
		// caller-side scan with that strategy.
		const size_t base = m_nextVictim.fetch_add(1, std::memory_order_relaxed) % workerCount;
		for (size_t i = 0; i < workerCount; ++i)
		{
			if (m_workers[(base + i) % workerCount]->deque.steal(task))
				return true;
		}
		return false;
	}

	void WorkerPool::workerMain(size_t workerIndex)
	{
		g_workerIndex = workerIndex;

		// Name the thread for debugger/profiler ergonomics. "multi-N" stays
		// under 16 bytes (Linux pthread limit) for any plausible worker
		// count, and is greppable in `top -H` / Activity Monitor.
		char name[16];
		std::snprintf(name, sizeof(name), "multi-%zu", workerIndex);
		setCurrentThreadName(name);

		Worker* self = m_workers[workerIndex].get();
		Task task;
		// do-while, not while: if start() returns and stop() is called before
		// this thread is scheduled, m_active is already false on first entry.
		// We must still run the wait/drain block once so the worker pops any
		// tasks that were submitted before stop() — that's the contract
		// stop() relies on to drain pending work.
		do
		{
			// Sleep
			{
				std::unique_lock<std::mutex> lk(self->mutex);
				self->cv.wait(lk, [&]()
							  { return tryGetTask(workerIndex, &task) || !m_active.load(std::memory_order_relaxed); });
			}

			// Run all tasks. A throwing task must not kill the worker —
			// async/batch wrappers installed by the submitter capture the
			// exception; raw submits that throw are swallowed here.
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
