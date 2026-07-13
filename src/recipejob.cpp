/*
 *  Created by LuckyNeko on 09/07/2026.
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "multi/details/recipejob.h"

#include <utility>

namespace multi::details
{
	RecipeJob::RecipeJob(Recipe&& recipe, Submit submit)
		: m_submit(std::move(submit))
		, m_recipe(std::move(recipe))
		, m_pending(m_recipe.recipeStepCount())
		, m_state(m_recipe.recipeStepCount())
		, m_remaining(m_recipe.recipeStepCount())
	{
		for (std::size_t i = 0; i < m_recipe.recipeStepCount(); ++i)
		{
			m_pending[i].store(m_recipe.recipeStep(i).predecessors, std::memory_order_relaxed);
			m_state[i].store(Pending, std::memory_order_relaxed);
		}
	}

	std::future<void> RecipeJob::getFuture()
	{
		return m_promise.get_future();
	}

	void RecipeJob::start()
	{
		for (std::size_t i = 0; i < m_pending.size(); ++i)
		{
			if (m_pending[i].load(std::memory_order_relaxed) == 0)
			{
				if (tryMarkScheduled(i))
					schedule(i);
			}
		}

		if (m_remaining.load(std::memory_order_acquire) == 0)
			complete();
	}

	void RecipeJob::schedule(std::size_t index)
	{
		auto self = shared_from_this();
		m_submit(Task([self, index]()
					  { self->runStep(index); }));
	}

	void RecipeJob::runStep(std::size_t index) noexcept
	{
		std::size_t current = index;
		std::size_t inlineSuccessor = 0;
		while (runStepOnce(current, inlineSuccessor))
			current = inlineSuccessor;
	}

	bool RecipeJob::runStepOnce(std::size_t index, std::size_t& inlineSuccessor) noexcept
	{
		const bool succeeded = executeStep(index);

		m_state[index].store(Finished, std::memory_order_release);
		finishOne();

		if (!succeeded)
		{
			skipSuccessors(index);
			return false;
		}

		return scheduleReadySuccessors(index, inlineSuccessor);
	}

	bool RecipeJob::executeStep(std::size_t index) noexcept
	{
		try
		{
			m_recipe.recipeStep(index).task();
			return true;
		}
		catch (...)
		{
			std::call_once(m_exceptionOnce, [this]()
						   { m_firstException = std::current_exception(); });
			return false;
		}
	}

	bool RecipeJob::scheduleReadySuccessors(std::size_t index, std::size_t& inlineSuccessor)
	{
		bool hasInlineSuccessor = false;
		for (const std::size_t successor : m_recipe.recipeStep(index).successors)
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
					schedule(successor);
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
		for (const std::size_t successor : m_recipe.recipeStep(index).successors)
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

		for (const std::size_t successor : m_recipe.recipeStep(index).successors)
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
