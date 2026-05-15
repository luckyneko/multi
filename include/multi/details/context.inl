
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
		// Forward declarations: definitions live in the second `details`
		// block below (after Context::sort, since chunkedSortImpl calls
		// ctx->merge which is itself declared on Context but not yet
		// defined at this point). Two-phase lookup needs qualified names
		// like `details::chunkedSortImpl` to resolve at template-parse
		// time even when the body is later in the file.
		template <class CtxT, class Iter, class Comp>
		void chunkedSortImpl(CtxT* ctx, Iter begin, Iter end, const Comp& comp);

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

			// Single-pass Dutch National Flag partition: classify each
			// element into [<pivot | ==pivot | >pivot] in one scan. The
			// previous implementation used two `std::partition` passes
			// (2N work for the partition step); this version is N work.
			// Invariants during the loop:
			//   [begin, p1) — already < pivot
			//   [p1, i)     — already == pivot
			//   [i, p2)     — not yet classified
			//   [p2, end)   — already > pivot
			Iter p1 = begin, i = begin, p2 = end;
			while (i < p2)
			{
				if (comp(*i, pivot))
				{
					std::iter_swap(p1, i);
					++p1;
					++i;
				}
				else if (comp(pivot, *i))
				{
					--p2;
					std::iter_swap(i, p2);
					// don't advance i — newly swapped-in element is
					// still unclassified
				}
				else
				{
					++i;
				}
			}

			// `parallel(a, b)` is fire-and-block, so reference captures of
			// begin/p1/p2/end/comp/cutoff are safe: this stack frame
			// outlives the dispatched tasks.
			ctx->parallel(
				[&]() { parallelSortImpl(ctx, begin, p1, comp, cutoff); },
				[&]() { parallelSortImpl(ctx, p2, end, comp, cutoff); });
		}

		// Parallel 3-way partition. Used only at the very top of
		// Context::sort, where the partition's O(N) scan is the largest
		// serial chunk on the caller's critical path. Below the top
		// level, partition cost is already split across workers (each
		// recursive call partitions a smaller range on its own thread),
		// so the recursion structure parallelises naturally.
		//
		// Algorithm: four phases.
		//   1) Count    — each chunk classifies its slice, records
		//                 {less, equal, greater} counts.
		//   2) Prefix   — serial scan over `chunkCount` counts to
		//                 assign each chunk a write offset within
		//                 the {less | equal | greater} regions.
		//   3) Scatter  — each chunk moves its elements into the
		//                 scratch buffer at the computed offsets.
		//   4) Move back — scratch → user's buffer.
		//
		// Memory cost: O(N) scratch (one buffer of `V` items). Required
		// because in-place parallel partition algorithms (Frias-Petit,
		// Tsigas-Zhang, etc.) need either atomics on both ends of the
		// array or a synchronisation barrier; the scratch-buffer version
		// is simpler, cache-friendly, and good enough at this level.
		// `V` must be default-constructible (the scratch vector
		// default-initialises its slots before scatter overwrites them).
		//
		// Returns {p1, p2} such that [begin, p1) < pivot,
		// [p1, p2) == pivot, [p2, end) > pivot.
		template <class CtxT, class Iter, class Comp, class V>
		std::pair<Iter, Iter> parallelPartition3way(
			CtxT* ctx, Iter begin, Iter end, const V& pivot, const Comp& comp)
		{
			using diff_t = typename std::iterator_traits<Iter>::difference_type;

			const std::size_t n = static_cast<std::size_t>(end - begin);
			const std::size_t tc = std::max<std::size_t>(1, ctx->threadCount());

			// Aim for ~tc chunks; min chunk size keeps per-task overhead
			// amortised over real work (~4096 items ≈ one L1 cache line
			// worth of int loads).
			const std::size_t chunkSize = std::max<std::size_t>(
				4096, (n + tc - 1) / tc);
			const std::size_t chunkCount = (n + chunkSize - 1) / chunkSize;

			// Per-chunk classification counts: {less, equal, greater}.
			std::vector<std::array<std::size_t, 3>> counts(chunkCount, std::array<std::size_t, 3>{0, 0, 0});

			// Phase 1 — count. Each chunk produces its triple.
			ctx->range(std::ptrdiff_t(0),
			           static_cast<std::ptrdiff_t>(chunkCount),
			           std::ptrdiff_t(1),
			           [&](std::ptrdiff_t cs) {
				const std::size_t c = static_cast<std::size_t>(cs);
				const std::size_t lo = c * chunkSize;
				const std::size_t hi = std::min(lo + chunkSize, n);
				std::size_t lt = 0, eq = 0, gt = 0;
				for (std::size_t i = lo; i < hi; ++i)
				{
					const V& x = begin[static_cast<diff_t>(i)];
					if (comp(x, pivot)) ++lt;
					else if (comp(pivot, x)) ++gt;
					else ++eq;
				}
				counts[c] = {lt, eq, gt};
			});

			// Phase 2 — prefix sum (serial, O(chunkCount)). Computes
			// each chunk's starting offset within the {less | equal |
			// greater} regions of the output.
			std::size_t totalLt = 0, totalEq = 0, totalGt = 0;
			std::vector<std::array<std::size_t, 3>> offsets(chunkCount);
			for (std::size_t c = 0; c < chunkCount; ++c)
			{
				offsets[c] = {totalLt, totalEq, totalGt};
				totalLt += counts[c][0];
				totalEq += counts[c][1];
				totalGt += counts[c][2];
			}
			const std::size_t eqStart = totalLt;
			const std::size_t gtStart = totalLt + totalEq;

			// Scratch buffer — single allocation, default-constructed
			// slots overwritten by the scatter phase.
			std::vector<V> scratch(n);

			// Phase 3 — scatter. Each chunk moves its elements into
			// scratch at the offsets computed in phase 2. After this
			// pass the user's buffer holds moved-from elements; phase 4
			// restores it.
			ctx->range(std::ptrdiff_t(0),
			           static_cast<std::ptrdiff_t>(chunkCount),
			           std::ptrdiff_t(1),
			           [&](std::ptrdiff_t cs) {
				const std::size_t c = static_cast<std::size_t>(cs);
				const std::size_t lo = c * chunkSize;
				const std::size_t hi = std::min(lo + chunkSize, n);
				std::size_t lp = offsets[c][0];
				std::size_t ep = eqStart + offsets[c][1];
				std::size_t gp = gtStart + offsets[c][2];
				for (std::size_t i = lo; i < hi; ++i)
				{
					V& x = begin[static_cast<diff_t>(i)];
					if (comp(x, pivot)) scratch[lp++] = std::move(x);
					else if (comp(pivot, x)) scratch[gp++] = std::move(x);
					else scratch[ep++] = std::move(x);
				}
			});

			// Phase 4 — move back to the user's buffer. We chunk this
			// too so the bulk copy is parallel rather than serial on
			// the caller.
			ctx->range(std::ptrdiff_t(0),
			           static_cast<std::ptrdiff_t>(chunkCount),
			           std::ptrdiff_t(1),
			           [&](std::ptrdiff_t cs) {
				const std::size_t c = static_cast<std::size_t>(cs);
				const std::size_t lo = c * chunkSize;
				const std::size_t hi = std::min(lo + chunkSize, n);
				for (std::size_t i = lo; i < hi; ++i)
					begin[static_cast<diff_t>(i)] = std::move(scratch[i]);
			});

			return {begin + static_cast<diff_t>(eqStart),
			        begin + static_cast<diff_t>(gtStart)};
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
		// is ≈ log₂(workerCount·8). Floor of 4096 keeps tiny chunks out
		// of dispatch — measured break-even on M-class hardware is
		// around the 10k-item point, where the previous 1024 floor
		// produced ~3 levels of recursion (4 parallel-call dispatches
		// totalling 12–20 µs) on a 70 µs serial sort. With 4096, n=10k
		// produces just one parallel split — a single dispatch instead.
		//
		// Why bound *depth* and not *fan-out*: the calling thread spins
		// in `parallel(left, right)` and steals work while waiting. If a
		// stolen recursive sort spawns another `parallel`, the caller's
		// stack grows by another frame for each level. A linear cutoff
		// (e.g. fixed 4096) lets depth reach log₂(n/4096), which over
		// large inputs combined with ASan/UBSan stack-frame overhead can
		// run the main thread out of stack — the divide-by-`tc·8` keeps
		// depth bounded by ~log₂(tc·8) regardless of n.
		constexpr diff_t LEAF_FLOOR = 4096;
		const diff_t cutoff = std::max<diff_t>(
			LEAF_FLOOR,
			n / static_cast<diff_t>(std::max<std::size_t>(1, tc * 8)));

		// Sub-threshold short-circuit: when n is below ~2× the cutoff,
		// even a single recursive split produces chunks barely larger
		// than the std::sort fallback would handle anyway, and the
		// parallel-call dispatch + median-of-three setup cost exceeds
		// any parallelism benefit. Punt directly to std::sort.
		//
		// Also catches tc < 2 (no real parallelism available) and
		// short-circuits `tc == 0` (single-threaded inline mode) cleanly.
		if (tc < 2 || n < 2 * LEAF_FLOOR)
		{
			std::sort(begin, end, comp);
			return;
		}

		// Chunked-sort threshold: at and above this size, use the
		// chunked sort + parallel-merge path (`details::chunkedSortImpl`).
		// Below it, fall through to the recursive parallel-quicksort
		// path — chunked sort's K-way split + log₂(K) merge stages cost
		// O(K) bookkeeping that's only amortised by enough work in each
		// chunk.
		constexpr diff_t CHUNKED_SORT_THRESHOLD = 500'000;
		if (n >= CHUNKED_SORT_THRESHOLD)
		{
			details::chunkedSortImpl(this, begin, end, comp);
			return;
		}

		// Parallel-partition threshold: the top-level partition's
		// serial O(N) scan is the largest single chunk on the caller's
		// critical path, but the parallel version pays a scratch-buffer
		// allocation + a 4-phase dispatch. Above the threshold and with
		// at least 2 workers, parallel-partition the top level; below
		// it, fall through to the original serial-partition recursion
		// where the dispatch+scratch overhead would dominate.
		//
		// Above CHUNKED_SORT_THRESHOLD (500k) the chunked path takes
		// over entirely. This branch covers the 100k-500k band where
		// chunked's K=tc*4 chunks would be too small (~10k-50k each)
		// to amortise the log₂(K) merge stages.
		constexpr diff_t PARALLEL_PARTITION_THRESHOLD = 100'000;
		if (n >= PARALLEL_PARTITION_THRESHOLD && tc >= 2)
		{
			using V = typename std::iterator_traits<ITER>::value_type;

			// Median-of-three pivot at the top level. Mirrors the
			// in-recursion median-of-three so the two paths produce
			// equivalent pivot quality.
			ITER mid = begin + n / 2;
			ITER last = end - 1;
			if (comp(*mid, *begin)) std::iter_swap(begin, mid);
			if (comp(*last, *begin)) std::iter_swap(begin, last);
			if (comp(*last, *mid)) std::iter_swap(mid, last);
			V pivot = *mid;

			auto pr = details::parallelPartition3way(this, begin, end, pivot, comp);

			// Recurse on the two outer parts in parallel. Each recursive
			// call uses the serial-partition path; below the top level,
			// partition cost already parallelises via the recursion
			// structure (each branch runs on its own worker).
			this->parallel(
				[&]() { details::parallelSortImpl(this, begin, pr.first, comp, cutoff); },
				[&]() { details::parallelSortImpl(this, pr.second, end, comp, cutoff); });
			return;
		}

		details::parallelSortImpl(this, begin, end, comp, cutoff);
	}

	namespace details
	{
		// Chunked sort + iterative pairwise parallel merge.
		//
		// Strategy:
		//   1. K = workerCount * 4 chunks (balanced; first n%K chunks get
		//      one extra item). std::sort each chunk in parallel.
		//   2. log2(K) merge stages, ping-ponging between the user's
		//      buffer and a scratch vector. Each stage:
		//        - numPairs = runCount / 2 pair-merges
		//        - + 1 leftover run carried as-is when runCount is odd
		//        - For each pair, pick serial-vs-parallel per-merge based
		//          on numPairs: when numPairs >= workerCount, one task per
		//          pair (serial std::merge) gives full task parallelism;
		//          when numPairs < workerCount (late stages, few big
		//          merges), use multi::merge per pair so the merge itself
		//          parallelises across workers.
		//   3. If final result is in scratch (log2(K) odd), parallel-move
		//      back to user's buffer.
		//
		// Memory: O(N) scratch (`std::vector<V>`). Requires V default-
		// constructible (same as parallelPartition3way).
		template <class CtxT, class Iter, class Comp>
		void chunkedSortImpl(CtxT* ctx, Iter begin, Iter end, const Comp& comp)
		{
			using V      = typename std::iterator_traits<Iter>::value_type;
			using diff_t = typename std::iterator_traits<Iter>::difference_type;

			const diff_t n = std::distance(begin, end);
			const std::size_t tc = std::max<std::size_t>(2, ctx->threadCount());
			const std::ptrdiff_t tcSigned = static_cast<std::ptrdiff_t>(tc);

			// K = workerCount * 4 — oversubscribe so Phase 1's std::sort
			// chunks balance across workers (a 2x or 4x overcount keeps
			// any worker that finishes early busy with the next chunk).
			// Power-of-two-ness not required: leftover handling below
			// covers odd run counts.
			const std::size_t K = tc * 4;
			const std::ptrdiff_t KSigned = static_cast<std::ptrdiff_t>(K);

			// Balanced chunk boundaries. First (n % K) chunks get base+1
			// items, the rest get base — same distribution as
			// ChunkedRangeJob, keeps load even when n is not divisible by K.
			std::vector<diff_t> bounds(K + 1);
			{
				const diff_t base  = n / static_cast<diff_t>(K);
				const std::size_t extra = static_cast<std::size_t>(n % static_cast<diff_t>(K));
				for (std::size_t c = 0; c <= K; ++c)
				{
					bounds[c] = static_cast<diff_t>(c) * base +
					            static_cast<diff_t>(std::min(c, extra));
				}
			}

			// Phase 1: parallel std::sort each chunk in place (in user's
			// buffer). After this the user's buffer holds K sorted runs.
			ctx->range(std::ptrdiff_t(0), KSigned, std::ptrdiff_t(1),
			           [&](std::ptrdiff_t cs) {
				const std::size_t c = static_cast<std::size_t>(cs);
				std::sort(begin + bounds[c], begin + bounds[c + 1], comp);
			});

			// Phase 2: scratch buffer, ping-pong merge stages.
			std::vector<V> scratch(static_cast<std::size_t>(n));
			std::size_t runCount = K;
			bool dataInScratch = false;
			std::vector<diff_t> newBounds;
			newBounds.reserve(K + 1);

			while (runCount > 1)
			{
				const std::size_t numPairs = runCount / 2;
				const bool hasLeftover = (runCount % 2) == 1;

				// Per-pair merge. Inside-the-lambda branch on
				// dataInScratch is constant for this stage; compiler can
				// hoist or peel it. Two distinct std::merge instantiations
				// (one per direction) — that's expected.
				if (static_cast<std::ptrdiff_t>(numPairs) >= tcSigned)
				{
					// Plenty of pairs — one task per pair, serial merge.
					ctx->range(std::ptrdiff_t(0),
					           static_cast<std::ptrdiff_t>(numPairs),
					           std::ptrdiff_t(1),
					           [&](std::ptrdiff_t ps) {
						const std::size_t p = static_cast<std::size_t>(ps);
						const diff_t aLo = bounds[2 * p];
						const diff_t aHi = bounds[2 * p + 1];
						const diff_t bLo = aHi;
						const diff_t bHi = bounds[2 * p + 2];
						if (dataInScratch)
						{
							std::merge(scratch.begin() + aLo, scratch.begin() + aHi,
							           scratch.begin() + bLo, scratch.begin() + bHi,
							           begin + aLo, comp);
						}
						else
						{
							std::merge(begin + aLo, begin + aHi,
							           begin + bLo, begin + bHi,
							           scratch.begin() + aLo, comp);
						}
					});
				}
				else
				{
					// Few pairs, each potentially large — use multi::merge
					// per pair so workers parallelise inside each merge
					// rather than sitting idle while one big merge runs
					// serially. Serial loop over pairs (each multi::merge
					// dispatches its own K' tasks); pairs run sequentially
					// at the stage level but each one consumes the full
					// pool.
					for (std::size_t p = 0; p < numPairs; ++p)
					{
						const diff_t aLo = bounds[2 * p];
						const diff_t aHi = bounds[2 * p + 1];
						const diff_t bLo = aHi;
						const diff_t bHi = bounds[2 * p + 2];
						if (dataInScratch)
						{
							ctx->merge(scratch.begin() + aLo, scratch.begin() + aHi,
							           scratch.begin() + bLo, scratch.begin() + bHi,
							           begin + aLo, comp);
						}
						else
						{
							ctx->merge(begin + aLo, begin + aHi,
							           begin + bLo, begin + bHi,
							           scratch.begin() + aLo, comp);
						}
					}
				}

				// Leftover (odd runCount): bulk move it as-is to the dst
				// buffer so the next stage's merges all read from the
				// same buffer. Done serially on the caller — leftover is
				// at most one base-sized chunk (~n/K items), and one
				// std::move range call vectorises far better than
				// dispatching tc tiny tasks.
				if (hasLeftover)
				{
					const diff_t lo = bounds[runCount - 1];
					const diff_t hi = bounds[runCount];
					if (dataInScratch)
					{
						std::move(scratch.begin() + lo, scratch.begin() + hi,
						          begin + lo);
					}
					else
					{
						std::move(begin + lo, begin + hi,
						          scratch.begin() + lo);
					}
				}

				// Build the new bounds layout for the next stage.
				// new run p = merged pair p (or leftover if p == numPairs).
				newBounds.clear();
				for (std::size_t p = 0; p <= numPairs; ++p)
					newBounds.push_back(bounds[2 * p]);
				if (hasLeftover)
					newBounds.push_back(bounds[runCount]);

				bounds.swap(newBounds);
				runCount = bounds.size() - 1;
				dataInScratch = !dataInScratch;
			}

			// Final: if result is in scratch, parallel-move back to user's
			// buffer. K_blocks tasks each doing a bulk std::move on its
			// slice — same K=workerCount*4 as Phase 1, so each task gets
			// the same chunk it originally sorted. One range dispatch (K
			// tasks), each task does a vectorisable bulk std::move on
			// ~n/K items.
			if (dataInScratch)
			{
				ctx->range(std::ptrdiff_t(0), KSigned, std::ptrdiff_t(1),
				           [&](std::ptrdiff_t cs) {
					const std::size_t c = static_cast<std::size_t>(cs);
					// Use the latest bounds, not the original chunk bounds —
					// the final layout has fewer, larger runs. But for the
					// move-back we just split n into K equal-ish pieces;
					// the bound layout from Phase 1's chunks works.
					const diff_t lo = static_cast<diff_t>(c) *
					                  (n / static_cast<diff_t>(K)) +
					                  static_cast<diff_t>(std::min(c, static_cast<std::size_t>(n % static_cast<diff_t>(K))));
					const std::size_t cNext = c + 1;
					const diff_t hi = static_cast<diff_t>(cNext) *
					                  (n / static_cast<diff_t>(K)) +
					                  static_cast<diff_t>(std::min(cNext, static_cast<std::size_t>(n % static_cast<diff_t>(K))));
					std::move(scratch.begin() + lo, scratch.begin() + hi,
					          begin + lo);
				});
			}
		}

		// Co-rank binary search for parallel merge.
		//
		// Given two sorted ranges A[0..m) and B[0..n), and a target output
		// position k (0 <= k <= m+n) in the merged result, returns the
		// number of A elements `a_k` that belong in the first k positions
		// of the merge. The corresponding B count is `b_k = k - a_k`.
		//
		// Stability convention: equivalent elements from A precede those
		// from B (matches std::merge's contract). This is enforced by:
		//   - decreasing a_k when A[a_k - 1] > B[b_k]   (strict)
		//   - increasing a_k when B[b_k - 1] >= A[a_k]  (>= biases A's side)
		//
		// Converges in O(log min(m, n)) per call. The two-sided shrinking
		// window (`iLow`, `jLow`) gives the log bound — each iteration at
		// least halves the search interval on one side.
		template <class IterA, class IterB, class Comp>
		typename std::iterator_traits<IterA>::difference_type
		coRank(typename std::iterator_traits<IterA>::difference_type k,
		       IterA a, typename std::iterator_traits<IterA>::difference_type m,
		       IterB b, typename std::iterator_traits<IterB>::difference_type n,
		       const Comp& comp)
		{
			using diff_t = typename std::iterator_traits<IterA>::difference_type;
			diff_t i = std::min<diff_t>(k, m);
			diff_t j = k - i;
			diff_t iLow = std::max<diff_t>(0, k - n);
			diff_t jLow = std::max<diff_t>(0, k - m);
			while (true)
			{
				if (i > 0 && j < n && comp(b[j], a[i - 1]))
				{
					// A[i-1] > B[j] under comp: A's last consumed element
					// is greater than B's next candidate — `i` is too large.
					// Shrink toward iLow.
					const diff_t delta = (i - iLow + 1) / 2;
					jLow = j;
					j += delta;
					i -= delta;
				}
				else if (j > 0 && i < m && !comp(b[j - 1], a[i]))
				{
					// B[j-1] >= A[i] under comp: B's last consumed element
					// is at least A's next candidate — for stable merge,
					// A[i] should have come first. Shrink toward jLow.
					const diff_t delta = (j - jLow + 1) / 2;
					iLow = i;
					i += delta;
					j -= delta;
				}
				else
				{
					return i;
				}
			}
		}
	} // namespace details

	template <typename IterA, typename IterB, typename OutIter, typename Comp>
	void Context::merge(IterA aBegin, IterA aEnd,
	                    IterB bBegin, IterB bEnd,
	                    OutIter outBegin,
	                    Comp comp)
	{
		using diff_t = typename std::iterator_traits<IterA>::difference_type;
		const diff_t m = std::distance(aBegin, aEnd);
		const diff_t n = std::distance(bBegin, bEnd);
		const diff_t total = m + n;
		const std::size_t tc = threadCount();

		// Below this total size, the co-rank setup + K-way dispatch
		// outweighs serial std::merge's vectorised pass. Number is
		// approximate; tune with bench if it matters.
		constexpr diff_t MERGE_PARALLEL_THRESHOLD = 8192;
		if (total < MERGE_PARALLEL_THRESHOLD || tc < 2)
		{
			std::merge(aBegin, aEnd, bBegin, bEnd, outBegin, comp);
			return;
		}

		// K = workerCount * 4 — oversubscribe modestly so load-imbalance
		// between chunks doesn't stall the merge. The co-rank algorithm
		// produces output chunks of equal size; input chunks may be
		// unequal but the work per chunk is proportional to its output
		// size, which is uniform.
		const std::size_t K = tc * 4;

		// Precompute K+1 split points. Position 0 is (0,0); position K
		// is (m,n). Interior splits via coRank — K-1 calls of O(log min(m,n))
		// each, runs in the main thread; K is small (~50) so this is
		// negligible vs the parallel merge work below.
		std::vector<diff_t> aSplit(K + 1), bSplit(K + 1);
		aSplit[0] = 0; bSplit[0] = 0;
		aSplit[K] = m; bSplit[K] = n;
		for (std::size_t c = 1; c < K; ++c)
		{
			// Use a 64-bit intermediate for c * total to avoid overflow
			// on 32-bit diff_t at very large N (unlikely but cheap).
			const diff_t k = static_cast<diff_t>(
				(static_cast<std::size_t>(total) * c) / K);
			const diff_t ai = details::coRank(k, aBegin, m, bBegin, n, comp);
			aSplit[c] = ai;
			bSplit[c] = k - ai;
		}

		// Parallel: each chunk merges its A-slice and B-slice into
		// the correct offset of the output range. Chunks don't
		// overlap, so no synchronisation is needed.
		this->range(std::ptrdiff_t(0),
		            static_cast<std::ptrdiff_t>(K),
		            std::ptrdiff_t(1),
		            [&](std::ptrdiff_t cs) {
			const std::size_t c = static_cast<std::size_t>(cs);
			const diff_t aLo = aSplit[c], aHi = aSplit[c + 1];
			const diff_t bLo = bSplit[c], bHi = bSplit[c + 1];
			const diff_t outLo = aLo + bLo;
			std::merge(aBegin + aLo, aBegin + aHi,
			           bBegin + bLo, bBegin + bHi,
			           outBegin + outLo, comp);
		});
	}
} // namespace multi
