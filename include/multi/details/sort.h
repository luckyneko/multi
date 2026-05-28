/*
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <type_traits>
#include <utility>
#include <vector>

// Parallel sort orchestrator + helpers.
//
// Not a Job: `Sorter` performs multiple sequential dispatches (parallel-sort
// of chunks, then log2(K) merge stages, then a parallel move-back). The
// Job pattern describes a single fan-out — Sorter is an algorithm wrapper
// that drives many dispatches itself. Mirrors the Job pattern's *outer*
// shape (construct, then `.run()`) for consistency with how context.inl
// dispatches Jobs.
//
// Templated on `CtxT` (always Context here) so the body can call back into
// `ctx->parallel` / `ctx->range` / `ctx->merge` without naming
// `multi::context()`. Requires Context to be complete at instantiation
// time — include order in [multi/context.h] places this header after the
// Context class definition closes.

namespace multi::details
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
		static_assert(std::is_default_constructible_v<V>,
			"multi::sort: parallel partition requires value_type to be default-constructible "
			"(scratch buffer is default-initialized before scatter)");

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

		static_assert(std::is_default_constructible_v<V>,
			"multi::sort: chunked parallel sort requires value_type to be default-constructible "
			"(scratch buffer is default-initialized before merge stages)");

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

	// Sorter
	// Parallel sort orchestrator. Construct, then call run(). Picks one
	// of three strategies based on input size and worker count:
	//   - tiny ranges (or single-threaded) → std::sort
	//   - mid ranges → parallelSortImpl recursive quicksort (with
	//     parallelPartition3way at the top level above 100k)
	//   - large ranges (>= 500k) → chunkedSortImpl
	template <class CtxT, class Iter, class Comp>
	class Sorter
	{
	public:
		Sorter(CtxT* ctx, Iter begin, Iter end, Comp comp)
			: m_ctx(ctx), m_begin(begin), m_end(end), m_comp(std::move(comp))
		{
		}

		void run()
		{
			using diff_t = typename std::iterator_traits<Iter>::difference_type;
			const diff_t n = std::distance(m_begin, m_end);
			const std::size_t tc = m_ctx->threadCount();

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
				std::sort(m_begin, m_end, m_comp);
				return;
			}

			// Chunked-sort threshold: at and above this size, use the
			// chunked sort + parallel-merge path (`chunkedSortImpl`).
			// Below it, fall through to the recursive parallel-quicksort
			// path — chunked sort's K-way split + log₂(K) merge stages cost
			// O(K) bookkeeping that's only amortised by enough work in each
			// chunk.
			constexpr diff_t CHUNKED_SORT_THRESHOLD = 500'000;
			if (n >= CHUNKED_SORT_THRESHOLD)
			{
				chunkedSortImpl(m_ctx, m_begin, m_end, m_comp);
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
				using V = typename std::iterator_traits<Iter>::value_type;

				// Median-of-three pivot at the top level. Mirrors the
				// in-recursion median-of-three so the two paths produce
				// equivalent pivot quality.
				Iter mid = m_begin + n / 2;
				Iter last = m_end - 1;
				if (m_comp(*mid, *m_begin)) std::iter_swap(m_begin, mid);
				if (m_comp(*last, *m_begin)) std::iter_swap(m_begin, last);
				if (m_comp(*last, *mid)) std::iter_swap(mid, last);
				V pivot = *mid;

				auto pr = parallelPartition3way(m_ctx, m_begin, m_end, pivot, m_comp);

				// Recurse on the two outer parts in parallel. Each recursive
				// call uses the serial-partition path; below the top level,
				// partition cost already parallelises via the recursion
				// structure (each branch runs on its own worker).
				m_ctx->parallel(
					[&]() { parallelSortImpl(m_ctx, m_begin, pr.first, m_comp, cutoff); },
					[&]() { parallelSortImpl(m_ctx, pr.second, m_end, m_comp, cutoff); });
				return;
			}

			parallelSortImpl(m_ctx, m_begin, m_end, m_comp, cutoff);
		}

	private:
		CtxT* m_ctx;
		Iter  m_begin;
		Iter  m_end;
		Comp  m_comp;
	};
} // namespace multi::details
