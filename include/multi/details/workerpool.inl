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
		const std::thread::id callerId = std::this_thread::get_id();

		for (size_t i = 0; i < count; ++i)
		{
			Task t = gen(i);
			const size_t idx = (base + i) % m_workerCount;
			pushOrRun(idx, t);
		}

		// Notify at most one worker per unique slot; round-robin wraps after
		// workerCount steps so min(count, workerCount) covers all recipients.
		const size_t wakeCount = count < m_workerCount ? count : m_workerCount;
		for (size_t i = 0; i < wakeCount; ++i)
		{
			const size_t idx = (base + i) % m_workerCount;
			if (m_workers[idx].thread.get_id() != callerId)
				workerNotify(idx);
		}
	}
} // namespace multi::details
