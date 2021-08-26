
template <typename... FUNCS>
void Job::add(Order order, FUNCS... funcs)
{
	switch (order)
	{
		case Order::seq:
			m_context->queueJobNode(m_context->allocJobNode(m_parent, std::forward<FUNCS>(funcs)...));
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
			JobNode* tail = nullptr;
			for (auto iter = begin; iter != end; ++iter)
			{
				tail = attachJobNode(tail, m_context->allocJobNode(m_parent, [iter, func](Job jb) { func(jb, *iter); }));
				if(!head)
					head = tail;
			}
			m_context->queueJobNode(head);
			break;
		}
		case Order::par:
		{
			for (auto iter = begin; iter != end; ++iter)
				m_context->queueJobNode(m_context->allocJobNode(m_parent, [iter, func](Job jb) { func(jb, *iter); }));
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
			JobNode* tail = nullptr;
			for (auto idx = begin; idx < end; idx += step)
			{
				tail = attachJobNode(tail, m_context->allocJobNode(m_parent, [idx, func](Job jb) { func(jb, idx); }));
				if(!head)
					head = tail;
			}
			m_context->queueJobNode(head);
			break;
		}
		case Order::par:
		{
			for (auto idx = begin; idx < end; idx += step)
				m_context->queueJobNode(m_context->allocJobNode(m_parent, [idx, func](Job jb) { func(jb, idx); }));
			break;
		}
	}
}

template <typename FUNC, typename PRED>
void Job::until(PRED&& pred, FUNC&& func)
{
	queueTasks([pred, func](Job jb)
	{
		jb.addUntil(pred, func);
	});
	
}

template <typename... TASKS>
void Job::queueTasks(Task&& task, TASKS... tasks)
{
	queueTasks(std::forward<Task>(task));
	queueTasks(std::forward<TASKS>(tasks)...);
}