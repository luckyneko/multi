
#include <algorithm>
#include <functional>
#include <iterator>
#include <memory>
#include <thread>
#include <type_traits>
#include <utility>

namespace multi
{
	// JobT must expose `taskCount()` (initial size_t), `remaining()` (atomic
	// size_t load), `run(size_t)` (non-virtual, noexcept), and
	// `rethrowIfFailed()`. The Job base provides all of these except run(),
	// which subclasses define directly. Templating on JobT keeps `job.run(i)`
	// a direct call inside the wrapper Task lambda — no virtual dispatch.
	template <class JobT>
	void Context::runQueueJob(JobT& job)
	{
		const std::size_t count = job.taskCount();
		if (count == 0)
			return;

		// Single-task fast path: run inline on the caller and skip submit/
		// wait entirely. Covers both the historical taskCount<=1 serial
		// fallback (ChunkedRangeJob/ChunkedEachJob normalise that to count=1)
		// and any other Job that happens to dispatch a single task.
		if (count == 1)
		{
			job.run(0);
			job.rethrowIfFailed();
			return;
		}

		// Two-task fast path (parallel(a, b) hits this every call): submit
		// task 0 to a worker so it can start in parallel, then run task 1
		// inline on the caller. Saves one push + fencedNotify + steal-loop
		// iteration vs the generic two-task submitBatch path. The spin
		// below still runs because we still need to wait for task 0 to
		// finish, but it has only one outstanding task to drain rather
		// than two.
		if (count == 2)
		{
			m_workerPool.submit(Task([&job]() { job.run(0); }));
			job.run(1);
			while (job.remaining() > 0)
			{
				if (!tryRunSteal())
					std::this_thread::yield();
			}
			job.rethrowIfFailed();
			return;
		}

		// Generator-based submitBatch constructs each wrapper Task at push
		// time, no intermediate vector. Wrapper is [&job, i] = 16 B (SBO fit).
		// `&job` carries the JobT type, so the inner job.run(i) is a direct
		// call resolved at compile time.
		m_workerPool.submitBatch(count, [&job](std::size_t i)
								 { return Task([&job, i]() { job.run(i); }); });

		// Caller participates by stealing while waiting. The release in
		// runOne() pairs with the acquire in remaining(), so every task's
		// stores (including m_firstException) are visible once the loop exits.
		while (job.remaining() > 0)
		{
			if (!tryRunSteal())
				std::this_thread::yield();
		}

		job.rethrowIfFailed();
	}

	// AsyncJob is heap-allocated via shared_ptr so its lifetime extends past
	// this call's stack frame (the wrapper Task captures the shared_ptr by
	// value, 16 B SBO fit). Doesn't go through runQueueJob — the caller
	// observes completion via the future on Handle, not by blocking here.
	//
	// Return type is deduced as Handle<R> where R = invoke_result_t<F>; the
	// `auto` lets the caller see `Handle<int>` from
	// `async([]{ return 42; })` and `Handle<void>` (a.k.a. `Handle<>`) from
	// `async([]{ ... })`.
	template <class F>
	auto Context::async(F&& f)
	{
		using DecayedF = std::decay_t<F>;
		using R = std::invoke_result_t<DecayedF>;
		auto job = std::make_shared<AsyncJob<DecayedF, R>>(std::forward<F>(f));
		auto fut = job->getFuture();
		m_workerPool.submit([job]() { job->run(0); });
		return Handle<R>(std::move(fut));
	}

	template <class Pred>
	void Context::waitUntil(Pred&& pred)
	{
		// Drain pending pool work while pred() reports "not yet". The
		// caller-side spin matches runQueueJob's wait loop, so a worker
		// thread sitting in waitUntil is indistinguishable from one
		// processing its own deque — useful when waiting on conditions
		// that aren't a single Handle (counter thresholds, batches of
		// async results, external events).
		while (!pred())
		{
			if (!tryRunSteal())
				std::this_thread::yield();
		}
	}

	template <class... Hs>
	void Context::waitAll(const Hs&... hs)
	{
		// Fold over &&: identity element is `true`, so the no-arg case
		// short-circuits to a no-op. Single-handle case (`waitAll(h)`) is
		// just a one-element fold. Every iteration re-evaluates all
		// handles' .complete() — that's cheap (each is an atomic load) and
		// avoids tracking per-handle state.
		waitUntil([&]() -> bool { return (hs.complete() && ...); });
	}

	template <class... Ts>
	void Context::waitAll(const std::tuple<Handle<Ts>...>& tup)
	{
		std::apply([this](const auto&... hs) { this->waitAll(hs...); }, tup);
	}

