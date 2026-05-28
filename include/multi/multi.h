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
	void range(size_t taskCount, IDX begin, IDX end, IDX step, FUNC&& func)
	{
		context()->range(taskCount, begin, end, step, std::forward<FUNC>(func));
	}

	// Parallel transform — see Context::transform. Output range must not
	// overlap input(s); random-access iterators required.
	template <typename InputIt, typename OutputIt, typename UnaryOp>
	void transform(InputIt begin, InputIt end, OutputIt outBegin, UnaryOp op)
	{
		context()->transform(begin, end, outBegin, std::move(op));
	}
	template <typename InputIt1, typename InputIt2, typename OutputIt, typename BinaryOp>
	void transform(InputIt1 first1, InputIt1 last1, InputIt2 first2,
	                OutputIt outBegin, BinaryOp op)
	{
		context()->transform(first1, last1, first2, outBegin, std::move(op));
	}

	// Parallel fill / generate / replace / replace_if — see Context for
	// thread-safety notes (especially `generate`: its callable is invoked
	// concurrently from worker threads).
	template <typename ITER, typename T>
	void fill(ITER begin, ITER end, const T& value)
	{
		context()->fill(begin, end, value);
	}
	template <typename ITER, typename Generator>
	void generate(ITER begin, ITER end, Generator gen)
	{
		context()->generate(begin, end, std::move(gen));
	}
	template <typename ITER, typename T>
	void replace(ITER begin, ITER end, const T& oldValue, const T& newValue)
	{
		context()->replace(begin, end, oldValue, newValue);
	}
	template <typename ITER, typename UnaryPred, typename T>
	void replace_if(ITER begin, ITER end, UnaryPred pred, const T& newValue)
	{
		context()->replace_if(begin, end, std::move(pred), newValue);
	}

	// Parallel count / count_if — see Context for random-access iterator
	// requirement and small-N serial fallback.
	template <typename ITER, typename T>
	typename std::iterator_traits<ITER>::difference_type
	count(ITER begin, ITER end, const T& value)
	{
		return context()->count(begin, end, value);
	}
	template <typename ITER, typename UnaryPred>
	typename std::iterator_traits<ITER>::difference_type
	count_if(ITER begin, ITER end, UnaryPred pred)
	{
		return context()->count_if(begin, end, std::move(pred));
	}

	// Parallel min_element / max_element / minmax_element — tie-breaking
	// matches std::* (min/max: first occurrence; minmax-max: last occurrence).
	template <typename ITER, typename Comp = std::less<>>
	ITER min_element(ITER begin, ITER end, Comp comp = {})
	{
		return context()->min_element(begin, end, std::move(comp));
	}
	template <typename ITER, typename Comp = std::less<>>
	ITER max_element(ITER begin, ITER end, Comp comp = {})
	{
		return context()->max_element(begin, end, std::move(comp));
	}
	template <typename ITER, typename Comp = std::less<>>
	std::pair<ITER, ITER> minmax_element(ITER begin, ITER end, Comp comp = {})
	{
		return context()->minmax_element(begin, end, std::move(comp));
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
	// iterators only; parallel scratch-buffer paths require default-
	// constructible value types.
	template <typename ITER, typename COMP = std::less<>>
	void sort(ITER begin, ITER end, COMP comp = {})
	{
		context()->sort(begin, end, std::move(comp));
	}

	// Parallel merge of two sorted ranges into a third. Random-access
	// iterators required for both inputs and output. Matches std::merge's
	// contract (stable: equivalent elements from A come first). See
	// Context::merge for the co-rank algorithm.
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
