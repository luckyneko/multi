/*
 *  Created by LuckyNeko on 09/07/2026.
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "multi/details/recipejob.h"
#include "multi/recipe.h"

#include <utility>
#include <vector>

namespace multi::details
{
	RecipeJob::RecipeJob(Recipe&& recipe, Submit submit)
		: m_submit(std::move(submit))
		, m_graph(std::move(recipe).bake())
		, m_pending(m_graph.stepCount())
		, m_state(m_graph.stepCount())
		, m_remaining(m_graph.stepCount())
	{
		for (std::size_t i = 0; i < m_graph.stepCount(); ++i)
		{
			m_pending[i].store(m_graph.step(i).predecessors, std::memory_order_relaxed);
			m_state[i].store(Pending, std::memory_order_relaxed);
		}
	}

	std::future<void> RecipeJob::getFuture()
	{
		return m_promise.get_future();
	}

	void RecipeJob::start()
	{
		Deferred deferred;
		for (std::size_t i = 0; i < m_pending.size(); ++i)
		{
			if (m_pending[i].load(std::memory_order_relaxed) == 0)
			{
				if (tryMarkScheduled(i))
					schedule(i, deferred);
			}
		}
		runDeferred(deferred);

		if (m_remaining.load(std::memory_order_acquire) == 0)
			complete();
	}

	void RecipeJob::schedule(std::size_t index, Deferred& deferred)
	{
		// The submit either queues the task or says that it did not: the pool
		// is not running, is stopping, or the target deque is full. It never
		// runs the task, so nothing nests here. A step that was not queued
		// stays with this frame's runner, which runs it once its current chain
		// ends, however many steps that takes.
		Task task([self = shared_from_this(), index]()
				  { self->runStep(index); });
		if (!m_submit(task))
			defer(index, deferred);
	}

	void RecipeJob::runStep(std::size_t index) noexcept
	{
		Deferred deferred;
		runChain(index, deferred);
		runDeferred(deferred);
	}

	void RecipeJob::defer(std::size_t index, Deferred& deferred) noexcept
	{
		// index is Scheduled and not yet run, as is deferred.tail: both slots
		// are free to hold links.
		m_pending[index].store(Deferred::None, std::memory_order_relaxed);
		if (deferred.tail == Deferred::None)
			deferred.head = index;
		else
			m_pending[deferred.tail].store(index, std::memory_order_relaxed);
		deferred.tail = index;
	}

	void RecipeJob::runChain(std::size_t index, Deferred& deferred) noexcept
	{
		std::size_t current = index;
		std::size_t inlineSuccessor = 0;
		while (runStepOnce(current, inlineSuccessor, deferred))
			current = inlineSuccessor;
	}

	void RecipeJob::runDeferred(Deferred& deferred) noexcept
	{
		// A chain may defer more steps; this loop, not the stack, takes them.
		while (deferred.head != Deferred::None)
		{
			const std::size_t index = deferred.head;
			deferred.head = m_pending[index].load(std::memory_order_relaxed);
			if (deferred.head == Deferred::None)
				deferred.tail = Deferred::None;
			runChain(index, deferred);
		}
	}

	bool RecipeJob::runStepOnce(std::size_t index, std::size_t& inlineSuccessor, Deferred& deferred) noexcept
	{
		const bool succeeded = executeStep(index);

		m_state[index].store(Finished, std::memory_order_release);
		finishOne();

		if (!succeeded)
		{
			skipSuccessors(index);
			return false;
		}

		return scheduleReadySuccessors(index, inlineSuccessor, deferred);
	}

	bool RecipeJob::executeStep(std::size_t index) noexcept
	{
		Task& task = m_graph.step(index).task;
		bool succeeded = true;
		try
		{
			task();
		}
		catch (...)
		{
			std::call_once(m_exceptionOnce, [this]()
						   { m_firstException = std::current_exception(); });
			succeeded = false;
		}

		// The step has run, so drop its callable and whatever it captured now
		// rather than with the job, which a RecipeHandle keeps alive. This is
		// before the step is counted, so a completed recipe holds no callable.
		task = Task();
		return succeeded;
	}

	bool RecipeJob::scheduleReadySuccessors(std::size_t index, std::size_t& inlineSuccessor, Deferred& deferred)
	{
		bool hasInlineSuccessor = false;
		for (const std::size_t successor : m_graph.step(index).successors)
		{
			if (m_pending[successor].fetch_sub(1, std::memory_order_acq_rel) == 1 &&
				tryMarkScheduled(successor))
			{
				if (!hasInlineSuccessor)
				{
					inlineSuccessor = successor;
					hasInlineSuccessor = true;
				}
				else
					schedule(successor, deferred);
			}
		}
		return hasInlineSuccessor;
	}

	bool RecipeJob::tryMarkScheduled(std::size_t index) noexcept
	{
		int expected = Pending;
		return m_state[index].compare_exchange_strong(expected, Scheduled, std::memory_order_acq_rel);
	}

	void RecipeJob::skipSuccessors(std::size_t index) noexcept
	{
		// Walks everything after index with an explicit stack, not recursion,
		// so the stack stays flat however long the chain. Several threads can
		// walk at once (two failing steps); skip()'s CAS gives each step to
		// exactly one of them, and a walker only goes on from steps it won.
		// The first step claimed from each visited step is walked next without
		// touching the stack, so skipping a plain chain allocates nothing.
		//
		// The stack can fail to grow. Dropping the claimed step would leave
		// its successors Pending and the recipe could never complete, and
		// letting bad_alloc reach noexcept would terminate. Instead the step
		// that could not be pushed is walked by a nested call. Chains still
		// need no stack, so while memory stays short only branching steps
		// nest: the walk may run deeper, but every step is still skipped.
		std::vector<std::size_t> claimed;
		std::size_t current = index;
		for (;;)
		{
			bool hasNext = false;
			std::size_t next = 0;
			for (const std::size_t successor : m_graph.step(current).successors)
			{
				if (!skip(successor))
					continue;
				if (!hasNext)
				{
					next = successor;
					hasNext = true;
					continue;
				}

				bool pushed = true;
				try
				{
					claimed.push_back(successor);
				}
				catch (...)
				{
					pushed = false;
				}
				if (!pushed)
					skipSuccessors(successor);
			}

			if (!hasNext)
			{
				if (claimed.empty())
					return;
				next = claimed.back();
				claimed.pop_back();
			}
			current = next;
		}
	}

	std::size_t RecipeJob::stepCount() const noexcept
	{
		return m_state.size();
	}

	std::size_t RecipeJob::finishedCount() const noexcept
	{
		const std::size_t total = stepCount();
		const std::size_t remaining = m_remaining.load(std::memory_order_acquire);
		return remaining < total ? total - remaining : 0;
	}

	float RecipeJob::progress() const noexcept
	{
		const std::size_t total = stepCount();
		if (total == 0)
			return 1.0f;
		return static_cast<float>(finishedCount()) / static_cast<float>(total);
	}

	bool RecipeJob::skip(std::size_t index) noexcept
	{
		int expected = Pending;
		if (!m_state[index].compare_exchange_strong(expected, Finished, std::memory_order_acq_rel))
			return false;

		// Winning the CAS makes the step ours, and it will never run: drop its
		// callable before counting it, as executeStep does.
		m_graph.step(index).task = Task();
		finishOne();
		return true;
	}

	void RecipeJob::finishOne() noexcept
	{
		if (m_remaining.fetch_sub(1, std::memory_order_acq_rel) == 1)
			complete();
	}

	void RecipeJob::complete() noexcept
	{
		std::call_once(m_completeOnce, [this]()
					   {
			try
			{
				if (m_firstException)
					m_promise.set_exception(m_firstException);
				else
					m_promise.set_value();
			}
			catch (...)
			{
			} });
	}
} // namespace multi::details
