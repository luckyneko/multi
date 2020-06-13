
template <typename... FUNCS>
void JobContext::add(Order order, FUNCS... funcs)
{
	if (!m_context)
	{
		runTasks(std::forward<FUNCS>(funcs)...);
	}
	else
	{
		switch (order)
		{
			case Order::seq:
				queueJobNode(allocJobNode(std::forward<FUNCS>(funcs)...));
				break;
			case Order::par:
				queueTasks(std::forward<FUNCS>(funcs)...);
				break;
		}
	}
}

template <typename ITER, typename FUNC>
void JobContext::each(Order order, ITER begin, ITER end, FUNC&& func)
{
	if (!m_context)
	{
		for (auto iter = begin; iter != end; ++iter)
			runTasks([iter, func](JobContext& context) { func(context, *iter); });
	}
	else
	{
		switch (order)
		{
			case Order::seq:
			{
				JobNode* head = nullptr;
				for (auto iter = end; iter != begin; --iter)
					head = allocJobNode([iter, func](JobContext& context) { func(context, *(iter - 1)); }, head);
				queueJobNode(head);
				break;
			}
			case Order::par:
			{
				for (auto iter = begin; iter != end; ++iter)
					queueJobNode(allocJobNode([iter, func](JobContext& context) { func(context, *iter); }));
				break;
			}
		}
	}
}

template <typename ITER, typename FUNC>
void JobContext::range(Order order, ITER begin, ITER end, ITER step, FUNC&& func)
{
	if (!m_context)
	{
		for (auto idx = begin; idx != end; idx += step)
			runTasks([idx, func](JobContext& context) { func(context, idx); });
	}
	else
	{
		switch (order)
		{
			case Order::seq:
			{
				JobNode* head = nullptr;
				for (auto idx = end; idx != begin; idx -= step)
					head = allocJobNode([idx, step, func](JobContext& context) { func(context, idx - step); }, head);
				queueJobNode(head);
				break;
			}
			case Order::par:
			{
				for (auto idx = begin; idx != end; idx += step)
					queueJobNode(allocJobNode([idx, func](JobContext& context) { func(context, idx); }));
				break;
			}
		}
	}
}

template <typename FUNC, typename PRED>
void JobContext::until(PRED&& pred, FUNC&& func)
{
	if (!m_context)
	{
		while (!pred())
			runTasks([func](JobContext& context) { func(context); });
	}
	else
	{
		if (!pred())
		{
			add(
				Order::seq,
				[func](JobContext& context) { func(context); },
				[pred, func](JobContext& context) { context.until(pred, func); });
		}
	}
}

template <typename... TASKS>
JobNode* JobContext::allocJobNode(Task&& task, TASKS... tasks)
{
	return allocJobNode(
		std::forward<Task>(task),
		allocJobNode(std::forward<TASKS>(tasks)...));
}

template <typename... TASKS>
void JobContext::runTasks(Task&& task, TASKS... tasks)
{
	runTasks(std::move(task));
	runTasks(std::forward<TASKS>(tasks)...);
}

template <typename... TASKS>
void JobContext::queueTasks(Task&& task, TASKS... tasks)
{
	queueTasks(std::move(task));
	queueTasks(std::forward<TASKS>(tasks)...);
}