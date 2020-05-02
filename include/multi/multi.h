/*
 *  Created by LuckyNeko on 16/03/2020.
 *  Copyright 2020 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#ifndef _MULTI_H_
#define _MULTI_H_

#include "multi/context.h"

namespace multi
{
	// Set Context
	Context*& context();

	// Start threads
	inline void start(size_t threadCount = std::thread::hardware_concurrency())
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
	inline Handle async(Function&& func)
	{
		return context()->async(std::forward<Function>(func));
	}

	inline void parallel(Function&& func)
	{
		context()->parallel(std::forward<Function>(func));
	}

	template <typename ITER, typename FUNC>
	void parallelFor(ITER begin, ITER end, FUNC&& func)
	{
		context()->parallelFor(begin, end, std::forward<FUNC>(func));
	}

	template <typename... FUNCS>
	void serial(FUNCS... funcs)
	{
		context()->serial(std::forward<FUNCS>(funcs)...);
	}

	template <typename ITER, typename FUNC>
	void serialFor(ITER begin, ITER end, FUNC&& func)
	{
		context()->serialFor(begin, end, std::forward<FUNC>(func));
	}
} // namespace multi

#endif // _MULTI_H_