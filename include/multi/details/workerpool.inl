/*
 *  Created by LuckyNeko on 11/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

namespace multi::details
{
	template <class Gen>
	void WorkerPool::submitBatch(size_t count, Gen&& gen)
	{
		if (count == 0)
			return;

		std::unique_lock<OperationLock> op(m_opLock, std::try_to_lock);
		if (!op)
		{
			for (size_t i = 0; i < count; ++i)
			{
				Task t = gen(i);
				t();
			}
			return;
		}

		// Claim count consecutive round-robin slots in one atomic step.
		const size_t base = m_nextWorker.fetch_add(count, std::memory_order_relaxed);

		for (size_t i = 0; i < count; ++i)
		{
			Task t = gen(i);
			const size_t idx = (base + i) % m_workerCount;
			pushOrRun(idx, t);
		}

		// One fence for the whole batch: every push above is sequenced before
		// it. Nobody parked means nobody to wake (see the parking protocol in
		// workerpool.cpp).
		if (!anyParkedAfterPush())
			return;

		// Wake each target slot's worker if it is parked; round-robin wraps
		// after workerCount steps, so min(count, workerCount) covers them all.
		// A target that is busy, or is the caller itself (its own local push),
		// leaves its tasks queued behind running work: wake one other parked
		// worker per such slot to steal them.
		const std::thread::id callerId = std::this_thread::get_id();
		const size_t targets = count < m_workerCount ? count : m_workerCount;
		size_t unclaimed = 0;
		for (size_t i = 0; i < targets; ++i)
		{
			const size_t idx = (base + i) % m_workerCount;
			if (m_workers[idx].thread.get_id() == callerId || !workerNotify(idx))
				++unclaimed;
		}
		for (size_t i = 0; i < unclaimed; ++i)
		{
			if (!wakeOneParked(base + targets + i))
				break;
		}
	}
} // namespace multi::details
