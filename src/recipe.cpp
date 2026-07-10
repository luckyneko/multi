/*
 *  Created by LuckyNeko on 09/07/2026.
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "multi/recipe.h"

#include <algorithm>

namespace multi
{
	Step::Step(Recipe* recipe, std::size_t index) noexcept
		: m_recipe(recipe)
		, m_index(index)
	{
	}

	bool Step::valid() const noexcept
	{
		return m_recipe != nullptr && m_recipe->validStep(*this);
	}

	RecipeResult Step::before(Step successor) const noexcept
	{
		if (!m_recipe)
			return RecipeResult::InvalidStep;
		return m_recipe->before(*this, successor);
	}

	std::size_t Recipe::stepCount() const noexcept
	{
		return m_steps.size();
	}

	bool Recipe::validStep(const Step& step) const noexcept
	{
		return step.m_recipe == this &&
			   step.m_index < m_steps.size();
	}

	RecipeResult Recipe::before(Step predecessor, Step successor) noexcept
	{
		if (!validStep(predecessor) || !successor.valid())
			return RecipeResult::InvalidStep;
		if (successor.m_recipe != this)
			return RecipeResult::DifferentRecipe;
		if (predecessor.m_index == successor.m_index || reaches(successor.m_index, predecessor.m_index))
			return RecipeResult::WouldCycle;

		auto& successors = m_steps[predecessor.m_index].successors;
		if (std::find(successors.begin(), successors.end(), successor.m_index) != successors.end())
			return RecipeResult::DuplicateEdge;

		successors.push_back(successor.m_index);
		++m_steps[successor.m_index].predecessors;
		return RecipeResult::Ok;
	}

	bool Recipe::reaches(std::size_t start, std::size_t target) const
	{
		std::vector<std::size_t> stack;
		std::vector<unsigned char> seen(m_steps.size(), 0);
		stack.push_back(start);

		while (!stack.empty())
		{
			const std::size_t index = stack.back();
			stack.pop_back();
			if (index == target)
				return true;
			if (seen[index])
				continue;
			seen[index] = 1;

			for (const std::size_t successor : m_steps[index].successors)
			{
				if (!seen[successor])
					stack.push_back(successor);
			}
		}
		return false;
	}

	std::size_t Recipe::recipeStepCount() const noexcept
	{
		return m_steps.size();
	}

	Recipe::Entry& Recipe::recipeStep(std::size_t index) noexcept
	{
		return m_steps[index];
	}

	const Recipe::Entry& Recipe::recipeStep(std::size_t index) const noexcept
	{
		return m_steps[index];
	}
} // namespace multi
