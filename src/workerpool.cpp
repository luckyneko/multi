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
		, m_parkedCount(0)
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
		m_parkedCount.store(0, std::memory_order_relaxed);
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

		// Wake workers to drain any tasks pushed before deactivation. This
		// claims and notifies every parked worker; one that is not parked is
		// not waiting, and sees !isActive() the next time it takes its mutex
		// (deactivate() happens-before that, through our lock of the mutex).
		for (size_t i = 0; i < m_workerCount; ++i)
			workerNotify(i);

		for (size_t i = 0; i < m_workerCount; ++i)
		{
			if (m_workers[i].thread.joinable())
				m_workers[i].thread.join();
		}
		// Every exit path out of workerMain unregisters, and claims decrement.
		assert(m_parkedCount.load(std::memory_order_relaxed) == 0);

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
		if (pushOrRun(idx, task)) // false: deque full, ran inline, nothing queued
			wakeAfterPush(idx);
	}

	bool WorkerPool::trySubmit(Task& task)
	{
		std::unique_lock<OperationLock> op(m_opLock, std::try_to_lock);
		if (!op)
			return false;

		const size_t idx = m_nextWorker.fetch_add(1, std::memory_order_relaxed) % m_workerCount;
		if (!tryPush(idx, task))
			return false;
		wakeAfterPush(idx);
		return true;
	}

	void WorkerPool::wakeAfterPush(size_t idx)
	{
		// Nobody parked: the target and every other worker are awake and will
		// scan the deques before they park (see the parking protocol below).
		if (!anyParkedAfterPush())
			return;

		// A parked target is claimed and woken. Otherwise the task waits behind
		// running work — the target is busy, or this is the owner's own push
		// and the owner may block on it — so hand it to another parked worker.
		const bool isOwner = m_workers[idx].thread.get_id() == std::this_thread::get_id();
		if (isOwner || !workerNotify(idx))
			wakeOneParked(idx + 1);
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

	/*
	 * Parking protocol
	 *
	 * A worker sleeps in cv.wait only while registered as parked (its parked
	 * flag set and counted in m_parkedCount), and it holds its own mutex from
	 * registering, through re-checking the deques, until cv.wait releases it:
	 *
	 *   worker (workerMain)                  submitter (submit/submitBatch)
	 *   -------------------                  ------------------------------
	 *   (a scan of the deques found nothing) push task                    (A)
	 *   lock own mutex                       seq_cst fence                (X)
	 *   parked = true, ++m_parkedCount (B)   read m_parkedCount           (C)
	 *   seq_cst fence                  (Y)   if non-zero: claim and wake the
	 *   re-check deques                (D)     target if parked, else one
	 *   still nothing: cv.wait                 other parked worker
	 *
	 * 1. Submitter pushes then reads "who is parked"; worker publishes
	 *    "parked" then re-checks. Each side is a store followed by a load of a
	 *    different object, which plain release/acquire does not order (both
	 *    stores can sit in store buffers while both loads read stale values:
	 *    the Dekker failure). The two seq_cst fences are totally ordered:
	 *    - X before Y: D observes the push A (or a later state, in which
	 *      another thread already took the task), so the worker does not
	 *      sleep on it.
	 *    - Y before X: C observes B or a later modification. A later
	 *      decrement of this worker's +1 is a claim, which wakes it, or its
	 *      own unregistering, which means it is awake. Likewise any relaxed
	 *      read of Worker::parked after X sees true unless the worker has
	 *      since been claimed. So the submitter sees it, and wakes it or
	 *      another parked worker.
	 *    Either way the push is never invisible to a sleeping worker and
	 *    unanswered by a wake. The fences carry only this liveness argument:
	 *    task data is published by the deques' own release/acquire pairs, and
	 *    parked/m_parkedCount are atomics, so TSan sees no unsynchronised
	 *    access. A worker's +1 and -1 always happen under its own mutex, so
	 *    they alternate in the count's modification order: a read that
	 *    observes some worker's +1 but not the matching -1 is non-zero.
	 *
	 * 2. Predicate check vs entering cv.wait. Claims (parked = false) happen
	 *    only in workerNotify, under the worker's mutex. The worker holds that
	 *    mutex from registration until cv.wait atomically releases it and
	 *    starts waiting, and never waits unregistered. So a claimer that sees
	 *    parked == true has found the worker inside cv.wait: the notify_one
	 *    it sends after unlocking either wakes it or finds it already waking
	 *    (spuriously), and in both cases the worker re-scans. A claimer that
	 *    sees parked == false has found a worker that is not waiting and
	 *    needs no notify. A claimer that got the mutex before the worker
	 *    locked it made its push happen-before the worker's re-check.
	 *
	 * 3. Claims are exclusive: clearing parked under the mutex gives each
	 *    sleeping worker to one claimer, so two pushes that each need a
	 *    helper wake two workers, not the same one twice.
	 *
	 * Case 1 needs the re-check D to take a task it can see, not merely to
	 * try once, and needs nothing more: see tryGetTaskBeforePark.
	 *
	 * Cost: with nobody parked, a push adds the fence and one relaxed load of
	 * m_parkedCount: no lock, no notify. Parking adds the registration and the
	 * fence; a worker going idle still scans twice before sleeping (the drain
	 * loop's last scan, then the re-check), as before the parking protocol.
	 */
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
				bool woken = false;
				for (;;)
				{
					if (!self->parked.load(std::memory_order_relaxed))
					{
						// Claimed by a waker: a plain scan first, so a worker
						// woken for a task takes it without touching the shared
						// count. Arriving from the drain loop, whose last scan
						// just came up empty, register at once instead: a third
						// scan before sleeping would mostly catch tasks a
						// submitter is still pushing, keeping idle workers
						// awake to contend with it (it wakes them when done).
						if (woken && (tryGetTask(workerIndex, &task) || !m_opLock.isActive()))
							break;

						// Register, then re-check below (protocol case 1).
						self->parked.store(true, std::memory_order_relaxed);
						m_parkedCount.fetch_add(1, std::memory_order_relaxed);
						std::atomic_thread_fence(std::memory_order_seq_cst);
					}

					// Registered: re-check after the fence, or after a
					// spurious wake, then sleep with the mutex released.
					if (tryGetTaskBeforePark(workerIndex, &task) || !m_opLock.isActive())
						break;
					self->cv.wait(lk);
					woken = true;
				}

				// Leaving with work (or for stop) while still registered:
				// unregister. A waker that claimed us already did.
				if (self->parked.load(std::memory_order_relaxed))
				{
					self->parked.store(false, std::memory_order_relaxed);
					m_parkedCount.fetch_sub(1, std::memory_order_relaxed);
				}
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

	bool WorkerPool::workerNotify(size_t workerIndex)
	{
		// lock/unlock before notify_one closes the race with cv.wait: a parked
		// worker holds its mutex until cv.wait releases it, so seeing parked
		// under the mutex means it is already waiting and the notify is not
		// lost (protocol case 2 above workerMain). Claiming under the same lock
		// makes each parked worker one waker's to wake (case 3).
		auto& w = m_workers[workerIndex];
		w.mutex.lock();
		const bool claimed = w.parked.load(std::memory_order_relaxed);
		if (claimed)
		{
			w.parked.store(false, std::memory_order_relaxed);
			m_parkedCount.fetch_sub(1, std::memory_order_relaxed);
		}
		w.mutex.unlock();
		if (claimed)
			w.cv.notify_one();
		return claimed;
	}

	bool WorkerPool::wakeOneParked(size_t start)
	{
		// Called after anyParkedAfterPush(). By protocol case 1, every worker
		// that could have missed this push reads as parked here until someone
		// claims it. Re-reading the count never goes back in its modification
		// order, so once it reads zero every such worker has been claimed, or
		// has unregistered itself, and is awake.
		for (size_t i = 0; i < m_workerCount; ++i)
		{
			if (m_parkedCount.load(std::memory_order_relaxed) == 0)
				return false;
			const size_t idx = (start + i) % m_workerCount;
			if (m_workers[idx].parked.load(std::memory_order_relaxed) && workerNotify(idx))
				return true;
		}
		return false;
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

	bool WorkerPool::tryGetTaskBeforePark(size_t workerIndex, Task* task)
	{
		// The last scan before sleeping must not come back empty while it can
		// see a queued task, or protocol case 1 (above workerMain) proves
		// nothing. A plain tryGetTask pass can: a Chase-Lev steal that loses
		// its CAS to another thief gives up on that deque although more tasks
		// may sit behind the stolen one, and an MPMC pop stops at a slot
		// another thread has claimed but not yet filled although later slots
		// are full. So this is one pass, own deque first, in which a deque
		// that reads non-empty is retried until a take succeeds or it reads
		// empty. Each failed take means another thread is mid-operation on
		// that deque, so the retry is transient.
		//
		// Each deque's state when it is read is all that matters. A task case 1
		// makes this worker responsible for was pushed before its submitter's
		// fence X, and X precedes this worker's fence Y, so the relaxed size
		// reads here (after Y) see its enqueue: its deque reads non-empty until
		// someone takes it. A task this pass misses is one whose enqueue it did
		// not see, so its submitter's X follows Y, and that submitter's read of
		// m_parkedCount sees this worker parked. Not waiting for every deque to
		// read empty is what keeps a parking worker from chasing a submitter's
		// push loop task by task, slowing the pusher it competes with.
		//
		// Each deque is read overflow first: an owner refilling its local half
		// moves tasks overflow -> local one at a time, so a task is missed only
		// while it is in the owner's hands, and that is the one the owner pops
		// next (LIFO). An empty deque is skipped without the steal's fence.
		for (size_t i = 0; i < m_workerCount; ++i)
		{
			WorkStealDeque& d = m_workers[(workerIndex + i) % m_workerCount].deque;
			for (unsigned failures = 0; d.overflowSizeHint() != 0 || d.localSizeHint() != 0; ++failures)
			{
				if (i == 0 ? d.pop(task) : d.steal(task))
					return true;
				if (failures > 0)
					std::this_thread::yield(); // a pusher may be preempted mid-push
			}
		}
		return false;
	}

	bool WorkerPool::tryPush(size_t idx, Task& task)
	{
		// Owner uses the SPSC Chase-Lev path; everyone else uses the MPMC
		// overflow ring. Calling tryPushLocal from a non-owner is a data race.
		// Both move from task only on success.
		Worker& w = m_workers[idx];
		const bool isOwner = w.thread.get_id() == std::this_thread::get_id();
		return isOwner ? w.deque.tryPushLocal(std::move(task)) : w.deque.tryPushRemote(std::move(task));
	}

	bool WorkerPool::pushOrRun(size_t idx, Task& task)
	{
		const bool pushed = tryPush(idx, task);
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
