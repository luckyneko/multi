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

#include "multi/chunkpolicy.h"

namespace multi::details
{
	/*
	 * Job
	 * Non-polymorphic state holder for batched dispatch (countdown, exception
	 * capture). Subclasses define a non-virtual `run(std::size_t i) noexcept`;
	 * Context::runQueueJob is templated on the concrete subclass so the call
	 * resolves directly, no virtual indirection.
	 *
	 * A sync Job lives on the caller's stack; runQueueJob blocks until every
	 * run() has completed, so the wrappers dispatched into worker deques never
	 * outlive it. AsyncJob is the exception — heap-allocated via shared_ptr so
	 * its wrapper can outlive the calling frame.
	 */
	class Job
	{
	public:
		// Number of run(i) invocations this Job dispatches; set by the
		// constructor, immutable thereafter.
		std::size_t taskCount() const noexcept
		{
			return m_taskCount;
		}

		// Acquire-load of the countdown; pairs with the release in runOne() so
		// a waiter seeing 0 observes every task's stores (incl. m_firstException).
		std::size_t remaining() const noexcept
		{
			return m_remaining.load(std::memory_order_acquire);
		}

		// Rethrow the first captured exception, if any. Call after waiting.
		void rethrowIfFailed()
		{
			if (m_firstException)
				std::rethrow_exception(m_firstException);
		}

	protected:
		explicit Job(std::size_t count) noexcept
			: m_taskCount(count)
			, m_remaining(count)
		{
		}

		// Protected non-virtual: Job is never destroyed through a base pointer.
		~Job() = default;

		Job(const Job&) = delete;
		Job& operator=(const Job&) = delete;

		// Run user code under try/catch, capture the first exception, and
		// release-decrement the countdown (which publishes the exception to
		// waiters). AsyncJob bypasses this — its exception path is the promise.
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
	 * AsyncJob — single-task heap-allocated Job for Context::async, held in a
	 * shared_ptr so its lifetime outlasts the calling frame. The exception path
	 * goes through std::promise (observed via the Handle's future), not
	 * Job::runOne, so the inherited m_remaining is unused. R is the functor's
	 * return type (defaults to invoke_result_t<F>); void uses promise<void>.
	 */
	template <class F, class R = std::invoke_result_t<F>>
	class AsyncJob : public Job
	{
	public:
		template <class G>
		explicit AsyncJob(G&& g)
			: Job(1)
			, m_func(std::forward<G>(g))
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
	 * ParallelJob — variadic-pack dispatch. Holds the functors in a tuple;
	 * run(i) dispatches to element i via a fold over a compile-time index pack.
	 */
	template <class... Fs>
	class ParallelJob : public Job
	{
	public:
		template <class... Gs>
		explicit ParallelJob(Gs&&... gs)
			: Job(sizeof...(Gs))
			, m_funcs(std::forward<Gs>(gs)...)
		{
		}

		void run(std::size_t i) noexcept
		{
			runOne([&]()
				   { dispatch(i, std::index_sequence_for<Fs...>{}); });
		}

	private:
		template <std::size_t... I>
		void dispatch(std::size_t i, std::index_sequence<I...>)
		{
			(void)((I == i ? (std::get<I>(m_funcs)(), true) : false) || ...);
		}

		std::tuple<Fs...> m_funcs;
	};

	/*
	 * EachJob — chunkPolicy tasks, each over a slice of the items; mirrors
	 * Context::each. Same distribution and chunk-count normalisation as
	 * RangeJob. Iterator-category split via if constexpr: random-access
	 * iterators store the begin iterator directly (no allocation); other
	 * categories materialise a std::vector<T*> pointer table at construction.
	 * Per-item each() resolves to PerItem here (one chunk per item).
	 */
	template <class ITER, class FUNC>
	class EachJob : public Job
	{
		static constexpr bool isRandomAccess = std::is_base_of_v<
			std::random_access_iterator_tag,
			typename std::iterator_traits<ITER>::iterator_category>;
		using ItemT = std::remove_reference_t<decltype(*std::declval<ITER>())>;
		using Storage = std::conditional_t<isRandomAccess, ITER, std::vector<ItemT*>>;

	public:
		EachJob(ChunkPolicy chunkPolicy, std::size_t workers, ITER begin, ITER end, FUNC& func)
			: EachJob(makeSetup(chunkPolicy, workers, begin, end), func)
		{
		}

		void run(std::size_t i) noexcept
		{
			const std::size_t startIdx = (i < m_extra)
											 ? i * (m_base + 1)
											 : m_extra * (m_base + 1) + (i - m_extra) * m_base;
			const std::size_t len = (i < m_extra) ? m_base + 1 : m_base;
			runOne([&]()
				   {
				for (std::size_t k = 0; k < len; ++k)
				{
					if constexpr (isRandomAccess)
						(*m_func)(m_storage[static_cast<typename std::iterator_traits<ITER>::difference_type>(startIdx + k)]);
					else
						(*m_func)(*m_storage[startIdx + k]);
				} });
		}

	private:
		struct Setup
		{
			Storage storage;
			std::size_t total;
			std::size_t effective;
		};

		static Setup makeSetup(ChunkPolicy chunkPolicy, std::size_t workers, ITER begin, ITER end)
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
			s.effective = chunkPolicy.resolve(total, workers);
			return s;
		}

		EachJob(Setup&& s, FUNC& func)
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
	 * RangeJob — chunkPolicy tasks, each over a slice of [begin, end);
	 * mirrors the chunked Context::range. Normalises an exact 0 to 1 and clamps
	 * to total. First m_extra tasks get m_base+1 items, the rest m_base — not
	 * ceiling division (total=15, N=14 would collapse to fewer chunks).
	 */
	template <class IDX, class FUNC>
	class RangeJob : public Job
	{
		static_assert(std::is_signed_v<IDX>, "multi::range: IDX must be a signed type; unsigned subtraction silently underflows");

	public:
		RangeJob(ChunkPolicy chunkPolicy, std::size_t workers, IDX begin, IDX end, IDX step, FUNC& func) noexcept
			: RangeJob(makeSetup(chunkPolicy, workers, begin, end, step), func)
		{
		}

		void run(std::size_t i) noexcept
		{
			const std::size_t startIdx = (i < m_extra)
											 ? i * (m_base + 1)
											 : m_extra * (m_base + 1) + (i - m_extra) * m_base;
			const std::size_t len = (i < m_extra) ? m_base + 1 : m_base;
			const IDX innerBegin = m_begin + static_cast<IDX>(startIdx) * m_step;
			const IDX innerEnd = innerBegin + static_cast<IDX>(len) * m_step;
			runOne([&]()
				   {
				for (IDX j = innerBegin; j < innerEnd; j += m_step)
					(*m_func)(j); });
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

		static Setup makeSetup(ChunkPolicy chunkPolicy, std::size_t workers, IDX begin, IDX end, IDX step) noexcept
		{
			const std::size_t total = computeTotal(begin, end, step);
			const std::size_t effective = chunkPolicy.resolve(total, workers);
			return Setup{begin, step, total, effective};
		}

		RangeJob(Setup s, FUNC& func) noexcept
			: Job(s.effective)
			, m_begin(s.begin)
			, m_step(s.step)
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
} // namespace multi::details
