/*
 *  Created by LuckyNeko on 17/10/2021.
 *  Copyright 2021 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <iterator>
#include <memory>
#include <thread>
#include <type_traits>
#include <utility>

namespace multi
{
	// Heap-allocated via shared_ptr so its lifetime outlasts this stack frame;
	// completion is observed via the Handle's future, not by blocking here.
	template <class F>
	auto Context::async(F&& f)
	{
		using DecayedF = std::decay_t<F>;
		using R = std::invoke_result_t<DecayedF>;
		auto job = std::make_shared<details::AsyncJob<DecayedF, R>>(std::forward<F>(f));
		auto fut = job->getFuture();
		m_workerPool.submit([job]()
							{ job->run(0); });
		return Handle<R>(std::move(fut));
	}

	template <typename F0, typename F1, typename... Fs>
	auto Context::async(F0&& f0, F1&& f1, Fs&&... fs)
	{
		return std::make_tuple(async(std::forward<F0>(f0)),
							   async(std::forward<F1>(f1)),
							   async(std::forward<Fs>(fs))...);
	}

	template <typename... TASKS>
	void Context::parallel(TASKS&&... tasks)
	{
		details::ParallelJob<std::decay_t<TASKS>...> job(std::forward<TASKS>(tasks)...);
		runQueueJob(job);
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

	template <typename CONTAINER, typename FUNC>
	void Context::each(CONTAINER&& c, FUNC&& func)
	{
		using std::begin;
		using std::end;
		each(begin(c), end(c), std::forward<FUNC>(func));
	}

	template <typename CONTAINER, typename FUNC>
	void Context::each(size_t taskCount, CONTAINER&& c, FUNC&& func)
	{
		using std::begin;
		using std::end;
		each(taskCount, begin(c), end(c), std::forward<FUNC>(func));
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

	template <class... Hs>
	void Context::waitAll(const Hs&... hs)
	{
		// Fold over &&: the no-arg case short-circuits to a no-op (identity
		// `true`). Re-evaluating every handle's complete() each iteration is
		// cheap (an atomic load) and avoids tracking per-handle state.
		waitUntil([&]() -> bool
				  { return (hs.complete() && ...); });
	}

	template <class... Ts>
	void Context::waitAll(const std::tuple<Handle<Ts>...>& tup)
	{
		std::apply([this](const auto&... hs)
				   { this->waitAll(hs...); }, tup);
	}

	template <class... Hs>
	std::size_t Context::waitAny(const Hs&... hs)
	{
		static_assert(sizeof...(Hs) > 0,
					  "Context::waitAny requires at least one handle");

		// `completed` == sizeof...(Hs) means "none observed yet". The fold over
		// || short-circuits on the first complete handle; `i` is a manual
		// counter that tracks source position since pack elements evaluate
		// left-to-right.
		std::size_t completed = sizeof...(Hs);
		waitUntil([&]() -> bool
				  {
			std::size_t i = 0;
			return ((hs.complete()
			             ? (completed = i, true)
			             : (++i, false)) || ...); });
		return completed;
	}

	template <class... Ts>
	std::size_t Context::waitAny(const std::tuple<Handle<Ts>...>& tup)
	{
		return std::apply([this](const auto&... hs)
						  { return this->waitAny(hs...); }, tup);
	}

	template <class Pred>
	void Context::waitUntil(Pred&& pred)
	{
		// Drain pending pool work while pred() reports "not yet"; matches
		// runQueueJob's wait loop, so a worker sitting here is indistinguishable
		// from one processing its own deque.
		while (!pred())
		{
			if (!tryRunSteal())
				std::this_thread::yield();
		}
	}

	// Templated on the concrete JobT so job.run(i) is a direct call inside the
	// wrapper Task lambda — no virtual dispatch.
	template <class JobT>
	void Context::runQueueJob(JobT& job)
	{
		const std::size_t count = job.taskCount();
		if (count == 0)
			return;

		// Single task: run inline on the caller, no submit/wait. Also the
		// chunked-job serial fallback (count normalised to 1).
		if (count == 1)
		{
			job.run(0);
			job.rethrowIfFailed();
			return;
		}

		// Two-task fast path (every parallel(a, b)): submit task 0 to a worker,
		// run task 1 inline, then drain the single outstanding task. Saves a
		// push + fencedNotify + steal-loop iteration vs the batch path.
		if (count == 2)
		{
			m_workerPool.submit(details::Task([&job]()
											  { job.run(0); }));
			job.run(1);
			while (job.remaining() > 0)
			{
				if (!tryRunSteal())
					std::this_thread::yield();
			}
			job.rethrowIfFailed();
			return;
		}

		// Generator-based submitBatch builds each wrapper Task at push time, no
		// intermediate vector. The caller then steals while waiting; the release
		// in runOne() pairs with the acquire in remaining(), so every task's
		// stores (incl. m_firstException) are visible once the loop exits.
		m_workerPool.submitBatch(count, [&job](std::size_t i)
								 { return details::Task([&job, i]()
														{ job.run(i); }); });

		while (job.remaining() > 0)
		{
			if (!tryRunSteal())
				std::this_thread::yield();
		}

		job.rethrowIfFailed();
	}
} // namespace multi
