/*
 *  Created by LuckyNeko on 09/07/2026.
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "multi/recipe.h"

#include <algorithm>
#include <utility>

namespace multi
{
	// Threshold to trigger using Sets
	constexpr std::size_t SuccessorSetThreshold = 128;

	Step::Step(std::uint32_t index) noexcept
		: m_index(index)
	{
	}

	bool Step::valid() const noexcept
	{
		return m_index != InvalidIndex;
	}

	StepLink operator>>(Step before, Step after) noexcept
	{
		return StepLink{before, after};
	}

	RecipeResult Recipe::order(StepLink link) noexcept
	{
		return order(link.before, link.after);
	}

	Recipe::Recipe(std::size_t reservedStepCount)
	{
		m_steps.reserve(reservedStepCount);
		m_reachStack.reserve(reservedStepCount);
		m_reachSeen.reserve(reservedStepCount);
	}

	std::size_t Recipe::stepCount() const noexcept
	{
		return m_steps.size();
	}

	bool Recipe::validStep(const Step& step) const noexcept
	{
		return step.valid() &&
			   static_cast<std::size_t>(step.m_index) < m_steps.size();
	}

	RecipeResult Recipe::order(Step before, Step after) noexcept
	{
		if (!validStep(before) || !validStep(after))
			return RecipeResult::InvalidStep;
		if (before.m_index == after.m_index)
			return RecipeResult::WouldCycle;

		auto& successors = m_steps[before.m_index].successors;
		std::unordered_set<std::size_t>* successorSet = nullptr;
		if (successors.size() >= SuccessorSetThreshold)
		{
			auto setIt = m_successorSets.find(before.m_index);
			if (setIt != m_successorSets.end())
			{
				successorSet = &setIt->second;
				if (successorSet->find(after.m_index) != successorSet->end())
					return RecipeResult::DuplicateEdge;
			}
			else if (std::find(successors.begin(), successors.end(), after.m_index) != successors.end())
			{
				return RecipeResult::DuplicateEdge;
			}
		}
		else if (std::find(successors.begin(), successors.end(), after.m_index) != successors.end())
		{
			return RecipeResult::DuplicateEdge;
		}

		const bool stepOrderProvesAcyclic = m_stepsAreTopologicallyOrdered && before.m_index < after.m_index;
		if (!stepOrderProvesAcyclic && reaches(after.m_index, before.m_index))
			return RecipeResult::WouldCycle;

		successors.push_back(after.m_index);
		if (successorSet)
		{
			successorSet->insert(after.m_index);
		}
		else if (successors.size() == SuccessorSetThreshold)
		{
			auto [setIt, inserted] = m_successorSets.emplace(before.m_index, std::unordered_set<std::size_t>{});
			(void)inserted;
			setIt->second.reserve(successors.size());
			setIt->second.insert(successors.begin(), successors.end());
		}
		++m_steps[after.m_index].predecessors;
		if (before.m_index > after.m_index)
			m_stepsAreTopologicallyOrdered = false;
		return RecipeResult::Ok;
	}

	bool Recipe::reaches(std::size_t start, std::size_t target) const
	{
		m_reachStack.clear();
		if (m_reachSeen.size() < m_steps.size())
			m_reachSeen.resize(m_steps.size());
		++m_reachGeneration;
		if (m_reachGeneration == 0)
		{
			std::fill(m_reachSeen.begin(), m_reachSeen.end(), 0);
			m_reachGeneration = 1;
		}
		m_reachStack.push_back(start);

		while (!m_reachStack.empty())
		{
			const std::size_t index = m_reachStack.back();
			m_reachStack.pop_back();
			if (index == target)
				return true;
			if (m_reachSeen[index] == m_reachGeneration)
				continue;
			m_reachSeen[index] = m_reachGeneration;

			for (const std::size_t successor : m_steps[index].successors)
			{
				if (m_reachSeen[successor] != m_reachGeneration)
					m_reachStack.push_back(successor);
			}
		}
		return false;
	}

	details::RecipeGraph Recipe::bake() && noexcept
	{
		return details::RecipeGraph(std::move(m_steps));
	}
} // namespace multi