	template <class... Hs>
	std::size_t Context::waitAny(const Hs&... hs)
	{
		static_assert(sizeof...(Hs) > 0,
		              "Context::waitAny requires at least one handle");

		// `completed` sentinel = sizeof...(Hs) means "none observed yet".
		// The fold over || short-circuits on the first complete handle,
		// recording its index in `completed`. `i` is a manual counter
		// re-initialised each predicate call — the fold expression
		// doesn't give us pack indices directly, but each pack element
		// evaluates left-to-right so this counter tracks the source
		// position exactly.
		std::size_t completed = sizeof...(Hs);
		waitUntil([&]() -> bool {
			std::size_t i = 0;
			return ((hs.complete()
			             ? (completed = i, true)
			             : (++i, false)) || ...);
		});
		return completed;
	}

	template <class... Ts>
	std::size_t Context::waitAny(const std::tuple<Handle<Ts>...>& tup)
	{
		return std::apply([this](const auto&... hs) { return this->waitAny(hs...); }, tup);
	}

	template <typename... TASKS>
	void Context::parallel(TASKS&&... tasks)
	{
		ParallelJob<std::decay_t<TASKS>...> job(std::forward<TASKS>(tasks)...);
		runQueueJob(job);
	}

	template <typename... Fs>
	auto Context::parallelAsync(Fs&&... fs)
	{
		// Each async() returns a prvalue Handle<R> which the tuple stores
		// via move-construction (Handle is move-only, but std::make_tuple
		// move-binds prvalues into its elements). Tuple positions follow
		// source order; the *evaluation* order of the async() calls is
		// unspecified per C++17 [expr.call], so the submission order may
		// interleave — but that's already true of any sequence of async()
		// calls on the same pool and not observable via the returned
		// tuple (positions are bound to their corresponding `fs` slot,
		// not to whichever submit completed first).
		return std::make_tuple(async(std::forward<Fs>(fs))...);
	}

	template <typename ITER, typename FUNC>
	void Context::each(ITER begin, ITER end, FUNC&& func)
	{
		EachJob<ITER, std::remove_reference_t<FUNC>> job(begin, end, func);
		runQueueJob(job);
	}

	template <typename ITER, typename FUNC>
	void Context::each(size_t taskCount, ITER begin, ITER end, FUNC&& func)
	{
		ChunkedEachJob<ITER, std::remove_reference_t<FUNC>> job(taskCount, begin, end, func);
		runQueueJob(job);
	}

	template <typename IDX, typename FUNC>
	void Context::range(IDX begin, IDX end, FUNC&& func)
	{
		range(begin, end, IDX(1), std::forward<FUNC>(func));
	}

	template <typename IDX, typename FUNC>
	void Context::range(IDX begin, IDX end, IDX step, FUNC&& func)
	{
		RangeJob<IDX, std::remove_reference_t<FUNC>> job(begin, end, step, func);
		runQueueJob(job);
	}

	template <typename IDX, typename FUNC>
	void Context::range(size_t taskCount, IDX begin, IDX end, IDX step, FUNC&& func)
	{
		ChunkedRangeJob<IDX, std::remove_reference_t<FUNC>> job(taskCount, begin, end, step, func);
		runQueueJob(job);
	}

	template <typename ITER, typename T, typename BinaryOp>
	T Context::reduce(ITER begin, ITER end, T init, BinaryOp&& op)
	{
		// Default chunk count: one chunk per worker, minimum 1. Caller can
		// override via the taskCount overload. Picked by measurement: more
		// oversubscription hurt simple sums (combine overhead) on the
		// arithmetic benches without measurable load-balance gain.
		const size_t n = threadCount() > 0 ? threadCount() : 1;
		return reduce(n, begin, end, std::move(init), std::forward<BinaryOp>(op));
	}

	template <typename ITER, typename T, typename BinaryOp>
	T Context::reduce(size_t taskCount, ITER begin, ITER end, T init, BinaryOp&& op)
	{
		details::Identity identity;
		TransformReduceJob<ITER, T,
		                   std::remove_reference_t<BinaryOp>,
		                   details::Identity>
			job(taskCount, begin, end, std::move(init), op, identity);
		runQueueJob(job);
		return job.finalize();
	}

	template <typename ITER, typename T, typename BinaryOp, typename UnaryOp>
	T Context::transformReduce(ITER begin, ITER end, T init,
	                            BinaryOp&& reduceOp, UnaryOp&& transformOp)
	{
		const size_t n = threadCount() > 0 ? threadCount() : 1;
		return transformReduce(n, begin, end, std::move(init),
		                        std::forward<BinaryOp>(reduceOp),
		                        std::forward<UnaryOp>(transformOp));
	}

