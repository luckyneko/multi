
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

		std::vector<Task> taskList;
		taskList.reserve(taskCount);
		ITER innerBegin = begin;
		ITER innerEnd = innerBegin;
		const size_t outerStep = totalTaskCount / (taskCount - 1);
		for (size_t i = 0; i < taskCount; ++i)
		{
			// Set end
			if (i + 1 == taskCount)
				innerEnd = end;
			else
				std::advance(innerEnd, outerStep);

			taskList.emplace_back([innerBegin, innerEnd, func]()
								  {
									  for (ITER it = innerBegin; it != innerEnd; ++it)
										  func(std::ref(*it));
								  });

			// Step begin
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

		IDX outerStep = step * (totalTaskCount / (taskCount - 1));
		range(begin, end, outerStep, [&](IDX innerBegin)
			  {
				  for (IDX i = innerBegin; i < innerBegin + outerStep && i < end; i += step)
				  {
					  func(i);
				  }
			  });
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