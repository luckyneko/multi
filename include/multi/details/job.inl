
template <typename... FUNCS>
void Job::add(Order order, FUNCS... funcs)
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

template <typename ITER, typename FUNC>
void Job::each(Order order, ITER begin, ITER end, FUNC&& func)
{
	switch (order)
	{
		case Order::seq:
		{
			JobNode* head = nullptr;
			for (auto iter = end; iter != begin; --iter)
				head = allocJobNode([iter, func](Job jb) { func(jb, *(iter - 1)); }, head);
			queueJobNode(head);
			break;
		}
		case Order::par:
		{
			for (auto iter = begin; iter != end; ++iter)
				queueJobNode(allocJobNode([iter, func](Job jb) { func(jb, *iter); }));
			break;
		}
	}
}

template <typename ITER, typename FUNC>
void Job::range(Order order, ITER begin, ITER end, ITER step, FUNC&& func)
{
	switch (order)
	{
		case Order::seq:
		{
			JobNode* head = nullptr;
			for (auto idx = end; idx != begin; idx -= step)
				head = allocJobNode([idx, step, func](Job jb) { func(jb, idx - step); }, head);
			queueJobNode(head);
			break;
		}
		case Order::par:
		{
			for (auto idx = begin; idx != end; idx += step)
				queueJobNode(allocJobNode([idx, func](Job jb) { func(jb, idx); }));
			break;
		}
	}
}

template <typename FUNC, typename PRED>
void Job::until(PRED&& pred, FUNC&& func)
{
	if (!pred())
	{
		add(
			Order::seq,
			[func](Job jb) { func(jb); },
			[pred, func](Job jb) { jb.until(pred, func); });
	}
}

template <typename... TASKS>
JobNode* Job::allocJobNode(Task&& task, TASKS... tasks)
{
	return allocJobNode(
		std::forward<Task>(task),
		allocJobNode(std::forward<TASKS>(tasks)...));
}

template <typename... TASKS>
void Job::queueTasks(Task&& task, TASKS... tasks)
{
	queueTasks(std::forward<Task>(task));
	queueTasks(std::forward<TASKS>(tasks)...);
}