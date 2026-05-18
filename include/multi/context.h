/*
 *  Created by LuckyNeko on 02/10/2021.
 *  Copyright 2021 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "multi/handle.h"
#include "multi/details/job.h"
#include "multi/details/task.h"
#include "multi/details/workerpool.h"

#include <type_traits>

namespace multi
{
	/*
	 * Context
	 * Holds multi-wide state.
	 */
	class Context
	{
	public:
		Context() = default;
		Context(const Context&) = delete;
		~Context() = default;

		// Start `threadCount` worker threads. Idempotent only in the sense
		// that calling `start()` on a running pool is undefined; call
		// `stop()` first. Passing 0 leaves the pool inactive — every
		// dispatch primitive runs inline on the caller in that state.
		void start(size_t threadCount);

		// Stop all workers, draining any in-flight tasks first. Blocks
		// until every worker has exited. Safe to call on an inactive pool
		// (no-op). After `stop()`, the pool can be `start()`ed again.
		void stop();

		// Number of worker threads currently running. Returns 0 when the
		// pool is inactive — every dispatch primitive short-circuits to
		// inline execution in that state.
		size_t threadCount() const;

		// Launch task onto a thread.
		// @return Handle<R> to wait on; R is the functor's return type (void
		// for value-less tasks).
		template <class F>
		auto async(F&& f);

		// Launch parallel tasks
		template <typename... TASKS>
		void parallel(TASKS&&... tasks);

		// Fan out a heterogeneous pack of functors, each dispatched via
		// async(), and return their Handles as a tuple. Element i holds
		// the Handle for fs[i], typed on its individual invoke_result.
		//
		// Use this when you need per-task observation (different return
		// types, fail-fast on one branch, per-handle wait_for, ...). For
		// fire-and-block siblings with no result observation, prefer the
		// existing void-returning `parallel(...)`.
		//
		// Each task lives in its own AsyncJob — sibling exceptions are
		// isolated per handle, not aggregated. Pair with `waitAll(h)`
		// per element if you want the caller to participate while waiting.
		template <typename... Fs>
		auto parallelAsync(Fs&&... fs);

		// Launch task for each item
		// ITER is an iterator
		// FUNC is function void(T) or void(T&)
		template <typename ITER, typename FUNC>
		void each(ITER begin, ITER end, FUNC&& func);
		template <typename ITER, typename FUNC>
		void each(size_t taskCount, ITER begin, ITER end, FUNC&& func);

		// Launch task for each idx with step from begin < end
		// IDX is a POD-type
		// FUNC is void(IDX)
		template <typename IDX, typename FUNC>
		void range(IDX begin, IDX end, FUNC&& func);
		template <typename IDX, typename FUNC>
		void range(IDX begin, IDX end, IDX step, FUNC&& func);
		template <typename IDX, typename FUNC>
		void range(size_t taskCount, IDX begin, IDX end, IDX step, FUNC&& func);

		// Parallel transform: apply `op` to each element in [begin, end) and
		// write the result to `outBegin`. Output must have space for
		// `std::distance(begin, end)` elements and must not overlap the
		// input. Random-access iterators only (matches the parallel
		// `range`-based dispatch). Below a small-N threshold or with
		// `threadCount() < 2`, delegates to `std::transform`.
		template <typename InputIt, typename OutputIt, typename UnaryOp>
		void transform(InputIt begin, InputIt end, OutputIt outBegin, UnaryOp op);

		// Binary variant: apply `op` to corresponding pairs from
		// [first1, last1) and [first2, first2 + (last1 - first1)). Output
		// must have space for `last1 - first1` and must not overlap either
		// input.
		template <typename InputIt1, typename InputIt2, typename OutputIt, typename BinaryOp>
		void transform(InputIt1 first1, InputIt1 last1, InputIt2 first2,
		                OutputIt outBegin, BinaryOp op);

		// Parallel fill: assigns `value` to every element in [begin, end).
		// Random-access iterators only; below a small-N threshold, delegates
		// to `std::fill`.
		template <typename ITER, typename T>
		void fill(ITER begin, ITER end, const T& value);

		// Parallel generate: invokes `gen()` for each element in [begin, end)
		// and assigns the result. `gen` is called concurrently from multiple
		// worker threads — **it must be thread-safe** (no shared mutable
		// state without synchronisation). Below a small-N threshold,
		// delegates to `std::generate` which is serial.
		template <typename ITER, typename Generator>
		void generate(ITER begin, ITER end, Generator gen);

		// Parallel replace: assigns `newValue` to every element in
		// [begin, end) that compares equal to `oldValue`.
		template <typename ITER, typename T>
		void replace(ITER begin, ITER end, const T& oldValue, const T& newValue);

		// Parallel replace_if: assigns `newValue` to every element in
		// [begin, end) for which `pred(element)` returns true.
		template <typename ITER, typename UnaryPred, typename T>
		void replace_if(ITER begin, ITER end, UnaryPred pred, const T& newValue);

		// Parallel count: returns the number of elements in [begin, end)
		// equal to `value`. Backed by `transformReduce` — inherits its
		// small-N serial fallback.
		template <typename ITER, typename T>
		typename std::iterator_traits<ITER>::difference_type
		count(ITER begin, ITER end, const T& value);

		// Parallel count_if: returns the number of elements in [begin, end)
		// for which `pred(element)` returns true.
		template <typename ITER, typename UnaryPred>
		typename std::iterator_traits<ITER>::difference_type
		count_if(ITER begin, ITER end, UnaryPred pred);

		// Parallel min_element: returns an iterator to the smallest element
		// in [begin, end) under `comp` (std::less<> by default). On tie,
		// returns the iterator to the **first** such element (matches
		// `std::min_element`). Returns `end` for an empty range.
		// Random-access iterators only.
		template <typename ITER, typename Comp = std::less<>>
		ITER min_element(ITER begin, ITER end, Comp comp = {});

		// Parallel max_element: like min_element under the opposite
		// orientation. On tie, returns the iterator to the **first** such
		// element (matches `std::max_element`).
		template <typename ITER, typename Comp = std::less<>>
		ITER max_element(ITER begin, ITER end, Comp comp = {});

		// Parallel minmax_element: returns `{min_it, max_it}`. On tie:
		// `min_it` is the **first** occurrence; `max_it` is the **last**
		// occurrence (matches `std::minmax_element`). Returns `{end, end}`
		// for an empty range.
		template <typename ITER, typename Comp = std::less<>>
		std::pair<ITER, ITER> minmax_element(ITER begin, ITER end, Comp comp = {});

		// Parallel reduce over [begin, end). Returns the result of folding
		// the range with `op`, seeded by `init`. `op` must be associative
		// AND commutative — partials are combined in unspecified order.
		// T must be default-constructible (partials are stored in a vector).
		// Empty range returns `init`.
		//
		// No-taskCount overload picks threadCount() chunks (one per worker);
		// pass an explicit count to override.
		template <typename ITER, typename T, typename BinaryOp>
		T reduce(ITER begin, ITER end, T init, BinaryOp&& op);
		template <typename ITER, typename T, typename BinaryOp>
		T reduce(size_t taskCount, ITER begin, ITER end, T init, BinaryOp&& op);

		// Parallel transformReduce: apply `transformOp` to each element,
		// then reduce with `reduceOp`. Same associativity/commutativity
		// requirement on reduceOp; transformOp is called once per element.
		template <typename ITER, typename T, typename BinaryOp, typename UnaryOp>
		T transformReduce(ITER begin, ITER end, T init, BinaryOp&& reduceOp, UnaryOp&& transformOp);
		template <typename ITER, typename T, typename BinaryOp, typename UnaryOp>
		T transformReduce(size_t taskCount, ITER begin, ITER end, T init,
						  BinaryOp&& reduceOp, UnaryOp&& transformOp);

		// In-place parallel sort over [begin, end). Random-access
		// iterators only (matches std::sort). Algorithm: median-of-three
		// pivot, 3-way partition, recursive parallel on the < and >
		// subranges; falls through to std::sort once a subrange is small
		// enough that dispatch overhead would outweigh the parallelism.
		// COMP must be the strict-weak-ordering compare used by std::sort.
		template <typename ITER, typename COMP = std::less<>>
		void sort(ITER begin, ITER end, COMP comp = {});

		// Parallel merge of two sorted ranges [aBegin, aEnd) and
		// [bBegin, bEnd) into the range starting at outBegin. Output
		// must have space for (aEnd-aBegin) + (bEnd-bBegin) elements and
		// must not overlap either input. Stable: for equivalent elements
		// (neither comp(a, b) nor comp(b, a)), the element from the A
		// range appears first — matches std::merge's contract.
		//
		// Algorithm: co-rank binary search computes K balanced split
		// points across both inputs in O(K * log(min(m, n))); each
		// chunk then runs serial std::merge in parallel. Below
		// MERGE_PARALLEL_THRESHOLD or with workerCount < 2, delegates
		// directly to std::merge.
		template <typename IterA, typename IterB, typename OutIter,
				  typename Comp = std::less<>>
		void merge(IterA aBegin, IterA aEnd,
				   IterB bBegin, IterB bEnd,
				   OutIter outBegin,
				   Comp comp = {});

		// Block the calling thread until `pred()` returns true, helping
		// drain the pool in the meantime via tryRunSteal(). The underlying
		// primitive for waitAll / waitAny — also useful directly when the
		// wait condition isn't a Handle (atomic counters, external events,
		// etc.). Matches the spirit of std::condition_variable::wait_until's
		// predicate-form overload but participates in pool work rather
		// than sleeping on a cv.
		template <class Pred>
		void waitUntil(Pred&& pred);

		// Block (participating in stealing) until *every* handle in the
		// pack completes. Each Hs must be a Handle<T> for some T (the
		// types may differ). Does not rethrow — observe each handle's
		// value/exception with `.get()` / `.wait()` afterwards.
		//
		// Single-handle case is just `waitAll(h)`. Empty pack returns
		// immediately (fold over && of zero terms is the identity, `true`).
		template <class... Hs>
		void waitAll(const Hs&... hs);

		// Tuple overload — pairs with the std::tuple<Handle<R>...>
		// returned by parallelAsync(...). Forwards to the variadic via
		// std::apply.
		template <class... Ts>
		void waitAll(const std::tuple<Handle<Ts>...>& tup);

		// Block until at least one handle in the pack completes. Returns
		// the zero-based index of the first completed handle (in source
		// order). Requires sizeof...(Hs) > 0 — surfaced via static_assert.
		template <class... Hs>
		std::size_t waitAny(const Hs&... hs);

		// Tuple overload — see waitAll's tuple overload.
		template <class... Ts>
		std::size_t waitAny(const std::tuple<Handle<Ts>...>& tup);

	private:
		// Dispatch a Job. Three paths:
		//   - taskCount() == 0: no-op (invalid inputs or empty range).
		//   - taskCount() == 1: run inline on the caller, no submission.
		//   - taskCount() >  1: submit a batch of count wrappers and spin on
		//     remaining() while participating via tryRunSteal().
		// Rethrows the first captured exception on completion. Templated on
		// the concrete subclass so `job.run(i)` resolves directly without a
		// vtable hop. AsyncJob doesn't go through this entry — it's
		// heap-allocated and dispatched fire-and-forget via WorkerPool::submit.
		template <class JobT>
		void runQueueJob(JobT& job);

		// Try to run one stolen task. Returns true if a task was popped
		// from some worker's deque and executed (exceptions swallowed),
		// false if no work was available. Private because the public wait
		// primitives (waitUntil / waitAll / waitAny) cover the common
		// use case; expose if a real consumer needs the non-blocking
		// "run one if available" primitive directly.
		bool tryRunSteal();

	private:
		details::WorkerPool m_workerPool;
	};
} // namespace multi

#include "multi/details/context.inl"
