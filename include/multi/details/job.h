/*
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#ifndef _MULTI_JOB_H_
#define _MULTI_JOB_H_

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <exception>
#include <future>
#include <iterator>
#include <mutex>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace multi
{
	/*
	 * Job
	 * Non-polymorphic state holder for batched dispatch (countdown, exception
	 * capture). Subclasses (in this header alongside the base) define a
	 * non-virtual `run(std::size_t i) noexcept` member; Context::runQueueJob
	 * is templated on the concrete subclass so the call resolves directly,
	 * no virtual indirection.
	 *
	 * Lifetime: the typical (sync) Job lives on the caller's stack inside an
	 * each/range/parallel call; runQueueJob blocks on remaining() until every
	 * run() invocation has completed before returning, so the wrappers
	 * dispatched into worker deques never outlive the Job. AsyncJob is the
	 * exception — it's heap-allocated via shared_ptr so the dispatched
	 * wrapper can outlive the calling stack frame.
	 *
	 * The wrapper Task pushed into worker deques is [&job, i]() { job.run(i); }
	 * (or, for AsyncJob, [job]() { job->run(0); } capturing a shared_ptr by
	 * value). Both fit Task's 24-byte SBO. Inside the lambda, `job` has the
	 * concrete subclass type, so `job.run(i)` is a direct call.
	 */
	class Job
	{
	public:
		// Initial number of run(i) invocations this Job will dispatch. Set
		// by the constructor, immutable thereafter. Used by runQueueJob to
		// decide between empty/inline/parallel paths and to size the batch.
		std::size_t taskCount() const noexcept
		{
			return m_taskCount;
		}

		// Acquire-load of the per-task countdown. Pairs with the release
		// fetch_sub inside runOne(): when a waiter observes remaining() == 0,
		// every preceding store from every task (including m_firstException)
		// is visible.
		std::size_t remaining() const noexcept
		{
			return m_remaining.load(std::memory_order_acquire);
		}

		// Caller-side rethrow after wait. No-op if no task threw.
		void rethrowIfFailed()
		{
			if (m_firstException)
				std::rethrow_exception(m_firstException);
		}

	protected:
		explicit Job(std::size_t count) noexcept
			: m_taskCount(count), m_remaining(count)
		{
		}

		// Protected non-virtual destructor: Job is never destroyed through a
		// base pointer. Each subclass is destroyed through its concrete type
		// (stack-owned, or `~AsyncJob<F>()` via shared_ptr).
		~Job() = default;

		Job(const Job&) = delete;
		Job& operator=(const Job&) = delete;

		// Standard subclass body: run user code under try/catch, capture the
		// first exception via call_once, and decrement the countdown. The
		// release on m_remaining is what publishes m_firstException to
		// waiters. AsyncJob deliberately does not call this — its exception
		// path goes through std::promise instead.
		template <class F>
		void runOne(F&& f) noexcept
		{
			try
			{
				std::forward<F>(f)();
			}
			catch (...)
			{
				std::call_once(m_excOnce, [&]()
							   { m_firstException = std::current_exception(); });
			}
			m_remaining.fetch_sub(1, std::memory_order_release);
		}

	private:
		std::size_t m_taskCount;
		std::atomic<std::size_t> m_remaining;
		std::once_flag m_excOnce;
		std::exception_ptr m_firstException;
	};

	/*
	 * RangeJob — one task per index of [begin, end) with the given step.
	 * Mirrors Context::range(begin, end, step, func) inputs. Computes its
	 * own count; returns count==0 for invalid inputs (step==0, end<=begin).
	 *
	 * Per-task dispatch uses the multiplied form `begin + i*step`. For
	 * floating-point IDX the iteration count is computed via the additive
	 * loop so it matches what an equivalent serial loop would produce; the
	 * dispatched values may differ in the last position by fp rounding, but
	 * the task count is what callers most often check against.
	 */
	template <class IDX, class FUNC>
	class RangeJob : public Job
	{
		static_assert(std::is_signed_v<IDX>, "multi::range: IDX must be a signed type; unsigned subtraction silently underflows");

	public:
		RangeJob(IDX begin, IDX end, IDX step, FUNC& func) noexcept
			: Job(computeCount(begin, end, step))
			, m_begin(begin), m_step(step), m_func(&func)
		{
		}

		void run(std::size_t i) noexcept
		{
			runOne([&]() { (*m_func)(m_begin + static_cast<IDX>(i) * m_step); });
		}

	private:
		static std::size_t computeCount(IDX begin, IDX end, IDX step) noexcept
		{
			if (step == 0 || end <= begin)
				return 0;
			if constexpr (std::is_integral_v<IDX>)
			{
				return static_cast<std::size_t>((end - begin + step - 1) / step);
			}
			else
			{
				std::size_t c = 0;
				for (IDX i = begin; i < end; i += step)
					++c;
				return c;
			}
		}

		IDX m_begin;
		IDX m_step;
		FUNC* m_func;
	};

	/*
	 * ChunkedRangeJob — taskCount tasks, each iterating a slice of [begin,end).
	 * Mirrors Context::range(taskCount, begin, end, step, func). Normalises
	 * taskCount=0 to 1 (single chunk over the whole range), and clamps to
	 * total when taskCount > total (one item per task).
	 *
	 * Distribution: first m_extra tasks get (m_base+1) items, the rest get
	 * m_base. Don't replace with simple ceiling division — for total=15,
	 * N=14 ceiling collapses to fewer chunks than requested.
	 */
	template <class IDX, class FUNC>
	class ChunkedRangeJob : public Job
	{
		static_assert(std::is_signed_v<IDX>, "multi::range: IDX must be a signed type; unsigned subtraction silently underflows");

	public:
		ChunkedRangeJob(std::size_t taskCount, IDX begin, IDX end, IDX step, FUNC& func) noexcept
			: ChunkedRangeJob(makeSetup(taskCount, begin, end, step), func)
		{
		}

		void run(std::size_t i) noexcept
		{
			const std::size_t startIdx = (i < m_extra)
				? i * (m_base + 1)
				: m_extra * (m_base + 1) + (i - m_extra) * m_base;
			const std::size_t len = (i < m_extra) ? m_base + 1 : m_base;
			const IDX innerBegin = m_begin + static_cast<IDX>(startIdx) * m_step;
			const IDX innerEnd   = innerBegin + static_cast<IDX>(len) * m_step;
			runOne([&]() {
				for (IDX j = innerBegin; j < innerEnd; j += m_step)
					(*m_func)(j);
			});
		}

	private:
		struct Setup
		{
			IDX begin;
			IDX step;
			std::size_t total;
			std::size_t effective;
		};

		static std::size_t computeTotal(IDX begin, IDX end, IDX step) noexcept
		{
			if (step == 0 || end <= begin)
				return 0;
			if constexpr (std::is_integral_v<IDX>)
			{
				return static_cast<std::size_t>((end - begin + step - 1) / step);
			}
			else
			{
				std::size_t c = 0;
				for (IDX i = begin; i < end; i += step)
					++c;
				return c;
			}
		}

		static Setup makeSetup(std::size_t taskCount, IDX begin, IDX end, IDX step) noexcept
		{
			const std::size_t total = computeTotal(begin, end, step);
			const std::size_t effective = (total == 0)
				? 0
				: std::min(std::max<std::size_t>(taskCount, 1), total);
			return Setup{begin, step, total, effective};
		}

		// Delegating ctor: Setup is computed once and consumed here.
		ChunkedRangeJob(Setup s, FUNC& func) noexcept
			: Job(s.effective)
			, m_begin(s.begin), m_step(s.step)
			, m_base(s.effective == 0 ? 0 : s.total / s.effective)
			, m_extra(s.effective == 0 ? 0 : s.total % s.effective)
			, m_func(&func)
		{
		}

		IDX m_begin;
		IDX m_step;
		std::size_t m_base;
		std::size_t m_extra;
		FUNC* m_func;
	};

	/*
	 * EachJob — one task per item in [begin, end). Mirrors
	 * Context::each(begin, end, func). Materialises a std::vector<T*>
	 * (T inferred from the iterator's reference type) so dispatch is O(1)
	 * per task regardless of iterator category. Job owns the vector — the
	 * pointer table outlives every wrapper because runQueueJob blocks
	 * until completion.
	 */
	template <class ITER, class FUNC>
	class EachJob : public Job
	{
		using ItemT = std::remove_reference_t<decltype(*std::declval<ITER>())>;

	public:
		EachJob(ITER begin, ITER end, FUNC& func)
			: EachJob(materialize(begin, end), func)
		{
		}

		void run(std::size_t i) noexcept
		{
			runOne([&]() { (*m_func)(*m_items[i]); });
		}

	private:
		static std::vector<ItemT*> materialize(ITER begin, ITER end)
		{
			std::vector<ItemT*> v;
			if constexpr (std::is_base_of_v<std::random_access_iterator_tag,
			                                typename std::iterator_traits<ITER>::iterator_category>)
			{
				v.reserve(static_cast<std::size_t>(std::distance(begin, end)));
			}
			for (ITER it = begin; it != end; ++it)
				v.push_back(&(*it));
			return v;
		}

		EachJob(std::vector<ItemT*>&& items, FUNC& func)
			: Job(items.size()), m_items(std::move(items)), m_func(&func)
		{
		}

		std::vector<ItemT*> m_items;
		FUNC* m_func;
	};

	/*
	 * ChunkedEachJob — taskCount tasks each iterating a slice of the items.
	 * Mirrors Context::each(taskCount, begin, end, func). Same chunk
	 * distribution and taskCount normalisation as ChunkedRangeJob.
	 */
	template <class ITER, class FUNC>
	class ChunkedEachJob : public Job
	{
		using ItemT = std::remove_reference_t<decltype(*std::declval<ITER>())>;

	public:
		ChunkedEachJob(std::size_t taskCount, ITER begin, ITER end, FUNC& func)
			: ChunkedEachJob(makeSetup(taskCount, begin, end), func)
		{
		}

		void run(std::size_t i) noexcept
		{
			const std::size_t startIdx = (i < m_extra)
				? i * (m_base + 1)
				: m_extra * (m_base + 1) + (i - m_extra) * m_base;
			const std::size_t len = (i < m_extra) ? m_base + 1 : m_base;
			runOne([&]() {
				for (std::size_t k = 0; k < len; ++k)
					(*m_func)(*m_items[startIdx + k]);
			});
		}

	private:
		struct Setup
		{
			std::vector<ItemT*> items;
			std::size_t effective;
		};

		static Setup makeSetup(std::size_t taskCount, ITER begin, ITER end)
		{
			Setup s;
			if constexpr (std::is_base_of_v<std::random_access_iterator_tag,
			                                typename std::iterator_traits<ITER>::iterator_category>)
			{
				s.items.reserve(static_cast<std::size_t>(std::distance(begin, end)));
			}
			for (ITER it = begin; it != end; ++it)
				s.items.push_back(&(*it));
			const std::size_t total = s.items.size();
			s.effective = (total == 0)
				? 0
				: std::min(std::max<std::size_t>(taskCount, 1), total);
			return s;
		}

		// Delegating ctor: Setup is consumed here. m_items moves before
		// m_base/m_extra are initialised, but they read m_items.size() which
		// is the post-move (i.e. final) size.
		ChunkedEachJob(Setup&& s, FUNC& func)
			: Job(s.effective)
			, m_items(std::move(s.items))
			, m_base(s.effective == 0 ? 0 : m_items.size() / s.effective)
			, m_extra(s.effective == 0 ? 0 : m_items.size() % s.effective)
			, m_func(&func)
		{
		}

		std::vector<ItemT*> m_items;
		std::size_t m_base;
		std::size_t m_extra;
		FUNC* m_func;
	};

	/*
	 * AsyncJob — single-task heap-allocated Job for Context::async.
	 * Lifetime extends past the calling stack frame, so it lives in a
	 * shared_ptr captured by the wrapper Task. The exception path goes
	 * through std::promise (Handle::wait observes it via the future), not
	 * through Job::m_firstException, so this subclass deliberately does
	 * not call runOne(). The inherited m_remaining is unused — the future
	 * is the synchronization point.
	 */
	template <class F>
	class AsyncJob : public Job
	{
	public:
		template <class G>
		explicit AsyncJob(G&& g) : Job(1), m_func(std::forward<G>(g))
		{
		}

		std::future<void> getFuture() { return m_promise.get_future(); }

		void run(std::size_t /*i*/) noexcept
		{
			try
			{
				m_func();
				m_promise.set_value();
			}
			catch (...)
			{
				m_promise.set_exception(std::current_exception());
			}
		}

	private:
		std::promise<void> m_promise;
		F m_func;
	};

	/*
	 * ParallelJob — variadic-pack dispatch. Holds the user functors in a
	 * tuple; run(i) dispatches to element i via a fold expression on a
	 * compile-time index pack. For typical N=2..4 the compiler turns the
	 * fold into a switch.
	 */
	template <class... Fs>
	class ParallelJob : public Job
	{
	public:
		template <class... Gs>
		explicit ParallelJob(Gs&&... gs)
			: Job(sizeof...(Gs)), m_funcs(std::forward<Gs>(gs)...)
		{
		}

		void run(std::size_t i) noexcept
		{
			runOne([&]() { dispatch(i, std::index_sequence_for<Fs...>{}); });
		}

	private:
		template <std::size_t... I>
		void dispatch(std::size_t i, std::index_sequence<I...>)
		{
			(void)((I == i ? (std::get<I>(m_funcs)(), true) : false) || ...);
		}

		std::tuple<Fs...> m_funcs;
	};
} // namespace multi

#endif // _MULTI_JOB_H_
