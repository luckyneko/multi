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
	// Access a Global Context
	Context& context();

	// Start threads
	inline void start(int threadCount = -1);

	// Stop threads
	inline void stop();

	// Number of threads
	inline size_t threadCount();

	// Work functions
	// Return type deduces to Handle<R> where R = invoke_result_t<F>: void
	// for value-less lambdas (back-compat with the old `Handle` typedef
	// behaviour), the functor's return type otherwise.
	template <class F>
	auto async(F&& f);

	// Block until `pred()` returns true, participating in work-stealing
	// while we wait. See Context::waitUntil for the rationale.
	template <class Pred>
	void waitUntil(Pred&& pred);

	// Block until *every* handle in the pack completes — participating in
	// work-stealing while we wait. Variadic + tuple-from-parallelAsync
	// overloads; the single-handle case is just `waitAll(h)`. See
	// Context::waitAll for the rationale.
	template <class... Hs>
	void waitAll(const Hs&... hs);
	template <class... Ts>
	void waitAll(const std::tuple<Handle<Ts>...>& tup);

	// Block until at least one handle completes; return its zero-based
	// index. See Context::waitAny.
	template <class... Hs>
	std::size_t waitAny(const Hs&... hs);
	template <class... Ts>
	std::size_t waitAny(const std::tuple<Handle<Ts>...>& tup);

	// Parallel
	template <typename... TASKS>
	void parallel(TASKS&&... tasks);

	// Fan-out variant: returns a std::tuple<Handle<R>...> for per-task
	// observation. See Context::parallelAsync for the contract.
	template <typename... Fs>
	auto parallelAsync(Fs&&... fs);

	// Launch task for each item
	template <typename ITER, typename FUNC>
	void each(ITER begin, ITER end, FUNC&& func);
	template <typename ITER, typename FUNC>
	void each(size_t taskCount, ITER begin, ITER end, FUNC&& func);

	// Step-less overload: default step to IDX(1) for the common case.
	template <typename IDX, typename FUNC>
	void range(IDX begin, IDX end, FUNC&& func);

	// Launch task for each idx with step
	template <typename IDX, typename FUNC>
	void range(IDX begin, IDX end, IDX step, FUNC&& func);
	template <typename IDX, typename FUNC>
	void range(size_t taskCount, IDX begin, IDX end, IDX step, FUNC&& func);

} // namespace multi

#include "multi/details/multi.inl"
