
#include "multi/details/merge.h"
#include "multi/details/sort.h"

#include <algorithm>
#include <functional>
#include <iterator>
#include <memory>
#include <numeric>
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
			m_workerPool.submit(details::Task([&job]() { job.run(0); }));
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
								 { return details::Task([&job, i]() { job.run(i); }); });

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
		auto job = std::make_shared<details::AsyncJob<DecayedF, R>>(std::forward<F>(f));
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
		details::ParallelJob<std::decay_t<TASKS>...> job(std::forward<TASKS>(tasks)...);
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
		details::EachJob<ITER, std::remove_reference_t<FUNC>> job(begin, end, func);
		runQueueJob(job);
	}

	template <typename ITER, typename FUNC>
	void Context::each(size_t taskCount, ITER begin, ITER end, FUNC&& func)
	{
		details::ChunkedEachJob<ITER, std::remove_reference_t<FUNC>> job(taskCount, begin, end, func);
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
		details::RangeJob<IDX, std::remove_reference_t<FUNC>> job(begin, end, step, func);
		runQueueJob(job);
	}

	template <typename IDX, typename FUNC>
	void Context::range(size_t taskCount, IDX begin, IDX end, IDX step, FUNC&& func)
	{
		details::ChunkedRangeJob<IDX, std::remove_reference_t<FUNC>> job(taskCount, begin, end, step, func);
		runQueueJob(job);
	}

	template <typename ITER, typename T, typename BinaryOp>
	T Context::reduce(ITER begin, ITER end, T init, BinaryOp&& op)
	{
		// Small-N serial fallback. Parallel dispatch (~5-10 µs round-trip
		// even with fencedNotify wake) dominates below this band — measured
		// at 10k items: serial std::accumulate ~5.5 µs vs parallel ~30 µs
		// (0.17×); at 100k serial ~55 µs vs parallel ~41 µs (1.34×). The
		// threshold sits between the two. Applies even for moderately
		// expensive transforms: trig+sqrt at 10k items still loses
		// 0.73× because dispatch is the floor, not per-element work.
		//
		// Only applied for random-access iterators — for other categories
		// std::distance is O(n) and the parallel path already does a full
		// walk in TransformReduceJob::makeSetup, so the cost ordering is
		// unchanged. Users with very expensive ops at small N can force
		// parallel via the explicit-taskCount overload below.
		constexpr std::size_t REDUCE_SERIAL_THRESHOLD = 32768;
		constexpr bool isRA = std::is_base_of_v<std::random_access_iterator_tag,
			typename std::iterator_traits<ITER>::iterator_category>;
		if (threadCount() < 2)
			return std::accumulate(begin, end, std::move(init), std::forward<BinaryOp>(op));
		if constexpr (isRA)
		{
			if (static_cast<std::size_t>(std::distance(begin, end)) < REDUCE_SERIAL_THRESHOLD)
				return std::accumulate(begin, end, std::move(init), std::forward<BinaryOp>(op));
		}

		// Default chunk count: one chunk per worker. Caller can override
		// via the taskCount overload. Picked by measurement: more
		// oversubscription hurt simple sums (combine overhead) on the
		// arithmetic benches without measurable load-balance gain.
		return reduce(threadCount(), begin, end, std::move(init),
		               std::forward<BinaryOp>(op));
	}

	template <typename ITER, typename T, typename BinaryOp>
	T Context::reduce(size_t taskCount, ITER begin, ITER end, T init, BinaryOp&& op)
	{
		details::Identity identity;
		details::TransformReduceJob<ITER, T,
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
		// Small-N serial fallback — see Context::reduce for rationale.
		// Same 32k threshold: even with ~30 ns/element transforms, dispatch
		// overhead dominates below this size (measured 0.11× at 1k items,
		// 0.73× at 10k items on a trig+sqrt op).
		constexpr std::size_t REDUCE_SERIAL_THRESHOLD = 32768;
		constexpr bool isRA = std::is_base_of_v<std::random_access_iterator_tag,
			typename std::iterator_traits<ITER>::iterator_category>;
		if (threadCount() < 2)
			return std::transform_reduce(begin, end, std::move(init),
			                              std::forward<BinaryOp>(reduceOp),
			                              std::forward<UnaryOp>(transformOp));
		if constexpr (isRA)
		{
			if (static_cast<std::size_t>(std::distance(begin, end)) < REDUCE_SERIAL_THRESHOLD)
				return std::transform_reduce(begin, end, std::move(init),
				                              std::forward<BinaryOp>(reduceOp),
				                              std::forward<UnaryOp>(transformOp));
		}

		return transformReduce(threadCount(), begin, end, std::move(init),
		                        std::forward<BinaryOp>(reduceOp),
		                        std::forward<UnaryOp>(transformOp));
	}

	template <typename ITER, typename T, typename BinaryOp, typename UnaryOp>
	T Context::transformReduce(size_t taskCount, ITER begin, ITER end, T init,
	                            BinaryOp&& reduceOp, UnaryOp&& transformOp)
	{
		details::TransformReduceJob<ITER, T,
		                   std::remove_reference_t<BinaryOp>,
		                   std::remove_reference_t<UnaryOp>>
			job(taskCount, begin, end, std::move(init), reduceOp, transformOp);
		runQueueJob(job);
		return job.finalize();
	}

	// Shared threshold for the elementwise / search primitives below.
	// Higher than `REDUCE_SERIAL_THRESHOLD` (32768) because these ops
	// have much cheaper per-element work than a reduce — a transform
	// writes one element, count_if does a predicate + 0/1 add, fill
	// stores a value. With SIMD-vectorisable scalar work the serial
	// path on M-class hardware processes 100k doubles in ~12 µs, well
	// below the ~25 µs parallel dispatch floor. Threshold tuned by
	// measurement on transform_unary / count_if / min_element benches:
	// 256k is the smallest power of 2 where parallel reliably beats
	// serial across the three shapes.
	//
	// Dispatch pattern shared by tier-1 wrappers: K = threadCount() chunks,
	// each calls the matching `std::*` algorithm on its slice. Calling the
	// library impl per chunk (rather than a per-index lambda inside
	// ChunkedRangeJob) keeps the inner loop in `std::*`'s own body, outside
	// `Job::runOne`'s try-catch — which lets the compiler vectorise the same
	// way it does for the serial path. Measured: ~2× win on count_if/1M,
	// ~3-4× win on min_element/1M vs the prior per-index implementations.
	namespace details
	{
		inline constexpr std::size_t ELEMENT_SERIAL_THRESHOLD = 262144;

		// Higher threshold for write-only ops (fill). std::fill on
		// contiguous memory becomes streaming-store / memset on every
		// modern toolchain, which already saturates single-core write
		// bandwidth — so a single core gets ~all of DRAM-write-bw on its
		// own. Splitting work across cores adds dispatch + cache-coherence
		// traffic without growing aggregate bandwidth, so parallel fill
		// loses below ~2M items on M-class hardware. 2097152 chosen by
		// measurement: 1M loses 0.94×; 10M wins 1.6×.
		inline constexpr std::size_t WRITE_SERIAL_THRESHOLD = 2097152;

		// Bounds of chunk `k` when `n` elements are split into `K` chunks
		// (balanced: first `n%K` chunks get one extra element). Mirrors
		// ChunkedRangeJob's distribution.
		template <class Diff>
		inline std::pair<Diff, Diff>
		chunkBounds(Diff n, std::size_t K, std::size_t k) noexcept
		{
			const Diff base  = n / static_cast<Diff>(K);
			const Diff extra = n % static_cast<Diff>(K);
			const Diff kS = static_cast<Diff>(k);
			const Diff kNext = static_cast<Diff>(k + 1);
			return {kS    * base + std::min<Diff>(kS,    extra),
			        kNext * base + std::min<Diff>(kNext, extra)};
		}
	}

	template <typename InputIt, typename OutputIt, typename UnaryOp>
	void Context::transform(InputIt begin, InputIt end, OutputIt outBegin, UnaryOp op)
	{
		static_assert(
			std::is_base_of_v<std::random_access_iterator_tag,
			                  typename std::iterator_traits<InputIt>::iterator_category>,
			"multi::transform input iterator must be random-access");
		static_assert(
			std::is_base_of_v<std::random_access_iterator_tag,
			                  typename std::iterator_traits<OutputIt>::iterator_category>,
			"multi::transform output iterator must be random-access");

		using diff_t = typename std::iterator_traits<InputIt>::difference_type;
		const diff_t n = std::distance(begin, end);
		if (threadCount() < 2 ||
		    static_cast<std::size_t>(n) < details::ELEMENT_SERIAL_THRESHOLD)
		{
			std::transform(begin, end, outBegin, op);
			return;
		}

		const std::size_t K = threadCount();
		range(diff_t(0), static_cast<diff_t>(K), diff_t(1), [&](diff_t ks) {
			const auto [lo, hi] = details::chunkBounds<diff_t>(n, K, static_cast<std::size_t>(ks));
			std::transform(begin + lo, begin + hi, outBegin + lo, op);
		});
	}

	template <typename InputIt1, typename InputIt2, typename OutputIt, typename BinaryOp>
	void Context::transform(InputIt1 first1, InputIt1 last1, InputIt2 first2,
	                         OutputIt outBegin, BinaryOp op)
	{
		static_assert(
			std::is_base_of_v<std::random_access_iterator_tag,
			                  typename std::iterator_traits<InputIt1>::iterator_category> &&
			std::is_base_of_v<std::random_access_iterator_tag,
			                  typename std::iterator_traits<InputIt2>::iterator_category> &&
			std::is_base_of_v<std::random_access_iterator_tag,
			                  typename std::iterator_traits<OutputIt>::iterator_category>,
			"multi::transform (binary) requires random-access iterators on all three ranges");

		using diff_t = typename std::iterator_traits<InputIt1>::difference_type;
		const diff_t n = std::distance(first1, last1);
		if (threadCount() < 2 ||
		    static_cast<std::size_t>(n) < details::ELEMENT_SERIAL_THRESHOLD)
		{
			std::transform(first1, last1, first2, outBegin, op);
			return;
		}

		const std::size_t K = threadCount();
		range(diff_t(0), static_cast<diff_t>(K), diff_t(1), [&](diff_t ks) {
			const auto [lo, hi] = details::chunkBounds<diff_t>(n, K, static_cast<std::size_t>(ks));
			std::transform(first1 + lo, first1 + hi, first2 + lo, outBegin + lo, op);
		});
	}

	template <typename ITER, typename T>
	void Context::fill(ITER begin, ITER end, const T& value)
	{
		static_assert(
			std::is_base_of_v<std::random_access_iterator_tag,
			                  typename std::iterator_traits<ITER>::iterator_category>,
			"multi::fill requires random-access iterators");

		using diff_t = typename std::iterator_traits<ITER>::difference_type;
		const diff_t n = std::distance(begin, end);
		if (threadCount() < 2 ||
		    static_cast<std::size_t>(n) < details::WRITE_SERIAL_THRESHOLD)
		{
			std::fill(begin, end, value);
			return;
		}

		const std::size_t K = threadCount();
		range(diff_t(0), static_cast<diff_t>(K), diff_t(1), [&](diff_t ks) {
			const auto [lo, hi] = details::chunkBounds<diff_t>(n, K, static_cast<std::size_t>(ks));
			std::fill(begin + lo, begin + hi, value);
		});
	}

	template <typename ITER, typename Generator>
	void Context::generate(ITER begin, ITER end, Generator gen)
	{
		static_assert(
			std::is_base_of_v<std::random_access_iterator_tag,
			                  typename std::iterator_traits<ITER>::iterator_category>,
			"multi::generate requires random-access iterators");

		using diff_t = typename std::iterator_traits<ITER>::difference_type;
		const diff_t n = std::distance(begin, end);
		if (threadCount() < 2 ||
		    static_cast<std::size_t>(n) < details::ELEMENT_SERIAL_THRESHOLD)
		{
			std::generate(begin, end, gen);
			return;
		}

		// `gen` is called concurrently — caller's responsibility to make it
		// thread-safe. Documented in the declaration.
		const std::size_t K = threadCount();
		range(diff_t(0), static_cast<diff_t>(K), diff_t(1), [&](diff_t ks) {
			const auto [lo, hi] = details::chunkBounds<diff_t>(n, K, static_cast<std::size_t>(ks));
			std::generate(begin + lo, begin + hi, gen);
		});
	}

	template <typename ITER, typename T>
	void Context::replace(ITER begin, ITER end, const T& oldValue, const T& newValue)
	{
		static_assert(
			std::is_base_of_v<std::random_access_iterator_tag,
			                  typename std::iterator_traits<ITER>::iterator_category>,
			"multi::replace requires random-access iterators");

		using diff_t = typename std::iterator_traits<ITER>::difference_type;
		const diff_t n = std::distance(begin, end);
		if (threadCount() < 2 ||
		    static_cast<std::size_t>(n) < details::ELEMENT_SERIAL_THRESHOLD)
		{
			std::replace(begin, end, oldValue, newValue);
			return;
		}

		const std::size_t K = threadCount();
		range(diff_t(0), static_cast<diff_t>(K), diff_t(1), [&](diff_t ks) {
			const auto [lo, hi] = details::chunkBounds<diff_t>(n, K, static_cast<std::size_t>(ks));
			std::replace(begin + lo, begin + hi, oldValue, newValue);
		});
	}

	template <typename ITER, typename UnaryPred, typename T>
	void Context::replace_if(ITER begin, ITER end, UnaryPred pred, const T& newValue)
	{
		static_assert(
			std::is_base_of_v<std::random_access_iterator_tag,
			                  typename std::iterator_traits<ITER>::iterator_category>,
			"multi::replace_if requires random-access iterators");

		using diff_t = typename std::iterator_traits<ITER>::difference_type;
		const diff_t n = std::distance(begin, end);
		if (threadCount() < 2 ||
		    static_cast<std::size_t>(n) < details::ELEMENT_SERIAL_THRESHOLD)
		{
			std::replace_if(begin, end, pred, newValue);
			return;
		}

		const std::size_t K = threadCount();
		range(diff_t(0), static_cast<diff_t>(K), diff_t(1), [&](diff_t ks) {
			const auto [lo, hi] = details::chunkBounds<diff_t>(n, K, static_cast<std::size_t>(ks));
			std::replace_if(begin + lo, begin + hi, pred, newValue);
		});
	}

	template <typename ITER, typename T>
	typename std::iterator_traits<ITER>::difference_type
	Context::count(ITER begin, ITER end, const T& value)
	{
		using diff_t = typename std::iterator_traits<ITER>::difference_type;
		const diff_t n = std::distance(begin, end);
		if (threadCount() < 2 ||
		    static_cast<std::size_t>(n) < static_cast<diff_t>(details::ELEMENT_SERIAL_THRESHOLD))
			return std::count(begin, end, value);

		const std::size_t K = threadCount();
		std::vector<diff_t> partials(K, 0);
		range(diff_t(0), static_cast<diff_t>(K), diff_t(1), [&](diff_t ks) {
			const std::size_t k = static_cast<std::size_t>(ks);
			const auto [lo, hi] = details::chunkBounds<diff_t>(n, K, k);
			partials[k] = std::count(begin + lo, begin + hi, value);
		});
		diff_t total = 0;
		for (diff_t c : partials) total += c;
		return total;
	}

	template <typename ITER, typename UnaryPred>
	typename std::iterator_traits<ITER>::difference_type
	Context::count_if(ITER begin, ITER end, UnaryPred pred)
	{
		using diff_t = typename std::iterator_traits<ITER>::difference_type;
		const diff_t n = std::distance(begin, end);
		if (threadCount() < 2 ||
		    static_cast<std::size_t>(n) < static_cast<diff_t>(details::ELEMENT_SERIAL_THRESHOLD))
			return std::count_if(begin, end, pred);

		const std::size_t K = threadCount();
		std::vector<diff_t> partials(K, 0);
		range(diff_t(0), static_cast<diff_t>(K), diff_t(1), [&](diff_t ks) {
			const std::size_t k = static_cast<std::size_t>(ks);
			const auto [lo, hi] = details::chunkBounds<diff_t>(n, K, k);
			partials[k] = std::count_if(begin + lo, begin + hi, pred);
		});
		diff_t total = 0;
		for (diff_t c : partials) total += c;
		return total;
	}

	template <typename ITER, typename Comp>
	ITER Context::min_element(ITER begin, ITER end, Comp comp)
	{
		static_assert(
			std::is_base_of_v<std::random_access_iterator_tag,
			                  typename std::iterator_traits<ITER>::iterator_category>,
			"multi::min_element requires random-access iterators");

		using diff_t = typename std::iterator_traits<ITER>::difference_type;
		const diff_t n = std::distance(begin, end);
		if (n <= 0)
			return end;
		if (threadCount() < 2 ||
		    static_cast<std::size_t>(n) < details::ELEMENT_SERIAL_THRESHOLD)
			return std::min_element(begin, end, comp);

		// K chunks, each runs std::min_element on its slice (gets the lib's
		// vectorised scalar reduction). Serial combine keeps the earlier
		// chunk on tie → first occurrence overall.
		const std::size_t K = threadCount();
		std::vector<diff_t> localMin(K);
		range(diff_t(0), static_cast<diff_t>(K), diff_t(1), [&](diff_t ks) {
			const std::size_t k = static_cast<std::size_t>(ks);
			const auto [lo, hi] = details::chunkBounds<diff_t>(n, K, k);
			localMin[k] = std::min_element(begin + lo, begin + hi, comp) - begin;
		});

		diff_t best = localMin[0];
		for (std::size_t k = 1; k < K; ++k)
		{
			const diff_t cand = localMin[k];
			if (comp(begin[cand], begin[best])) best = cand;
		}
		return begin + best;
	}

	template <typename ITER, typename Comp>
	ITER Context::max_element(ITER begin, ITER end, Comp comp)
	{
		static_assert(
			std::is_base_of_v<std::random_access_iterator_tag,
			                  typename std::iterator_traits<ITER>::iterator_category>,
			"multi::max_element requires random-access iterators");

		using diff_t = typename std::iterator_traits<ITER>::difference_type;
		const diff_t n = std::distance(begin, end);
		if (n <= 0)
			return end;
		if (threadCount() < 2 ||
		    static_cast<std::size_t>(n) < details::ELEMENT_SERIAL_THRESHOLD)
			return std::max_element(begin, end, comp);

		// Mirror of min_element using std::max_element per chunk. Cannot
		// reuse min_element(swap-comp) here without losing std::*'s
		// hand-tuned max reduction; spelling it out keeps the inner loop
		// in libc++'s body. Tie-breaking: std::max_element returns first
		// occurrence; serial combine across chunks `comp(best, cand)` —
		// strict — keeps earlier chunk on tie. Both match std::max_element.
		const std::size_t K = threadCount();
		std::vector<diff_t> localMax(K);
		range(diff_t(0), static_cast<diff_t>(K), diff_t(1), [&](diff_t ks) {
			const std::size_t k = static_cast<std::size_t>(ks);
			const auto [lo, hi] = details::chunkBounds<diff_t>(n, K, k);
			localMax[k] = std::max_element(begin + lo, begin + hi, comp) - begin;
		});

		diff_t best = localMax[0];
		for (std::size_t k = 1; k < K; ++k)
		{
			const diff_t cand = localMax[k];
			if (comp(begin[best], begin[cand])) best = cand;
		}
		return begin + best;
	}

	template <typename ITER, typename Comp>
	std::pair<ITER, ITER> Context::minmax_element(ITER begin, ITER end, Comp comp)
	{
		static_assert(
			std::is_base_of_v<std::random_access_iterator_tag,
			                  typename std::iterator_traits<ITER>::iterator_category>,
			"multi::minmax_element requires random-access iterators");

		using diff_t = typename std::iterator_traits<ITER>::difference_type;
		const diff_t n = std::distance(begin, end);
		if (n <= 0)
			return {end, end};
		if (threadCount() < 2 ||
		    static_cast<std::size_t>(n) < details::ELEMENT_SERIAL_THRESHOLD)
		{
			auto p = std::minmax_element(begin, end, comp);
			return {p.first, p.second};
		}

		// std::minmax_element returns first-min, last-max per chunk — same
		// tie rules we want overall. Combine: keep earlier chunk for min
		// (strict `<` on combine), keep later chunk for max (non-strict
		// `>=` via `!comp(cand, best)`).
		const std::size_t K = threadCount();
		std::vector<diff_t> localMin(K), localMax(K);
		range(diff_t(0), static_cast<diff_t>(K), diff_t(1), [&](diff_t ks) {
			const std::size_t k = static_cast<std::size_t>(ks);
			const auto [lo, hi] = details::chunkBounds<diff_t>(n, K, k);
			auto p = std::minmax_element(begin + lo, begin + hi, comp);
			localMin[k] = p.first  - begin;
			localMax[k] = p.second - begin;
		});

		diff_t bMin = localMin[0];
		diff_t bMax = localMax[0];
		for (std::size_t k = 1; k < K; ++k)
		{
			if (comp(begin[localMin[k]], begin[bMin])) bMin = localMin[k];
			if (!comp(begin[localMax[k]], begin[bMax])) bMax = localMax[k];
		}
		return {begin + bMin, begin + bMax};
	}

	template <typename ITER, typename COMP>
	void Context::sort(ITER begin, ITER end, COMP comp)
	{
		static_assert(
			std::is_base_of_v<std::random_access_iterator_tag,
			                  typename std::iterator_traits<ITER>::iterator_category>,
			"multi::sort requires random-access iterators (matches std::sort)");

		details::Sorter<Context, ITER, COMP> sorter(this, begin, end, std::move(comp));
		sorter.run();
	}

	template <typename IterA, typename IterB, typename OutIter, typename Comp>
	void Context::merge(IterA aBegin, IterA aEnd,
	                    IterB bBegin, IterB bEnd,
	                    OutIter outBegin,
	                    Comp comp)
	{
		details::Merger<Context, IterA, IterB, OutIter, Comp> merger(
			this, aBegin, aEnd, bBegin, bEnd, outBegin, std::move(comp));
		merger.run();
	}
} // namespace multi
