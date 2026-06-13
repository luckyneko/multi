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

		OperationGuard op(*this);
		if (!op.entered())
		{
			for (size_t i = 0; i < count; ++i)
			{
				Task t = gen(i);
				t();
			}
			return;
		}

		const size_t workerCount = m_workerCount;
		const size_t base = m_nextWorker.fetch_add(count, std::memory_order_relaxed);
		const size_t self = currentWorkerIndex();

		for (size_t i = 0; i < count; ++i)
		{
			Task t = gen(i);
			const size_t idx = (base + i) % workerCount;
			pushWithRetry(idx, t);
		}

		// Round-robin from `base` means the first min(count, workerCount) slots
		// cover every distinct worker that got a task. A spurious notify (e.g.
		// to a worker that bailed inline on shutdown) is harmless.
		const size_t wakeCount = count < workerCount ? count : workerCount;
		for (size_t i = 0; i < wakeCount; ++i)
		{
			const size_t idx = (base + i) % workerCount;
			if (idx != self)
				fencedNotify(m_workers[idx]);
		}
	}
} // namespace multi::details
