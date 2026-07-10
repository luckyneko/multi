/*
 *  Created by LuckyNeko on 16/03/2020.
 *  Copyright 2020 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "multi/context.h"
#include "multi/recipe.h"
#include "multi/version.h"

namespace multi
{
	/// @return Reference to the process-global Context.
	Context& context();

	/// Start the global pool's worker threads. @see Context::start
	inline bool start(int threadCount = -1);

	/// Stop the global pool. @see Context::stop
	inline void stop();

	/// @return Number of worker threads in the global pool.
	inline size_t threadCount();

	/// Launch @p f on the global pool. @see Context::async
	template <class F>
	auto async(F&& f);

	/// Variadic fan-out on the global pool, returning a tuple of Handles.
	/// @see Context::async
	template <typename F0, typename F1, typename... Fs>
	auto async(F0&& f0, F1&& f1, Fs&&... fs);
	/// Launch a Recipe on the global pool. @see Context::async
	inline Handle<> async(Recipe&& recipe);

	/// Run tasks in parallel on the global pool. @see Context::parallel
	template <typename... TASKS>
	void parallel(TASKS&&... tasks);

	/// Launch one task per item. @see Context::each
	template <typename ITER, typename FUNC>
	void each(ITER begin, ITER end, FUNC&& func);
	template <typename ITER, typename FUNC>
	void each(ChunkPolicy chunkPolicy, ITER begin, ITER end, FUNC&& func);
	/// Range-based overloads — iterate a whole container. @see Context::each
	template <typename CONTAINER, typename FUNC>
	void each(CONTAINER&& c, FUNC&& func);
	template <typename CONTAINER, typename FUNC>
	void each(ChunkPolicy chunkPolicy, CONTAINER&& c, FUNC&& func);

	/// Launch one task per index. @see Context::range
	template <typename IDX, typename FUNC>
	void range(IDX begin, IDX end, FUNC&& func);
	template <typename IDX, typename FUNC>
	void range(IDX begin, IDX end, IDX step, FUNC&& func);
	template <typename IDX, typename FUNC>
	void range(ChunkPolicy chunkPolicy, IDX begin, IDX end, IDX step, FUNC&& func);

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

	/// Block (work-stealing) until @p pred is true. @see Context::waitUntil
	template <class Pred>
	void waitUntil(Pred&& pred);

} // namespace multi

#include "multi/details/multi.inl"
