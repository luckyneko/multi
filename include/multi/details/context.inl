
namespace multi
{
	// Launch parallel tasks
	template <typename... TASKS>
	void Context::parallel(TASKS... tasks)
	{
		const size_t count = sizeof...(TASKS);
		std::vector<Task> taskList;
		taskList.reserve(count);
		appendTasks(taskList, std::forward<TASKS>(tasks)...);
		runQueueJob(std::move(taskList));
	}

	// Launch task for each item
	// ITER is an iterator
	// FUNC is function void(T) or void(T&)
	template <typename ITER, typename FUNC>
	void Context::each(ITER begin, ITER end, FUNC&& func)
	{
		std::vector<Task> taskList;
		taskList.reserve(std::distance(begin, end));
		for (ITER it = begin; it != end; ++it)
			taskList.emplace_back(std::bind(func, std::ref(*it)));
		runQueueJob(std::move(taskList));
	}

	template <typename ITER, typename FUNC>
	void Context::each(size_t taskCount, ITER begin, ITER end, FUNC&& func)
	{
		// Handle single threaded case
		if (taskCount <= 1)
		{
			for (ITER it = begin; it != end; ++it)
				func(std::ref(*it));
			return;
		}

		// Handle less tasks than requested taskCount case
		auto totalTaskCount = std::distance(begin, end);
		if (totalTaskCount <= taskCount)
		{
			each(begin, end, std::move(func));
			return;
		}

		// Spread items as evenly as possible: first 'extra' chunks get (base+1)
		// items and the rest get 'base'. Avoids iterator overflow that a simple
		// outerStep-advance approach causes on non-final chunks.
		const size_t total = static_cast<size_t>(totalTaskCount);
		const size_t base = total / taskCount;
		const size_t extra = total % taskCount;

		std::vector<Task> taskList;
		taskList.reserve(taskCount);
		ITER innerBegin = begin;
		for (size_t i = 0; i < taskCount; ++i)
		{
			ITER innerEnd = innerBegin;
			std::advance(innerEnd, i < extra ? base + 1 : base);
			auto task = [innerBegin, innerEnd, func]()
			{
				for (ITER it = innerBegin; it != innerEnd; ++it)
					func(std::ref(*it));
			};
			taskList.emplace_back(std::move(task));
			innerBegin = innerEnd;
		}
		runQueueJob(std::move(taskList));
	}

	// Launch task for each idx with step from begin < end
	// IDX is a POD-type
	// FUNC is void(IDX)
	template <typename IDX, typename FUNC>
	void Context::range(IDX begin, IDX end, IDX step, FUNC&& func)
	{
		if (step == 0)
			return;

		if (end <= begin)
			return;

		std::vector<Task> taskList;
		taskList.reserve((end - begin) / step);
		for (IDX i = begin; i < end; i += step)
			taskList.emplace_back(std::bind(func, i));
		runQueueJob(std::move(taskList));
	}

	template <typename IDX, typename FUNC>
	void Context::range(size_t taskCount, IDX begin, IDX end, IDX step, FUNC&& func)
	{
		if (step == 0)
			return;

		if (end <= begin)
			return;

		// Handle single threaded case
		if (taskCount <= 1)
		{
			for (IDX i = begin; i < end; i += step)
				func(i);
			return;
		}

		// Handle less tasks than requested taskCount case
		size_t totalTaskCount = size_t((end - begin) / step) + (((end - begin) % step > 0) ? 1 : 0);
		if (totalTaskCount <= taskCount)
		{
			range(begin, end, step, std::move(func));
			return;
		}

		// Balanced distribution: first 'extra' chunks get (base+1) items, rest get 'base'.
		// Guarantees exactly taskCount chunks and avoids the ceiling-division collapse where
		// ceil(total/ceil(total/N)) < N (e.g. total=15, N=14 → ceiling gives 8 chunks, not 14).
		const size_t base = totalTaskCount / taskCount;
		const size_t extra = totalTaskCount % taskCount;

		std::vector<Task> taskList;
		taskList.reserve(taskCount);
		IDX innerBegin = begin;
		for (size_t i = 0; i < taskCount; ++i)
		{
			IDX innerEnd = innerBegin + static_cast<IDX>(i < extra ? base + 1 : base) * step;
			auto task = [innerBegin, innerEnd, step, func]()
			{
				for (IDX j = innerBegin; j < innerEnd; j += step)
					func(j);
			};
			taskList.emplace_back(std::move(task));
			innerBegin = innerEnd;
		}
		runQueueJob(std::move(taskList));
	}

	template <typename... TASKS>
	void Context::appendTasks(std::vector<Task>& taskList, Task&& task, TASKS... tasks) const
	{
		taskList.emplace_back(std::move(task));
		appendTasks(taskList, std::forward<TASKS>(tasks)...);
	}
	inline void Context::appendTasks(std::vector<Task>& taskList, Task&& task) const
	{
		taskList.emplace_back(std::move(task));
	}
} // namespace multi