/*
 *  Created by LuckyNeko on 17/10/2021.
 *  Copyright 2021 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

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

namespace multi::details
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
	 * Context::each(begin, end, func).
	 *
	 * Storage depends on iterator category. For random-access iterators we
	 * store the begin iterator directly and index with m_storage[i] — no
	 * allocation. For other iterator categories (e.g. std::map's
	 * bidirectional) we materialise a std::vector<T*> at construction so
	 * dispatch stays O(1) per task. Job owns whichever storage; lifetime
	 * is guaranteed by runQueueJob blocking until completion.
	 */
	template <class ITER, class FUNC>
	class EachJob : public Job
	{
		using ItemT = std::remove_reference_t<decltype(*std::declval<ITER>())>;
		static constexpr bool isRandomAccess = std::is_base_of_v<
			std::random_access_iterator_tag,
			typename std::iterator_traits<ITER>::iterator_category>;
		using Storage = std::conditional_t<isRandomAccess, ITER, std::vector<ItemT*>>;

	public:
		EachJob(ITER begin, ITER end, FUNC& func)
			: EachJob(makeStorage(begin, end), countOf(begin, end), func)
		{
		}

		void run(std::size_t i) noexcept
		{
			runOne([&]() {
				if constexpr (isRandomAccess)
					(*m_func)(m_storage[static_cast<typename std::iterator_traits<ITER>::difference_type>(i)]);
				else
					(*m_func)(*m_storage[i]);
			});
		}

	private:
		static Storage makeStorage(ITER begin, ITER end)
		{
			if constexpr (isRandomAccess)
			{
				(void)end;
				return begin;
			}
			else
			{
				std::vector<ItemT*> v;
				for (ITER it = begin; it != end; ++it)
					v.push_back(&(*it));
				return v;
			}
		}

		static std::size_t countOf(ITER begin, ITER end)
		{
			const auto d = std::distance(begin, end);
			return d > 0 ? static_cast<std::size_t>(d) : 0;
		}

		EachJob(Storage&& s, std::size_t count, FUNC& func)
			: Job(count), m_storage(std::move(s)), m_func(&func)
		{
		}

		Storage m_storage;
		FUNC* m_func;
	};

	/*
	 * ChunkedEachJob — taskCount tasks each iterating a slice of the items.
	 * Mirrors Context::each(taskCount, begin, end, func). Same chunk
	 * distribution and taskCount normalisation as ChunkedRangeJob, and the
	 * same iterator-category split as EachJob: random-access iterators are
	 * stored directly (no allocation), other categories materialise a
	 * std::vector<T*>.
	 */
	template <class ITER, class FUNC>
	class ChunkedEachJob : public Job
	{
		using ItemT = std::remove_reference_t<decltype(*std::declval<ITER>())>;
		static constexpr bool isRandomAccess = std::is_base_of_v<
			std::random_access_iterator_tag,
			typename std::iterator_traits<ITER>::iterator_category>;
		using Storage = std::conditional_t<isRandomAccess, ITER, std::vector<ItemT*>>;

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
				{
					if constexpr (isRandomAccess)
						(*m_func)(m_storage[static_cast<typename std::iterator_traits<ITER>::difference_type>(startIdx + k)]);
					else
						(*m_func)(*m_storage[startIdx + k]);
				}
			});
		}

	private:
		struct Setup
		{
			Storage storage;
			std::size_t total;
			std::size_t effective;
		};

		static Setup makeSetup(std::size_t taskCount, ITER begin, ITER end)
		{
			Setup s;
			std::size_t total;
			if constexpr (isRandomAccess)
			{
				s.storage = begin;
				const auto d = std::distance(begin, end);
				total = d > 0 ? static_cast<std::size_t>(d) : 0;
			}
			else
			{
				for (ITER it = begin; it != end; ++it)
					s.storage.push_back(&(*it));
				total = s.storage.size();
			}
			s.total = total;
			s.effective = (total == 0)
				? 0
				: std::min(std::max<std::size_t>(taskCount, 1), total);
			return s;
		}

		// Delegating ctor: Setup is consumed here. Storage moves first, but
		// m_base/m_extra read from the Setup struct (not m_storage), so the
		// post-move state of Setup is irrelevant.
		ChunkedEachJob(Setup&& s, FUNC& func)
			: Job(s.effective)
			, m_storage(std::move(s.storage))
			, m_base(s.effective == 0 ? 0 : s.total / s.effective)
			, m_extra(s.effective == 0 ? 0 : s.total % s.effective)
			, m_func(&func)
		{
		}

		Storage m_storage;
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
	 *
	 * R is the functor's return type; defaults to whatever invoke_result_t<F>
	 * yields. For void R the promise is std::promise<void> as before; for
	 * non-void R the promise carries the value through to Handle<R>::get().
	 */
	template <class F, class R = std::invoke_result_t<F>>
	class AsyncJob : public Job
	{
	public:
		template <class G>
		explicit AsyncJob(G&& g) : Job(1), m_func(std::forward<G>(g))
		{
		}

		std::future<R> getFuture() { return m_promise.get_future(); }

		void run(std::size_t /*i*/) noexcept
		{
			try
			{
				if constexpr (std::is_void_v<R>)
				{
					m_func();
					m_promise.set_value();
				}
				else
				{
					m_promise.set_value(m_func());
				}
			}
			catch (...)
			{
				m_promise.set_exception(std::current_exception());
			}
		}

	private:
		std::promise<R> m_promise;
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
} // namespace multi::details
