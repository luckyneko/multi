/*
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <vector>

// Parallel merge orchestrator + co-rank helper.
//
// Not a Job: `Merger` performs serial setup (K-1 coRank calls) and then one
// `ctx->range` dispatch. The Job pattern describes a single fan-out where
// each worker picks a slot via run(i) — Merger does its own dispatch
// internally, so it's an algorithm wrapper, not a Job. Mirrors the Job
// pattern's *outer* shape (construct, then `.run()`) for consistency with
// how context.inl dispatches Jobs.
//
// Templated on `CtxT` (always Context here) so the body can call back into
// `ctx->range` without naming `multi::context()`. Requires Context to be
// complete at instantiation time — include order in [multi/context.h]
// places this header after the Context class definition closes.

namespace multi::details
{
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

	// Merger
	// Parallel merge orchestrator. Construct, then call run().
	template <class CtxT, class IterA, class IterB, class OutIter, class Comp>
	class Merger
	{
	public:
		Merger(CtxT* ctx, IterA aBegin, IterA aEnd,
		       IterB bBegin, IterB bEnd, OutIter outBegin, Comp comp)
			: m_ctx(ctx)
			, m_aBegin(aBegin), m_aEnd(aEnd)
			, m_bBegin(bBegin), m_bEnd(bEnd)
			, m_outBegin(outBegin)
			, m_comp(std::move(comp))
		{
		}

		void run()
		{
			using diff_t = typename std::iterator_traits<IterA>::difference_type;
			const diff_t m = std::distance(m_aBegin, m_aEnd);
			const diff_t n = std::distance(m_bBegin, m_bEnd);
			const diff_t total = m + n;
			const std::size_t tc = m_ctx->threadCount();

			// Below this total size, the co-rank setup + K-way dispatch
			// outweighs serial std::merge's vectorised pass. Number is
			// approximate; tune with bench if it matters.
			constexpr diff_t MERGE_PARALLEL_THRESHOLD = 8192;
			if (total < MERGE_PARALLEL_THRESHOLD || tc < 2)
			{
				std::merge(m_aBegin, m_aEnd, m_bBegin, m_bEnd, m_outBegin, m_comp);
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
				const diff_t ai = coRank(k, m_aBegin, m, m_bBegin, n, m_comp);
				aSplit[c] = ai;
				bSplit[c] = k - ai;
			}

			// Parallel: each chunk merges its A-slice and B-slice into
			// the correct offset of the output range. Chunks don't
			// overlap, so no synchronisation is needed.
			m_ctx->range(std::ptrdiff_t(0),
			             static_cast<std::ptrdiff_t>(K),
			             std::ptrdiff_t(1),
			             [&](std::ptrdiff_t cs) {
				const std::size_t c = static_cast<std::size_t>(cs);
				const diff_t aLo = aSplit[c], aHi = aSplit[c + 1];
				const diff_t bLo = bSplit[c], bHi = bSplit[c + 1];
				const diff_t outLo = aLo + bLo;
				std::merge(m_aBegin + aLo, m_aBegin + aHi,
				           m_bBegin + bLo, m_bBegin + bHi,
				           m_outBegin + outLo, m_comp);
			});
		}

	private:
		CtxT*   m_ctx;
		IterA   m_aBegin;
		IterA   m_aEnd;
		IterB   m_bBegin;
		IterB   m_bEnd;
		OutIter m_outBegin;
		Comp    m_comp;
	};
} // namespace multi::details
