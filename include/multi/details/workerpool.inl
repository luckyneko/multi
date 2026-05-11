
namespace multi
{
	template <class Gen>
	void WorkerPool::submitBatch(size_t count, Gen&& gen)
	{
		if (count == 0)
			return;

		if (!isActive())
		{
			for (size_t i = 0; i < count; ++i)
			{
				Task t = gen(i);
				t();
			}
			return;
		}

		m_inFlight.fetch_add(1, std::memory_order_seq_cst);
		if (!isActive())
		{
			m_inFlight.fetch_sub(1, std::memory_order_seq_cst);
			for (size_t i = 0; i < count; ++i)
			{
				Task t = gen(i);
				t();
			}
			return;
		}

		const size_t workerCount = m_workers.size();
		const size_t base = m_nextWorker.fetch_add(count, std::memory_order_relaxed);
		const size_t self = currentWorkerIndex();

		std::vector<bool> notifyMask(workerCount, false);
		for (size_t i = 0; i < count; ++i)
		{
			Task t = gen(i);
			const size_t idx = (base + i) % workerCount;
			if (pushWithRetry(idx, t) && idx != self)
				notifyMask[idx] = true;
		}

		for (size_t i = 0; i < workerCount; ++i)
			if (notifyMask[i])
				fencedNotify(*m_workers[i]);

		m_inFlight.fetch_sub(1, std::memory_order_seq_cst);
	}
} // namespace multi