	template <typename ITER, typename T, typename BinaryOp, typename UnaryOp>
	T Context::transformReduce(size_t taskCount, ITER begin, ITER end, T init,
	                            BinaryOp&& reduceOp, UnaryOp&& transformOp)
	{
		TransformReduceJob<ITER, T,
		                   std::remove_reference_t<BinaryOp>,
		                   std::remove_reference_t<UnaryOp>>
			job(taskCount, begin, end, std::move(init), reduceOp, transformOp);
		runQueueJob(job);
		return job.finalize();
	}

	namespace details
	{
		// Parallel quicksort body. Templated on `CtxT` (always Context here)
		// so the recursive call resolves without re-naming the dispatcher;
		// keeps this routine independent of multi::context() so it works on
		// any Context instance, including test-local pools.
		//
		// Strategy: pick a median-of-three pivot from {begin, mid, last};
		// 3-way partition into [<pivot | ==pivot | >pivot]; recurse on the
		// two outer parts in parallel. The equal-to-pivot middle stays
		// where it is (already correctly placed). Stops parallel recursion
		// when a subrange falls under `cutoff` and delegates to std::sort.
		//
		// `cutoff` is computed at the top-level `Context::sort` entry from
		// total size and worker count, and threaded through recursion
		// unchanged. Bounded recursion depth (≈ log₂(workerCount·8)) keeps
		// the caller-stack safe even when the spin-and-steal participation
		// pulls every recursive task back onto the calling thread.
		template <class CtxT, class Iter, class Comp>
		void parallelSortImpl(CtxT* ctx, Iter begin, Iter end, const Comp& comp,
		                     typename std::iterator_traits<Iter>::difference_type cutoff)
		{
			using diff_t = typename std::iterator_traits<Iter>::difference_type;

			const diff_t n = std::distance(begin, end);
			if (n <= cutoff)
			{
				std::sort(begin, end, comp);
				return;
			}

			// Median-of-three: sort {*begin, *mid, *last} in place so *mid
			// becomes the median under `comp`. Defends against degenerate
			// O(n²) on already-sorted / reverse-sorted input.
			Iter mid = begin + n / 2;
			Iter last = end - 1;
			if (comp(*mid, *begin)) std::iter_swap(begin, mid);
			if (comp(*last, *begin)) std::iter_swap(begin, last);
			if (comp(*last, *mid)) std::iter_swap(mid, last);
			// `pivot` is a value copy — *mid may move during partition.
			auto pivot = *mid;

			// 3-way partition:
			//   p1 = first element NOT (x < pivot)  → [begin, p1) is < pivot
			//   p2 = first element NOT (x <= pivot) → [p1, p2) is == pivot,
			//                                        [p2, end) is > pivot
			// Handles duplicates well: a run of equal-to-pivot elements
			// lands in the middle and is excluded from recursion. Worst
			// case (all equal) returns after these two O(n) scans.
			auto p1 = std::partition(begin, end, [&](const auto& x) { return comp(x, pivot); });
			auto p2 = std::partition(p1, end, [&](const auto& x) { return !comp(pivot, x); });

			// `parallel(a, b)` is fire-and-block, so reference captures of
			// begin/p1/p2/end/comp/cutoff are safe: this stack frame
			// outlives the dispatched tasks.
			ctx->parallel(
				[&]() { parallelSortImpl(ctx, begin, p1, comp, cutoff); },
				[&]() { parallelSortImpl(ctx, p2, end, comp, cutoff); });
		}
	} // namespace details

	template <typename ITER, typename COMP>
	void Context::sort(ITER begin, ITER end, COMP comp)
	{
		static_assert(
			std::is_base_of_v<std::random_access_iterator_tag,
			                  typename std::iterator_traits<ITER>::iterator_category>,
			"multi::sort requires random-access iterators (matches std::sort)");

		using diff_t = typename std::iterator_traits<ITER>::difference_type;
		const diff_t n = std::distance(begin, end);
		const std::size_t tc = threadCount();

		// Cutoff scales with workerCount: aim for ~(workerCount * 8) leaf
		// chunks at most, so the worst-case caller-side recursion depth
		// is ≈ log₂(workerCount·8). Floor of 1024 keeps tiny chunks out
		// of dispatch and lets std::sort do the leaf work efficiently.
		//
		// Why bound *depth* and not *fan-out*: the calling thread spins
		// in `parallel(left, right)` and steals work while waiting. If a
		// stolen recursive sort spawns another `parallel`, the caller's
		// stack grows by another frame for each level. A linear cutoff
		// (e.g. fixed 1024) lets depth reach log₂(n/1024), which over
		// large inputs combined with ASan/UBSan stack-frame overhead can
		// run the main thread out of stack.
		const diff_t cutoff = std::max<diff_t>(
			1024,
			n / static_cast<diff_t>(std::max<std::size_t>(1, tc * 8)));

		details::parallelSortImpl(this, begin, end, comp, cutoff);
	}
} // namespace multi
