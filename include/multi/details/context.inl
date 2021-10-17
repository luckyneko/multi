
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
			taskList.push_back(std::bind(func, std::ref(*it)));
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
			taskList.push_back(std::bind(func, i));
		runQueueJob(std::move(taskList));
	}

	template <typename... TASKS>
	void Context::appendTasks(std::vector<Task>& taskList, Task&& task, TASKS... tasks) const
	{
		taskList.push_back(std::move(task));
		appendTasks(taskList, std::forward<TASKS>(tasks)...);
	}
	inline void Context::appendTasks(std::vector<Task>& taskList, Task&& task) const
	{
		taskList.push_back(std::move(task));
	}
} // namespace multi