
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
		taskList.reserve(end - begin);
		for (ITER it = begin; it != end; ++it)
			taskList.emplace_back(std::bind(func, std::ref(*it)));
		runQueueJob(std::move(taskList));
	}

	template <typename ITER, typename FUNC>
	void Context::each(size_t taskCount, ITER begin, ITER end, FUNC&& func)
	{
		auto numLoops = (end - begin);
		size_t outerStep = numLoops / (taskCount - 1);

		std::vector<Task> taskList;
		taskList.reserve(taskCount);
		for (size_t i = 0; i < taskCount; ++i)
		{
			ITER innerBegin = begin + (i * outerStep);
			ITER innerEnd = (i + 1 == taskCount) ? end : innerBegin + outerStep;
			taskList.emplace_back([innerBegin, innerEnd, func]()
								  {
									  for (ITER it = innerBegin; it < innerEnd; ++it)
										  func(std::ref(*it));
								  });
		}
		runQueueJob(std::move(taskList));
	}

	// Launch task for each idx with step from begin < end
	// IDX is a POD-type
	// FUNC is void(IDX)
	template <typename IDX, typename FUNC>
	void Context::range(IDX begin, IDX end, IDX step, FUNC&& func)
	{
		std::vector<Task> taskList;
		taskList.reserve((end - begin) / step);
		for (IDX i = begin; i < end; i += step)
			taskList.emplace_back(std::bind(func, i));
		runQueueJob(std::move(taskList));
	}

	template <typename IDX, typename FUNC>
	void Context::range(size_t taskCount, IDX begin, IDX end, IDX step, FUNC&& func)
	{
		IDX numLoops = (end - begin) / step;
		IDX outerStep = numLoops / (taskCount - 1);
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