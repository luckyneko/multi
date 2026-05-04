
#include <future>
#include <memory>

namespace multi
{
	// Launch task onto a thread. F is stored directly in AsyncState — no
	// std::function type erasure until the wrapper lambda is submitted to
	// WorkerPool::submit (where only a shared_ptr is captured, SBO-fitting).
	template <class F>
	Handle Context::async(F&& f)
	{
		struct AsyncState
		{
			std::promise<void> promise;
			std::decay_t<F> func;
			explicit AsyncState(F&& fn) : func(std::forward<F>(fn)) {}
			void run() noexcept
			{
				try
				{
					func();
					promise.set_value();
				}
				catch (...)
				{
					promise.set_exception(std::current_exception());
				}
			}
		};
		auto state = std::make_shared<AsyncState>(std::forward<F>(f));
		auto hdl = state->promise.get_future();
		m_workerPool.submit([state = std::move(state)]() { state->run(); });
		return Handle(std::move(hdl), this);
	}

	// Launch parallel tasks
	template <typename... TASKS>
	void Context::parallel(TASKS&&... tasks)
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
		// Capture func by reference: runQueueJob blocks until every task has
		// run, so func outlives them. The resulting lambda is ~16 bytes and
		// SBO-fits in std::function — std::bind builds a larger functor.
		for (ITER it = begin; it != end; ++it)
		{
			auto* item = &(*it);
			taskList.emplace_back([&func, item]()
								  { func(*item); });
		}
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

		// Handle less tasks than requested taskCount case.
		// std::distance returns a signed difference_type; normalize to size_t
		// up-front so later comparisons don't trip -Wsign-compare in user code.
		auto rawDistance = std::distance(begin, end);
		if (rawDistance <= 0)
			return;
		const size_t total = static_cast<size_t>(rawDistance);
		if (total <= taskCount)
		{
			each(begin, end, std::move(func));
			return;
		}

		// Spread items as evenly as possible: first 'extra' chunks get (base+1)
		// items and the rest get 'base'. Avoids iterator overflow that a simple
		// outerStep-advance approach causes on non-final chunks.
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

	template <typename IDX, typename FUNC>
	void Context::range(IDX begin, IDX end, FUNC&& func)
	{
		range(begin, end, IDX(1), std::forward<FUNC>(func));
	}

	// Launch task for each idx with step from begin < end
	// IDX is a signed POD-type
	// FUNC is void(IDX)
	template <typename IDX, typename FUNC>
	void Context::range(IDX begin, IDX end, IDX step, FUNC&& func)
	{
		static_assert(std::is_signed_v<IDX>, "multi::range: IDX must be a signed type; unsigned subtraction silently underflows");
		if (step == 0)
			return;

		if (end <= begin)
			return;

		std::vector<Task> taskList;
		taskList.reserve((end - begin) / step);
		// Capture func by reference (see each() above); i is captured by value.
		for (IDX i = begin; i < end; i += step)
			taskList.emplace_back([&func, i]()
								  { func(i); });
		runQueueJob(std::move(taskList));
	}

	template <typename IDX, typename FUNC>
	void Context::range(size_t taskCount, IDX begin, IDX end, IDX step, FUNC&& func)
	{
		static_assert(std::is_signed_v<IDX>, "multi::range: IDX must be a signed type; unsigned subtraction silently underflows");
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

	template <typename T, typename... TASKS>
	void Context::appendTasks(std::vector<Task>& taskList, T&& task, TASKS&&... tasks) const
	{
		taskList.emplace_back(std::forward<T>(task));
		appendTasks(taskList, std::forward<TASKS>(tasks)...);
	}
	inline void Context::appendTasks(std::vector<Task>& /*taskList*/) const
	{
	}
} // namespace multi