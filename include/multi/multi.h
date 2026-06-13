/*
 *  Created by LuckyNeko on 16/03/2020.
 *  Copyright 2020 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "multi/context.h"
#include "multi/version.h"

namespace multi
{
	/// @return Reference to the process-global Context.
	Context& context();

	/// Start the global pool's worker threads. @see Context::start
	inline void start(int threadCount = -1);

	/// Stop the global pool. @see Context::stop
	inline void stop();

	/// @return Number of worker threads in the global pool.
	inline size_t threadCount();

	/// Launch @p f on the global pool. @see Context::async
	template <class F>
	auto async(F&& f);

	/// Block (work-stealing) until @p pred is true. @see Context::waitUntil
	template <class Pred>
	void waitUntil(Pred&& pred);

	/// Block (work-stealing) until every handle completes. @see Context::waitAll
	template <class... Hs>
	void waitAll(const Hs&... hs);
	template <class... Ts>
	void waitAll(const std::tuple<Handle<Ts>...>& tup);

	/// Block until one handle completes; @return its index. @see Context::waitAny
	template <class... Hs>
	std::size_t waitAny(const Hs&... hs);
	template <class... Ts>
	std::size_t waitAny(const std::tuple<Handle<Ts>...>& tup);

	/// Run tasks in parallel on the global pool. @see Context::parallel
	template <typename... TASKS>
	void parallel(TASKS&&... tasks);

	/// Fan-out variant returning a tuple of Handles. @see Context::parallelAsync
	template <typename... Fs>
	auto parallelAsync(Fs&&... fs);

	/// Launch one task per item. @see Context::each
	template <typename ITER, typename FUNC>
	void each(ITER begin, ITER end, FUNC&& func);
	template <typename ITER, typename FUNC>
	void each(size_t taskCount, ITER begin, ITER end, FUNC&& func);

	/// Launch one task per index. @see Context::range
	template <typename IDX, typename FUNC>
	void range(IDX begin, IDX end, FUNC&& func);
	template <typename IDX, typename FUNC>
	void range(IDX begin, IDX end, IDX step, FUNC&& func);
	template <typename IDX, typename FUNC>
	void range(size_t taskCount, IDX begin, IDX end, IDX step, FUNC&& func);

} // namespace multi

#include "multi/details/multi.inl"
