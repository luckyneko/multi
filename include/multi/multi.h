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
	// Access (or replace) the global Context pointer used by every free
	// function in this header. Default points to a static Context instance
	// in src/multi.cpp; tests swap it to exercise multiple Context
	// instances side-by-side.
	//
	// THREAD-SAFETY CONTRACT: the returned reference is to an
	// unsynchronised pointer. Reads from the free-function wrappers
	// (start/async/parallel/each/range/reduce/...) and writes via
	// `multi::context() = otherCtx` race if they happen concurrently.
	// The contract is "set once at startup, before any worker thread or
	// concurrent caller can observe it"; swapping while workers are
	// running, or while other threads are calling free functions, is
	// undefined behaviour. Code that needs hot-swappable pools should
	// hold an explicit `Context*` of its own rather than reaching through
	// this accessor.
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

	// Block until `pred()` returns true, participating in work-stealing
	// while we wait. See Context::waitUntil for the rationale.
	template <class Pred>
	void waitUntil(Pred&& pred)
	{
		context()->waitUntil(std::forward<Pred>(pred));
	}

	// Block until *every* handle in the pack completes — participating in
	// work-stealing while we wait. Variadic + tuple-from-parallelAsync
	// overloads; the single-handle case is just `waitAll(h)`. See
	// Context::waitAll for the rationale.
	template <class... Hs>
	void waitAll(const Hs&... hs)
	{
		context()->waitAll(hs...);
	}
	template <class... Ts>
	void waitAll(const std::tuple<Handle<Ts>...>& tup)
	{
		context()->waitAll(tup);
	}

	// Block until at least one handle completes; return its zero-based
	// index. See Context::waitAny.
	template <class... Hs>
	std::size_t waitAny(const Hs&... hs)
	{
		return context()->waitAny(hs...);
	}
	template <class... Ts>
	std::size_t waitAny(const std::tuple<Handle<Ts>...>& tup)
	{
		return context()->waitAny(tup);
	}

	// Parallel
	template <typename... TASKS>
	void parallel(TASKS&&... tasks)
	{
		context()->parallel(std::forward<TASKS>(tasks)...);
	}

	// Fan-out variant: returns a std::tuple<Handle<R>...> for per-task
	// observation. See Context::parallelAsync for the contract.
	template <typename... Fs>
	auto parallelAsync(Fs&&... fs)
	{
		return context()->parallelAsync(std::forward<Fs>(fs)...);
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

	// Parallel reduce / transformReduce — see Context::reduce documentation
	// for the associativity/commutativity contract on the binary op.
	template <typename ITER, typename T, typename BinaryOp>
	T reduce(ITER begin, ITER end, T init, BinaryOp&& op)
	{
		return context()->reduce(begin, end, std::move(init), std::forward<BinaryOp>(op));
	}

	template <typename ITER, typename T, typename BinaryOp>
	T reduce(size_t taskCount, ITER begin, ITER end, T init, BinaryOp&& op)
	{
		return context()->reduce(taskCount, begin, end, std::move(init), std::forward<BinaryOp>(op));
	}

	template <typename ITER, typename T, typename BinaryOp, typename UnaryOp>
	T transformReduce(ITER begin, ITER end, T init, BinaryOp&& reduceOp, UnaryOp&& transformOp)
	{
		return context()->transformReduce(begin, end, std::move(init),
		                                   std::forward<BinaryOp>(reduceOp),
		                                   std::forward<UnaryOp>(transformOp));
	}

	template <typename ITER, typename T, typename BinaryOp, typename UnaryOp>
	T transformReduce(size_t taskCount, ITER begin, ITER end, T init,
	                   BinaryOp&& reduceOp, UnaryOp&& transformOp)
	{
		return context()->transformReduce(taskCount, begin, end, std::move(init),
		                                   std::forward<BinaryOp>(reduceOp),
		                                   std::forward<UnaryOp>(transformOp));
	}

	// In-place parallel sort — see Context::sort for the algorithm
	// (median-of-three pivot, 3-way partition, std::sort cutoff). RA
	// iterators only.
	template <typename ITER, typename COMP = std::less<>>
	void sort(ITER begin, ITER end, COMP comp = {})
	{
		context()->sort(begin, end, std::move(comp));
	}

	// Parallel merge of two sorted ranges into a third. Matches
	// std::merge's contract (stable: equivalent elements from A come
	// first). See Context::merge for the co-rank algorithm.
	template <typename IterA, typename IterB, typename OutIter,
	          typename Comp = std::less<>>
	void merge(IterA aBegin, IterA aEnd,
	           IterB bBegin, IterB bEnd,
	           OutIter outBegin,
	           Comp comp = {})
	{
		context()->merge(aBegin, aEnd, bBegin, bEnd, outBegin, std::move(comp));
	}
} // namespace multi