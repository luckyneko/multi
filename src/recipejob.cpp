/*
 *  Created by LuckyNeko on 09/07/2026.
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "multi/details/recipejob.h"
#include "multi/recipe.h"

#include <utility>

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

	namespace
	{
		// Set while RecipeJob::schedule is inside m_submit. The submit runs the
		// step's task inline on this thread when the pool is not running, is
		// stopping or the target deque is full; runStep then finds its own job
		// here and hands the step back to the frame that called schedule,
		// instead of running it one level deeper on the stack. Only that task
		// can run inside m_submit, so no step's callable ever sees this set,
		// and the job check keeps a step of any other RecipeJob off the list.
		struct InlineSubmit
		{
			const RecipeJob* job;
			RecipeDeferredSteps* deferred;
		};

		thread_local const InlineSubmit* t_inlineSubmit = nullptr;
	} // namespace

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
		const InlineSubmit submitting{this, &deferred};
		struct Restore
		{
			const InlineSubmit* previous;
			~Restore() { t_inlineSubmit = previous; }
		} restore{t_inlineSubmit};
		t_inlineSubmit = &submitting;

		m_submit(Task([self = shared_from_this(), index]()
					  { self->runStep(index); }));
	}

	void RecipeJob::runStep(std::size_t index) noexcept
	{
		const InlineSubmit* const submitting = t_inlineSubmit;
		if (submitting != nullptr && submitting->job == this)
		{
			// Run inline by our own schedule(): the frame that called it runs
			// the step once its current chain ends.
			defer(index, *submitting->deferred);
			return;
		}

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
		try
		{
			m_graph.step(index).task();
			return true;
		}
		catch (...)
		{
			std::call_once(m_exceptionOnce, [this]()
						   { m_firstException = std::current_exception(); });
			return false;
		}
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
		for (const std::size_t successor : m_graph.step(index).successors)
			skip(successor);
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

	void RecipeJob::skip(std::size_t index) noexcept
	{
		int expected = Pending;
		if (!m_state[index].compare_exchange_strong(expected, Finished, std::memory_order_acq_rel))
			return;

		finishOne();

		for (const std::size_t successor : m_graph.step(index).successors)
			skip(successor);
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
