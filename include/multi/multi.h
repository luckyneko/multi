/*
 *  Created by LuckyNeko on 16/03/2020.
 *  Copyright 2020 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "multi/context.h"

namespace multi
{
	// Set Context
	Context*& context();

	// Start threads
	inline void start(size_t threadCount)
	{
		context()->start(threadCount);
	}

	// Stop threads
	inline void stop()
	{
		context()->stop();
	}

	// Number of threads
	inline size_t threadCount()
	{
		return context()->threadCount();
	}

	// Work functions
	// Return type deduces to Handle<R> where R = invoke_result_t<F>: void
	// for value-less lambdas (back-compat with the old `Handle` typedef
	// behaviour), the functor's return type otherwise.
	template <class F>
	auto async(F&& f)
	{
		return context()->async(std::forward<F>(f));
	}

	// Parallel
	template <typename... TASKS>
	void parallel(TASKS&&... tasks)
	{
		context()->parallel(std::forward<TASKS>(tasks)...);
	}

	// Launch task for each item
	template <typename ITER, typename FUNC>
	void each(ITER begin, ITER end, FUNC&& func)
	{
		context()->each(begin, end, std::forward<FUNC>(func));
	}
	template <typename ITER, typename FUNC>
	void each(size_t taskCount, ITER begin, ITER end, FUNC&& func)
	{
		context()->each(taskCount, begin, end, std::forward<FUNC>(func));
	}

	// Step-less overload: default step to IDX(1) for the common case.
	template <typename IDX, typename FUNC>
	void range(IDX begin, IDX end, FUNC&& func)
	{
		context()->range(begin, end, std::forward<FUNC>(func));
	}

	// Launch task for each idx with step
	template <typename IDX, typename FUNC>
	void range(IDX begin, IDX end, IDX step, FUNC&& func)
	{
		context()->range(begin, end, step, std::forward<FUNC>(func));
	}
	template <typename IDX, typename FUNC>
	void range(size_t jobCount, IDX begin, IDX end, IDX step, FUNC&& func)
	{
		context()->range(jobCount, begin, end, step, std::forward<FUNC>(func));
	}
} // namespace multi