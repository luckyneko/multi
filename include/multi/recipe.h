/*
 *  Created by LuckyNeko on 09/07/2026.
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "multi/details/task.h"
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace multi
{
	namespace details
	{
		class RecipeJob;
	}

	class Recipe;

	enum class RecipeResult
	{
		Ok,
		InvalidStep,
		WouldCycle,
		DuplicateEdge,
	};

	/// Light token for one callable inside a Recipe.
	class Step
	{
	public:
		Step() = default;

		bool valid() const noexcept;
		explicit operator bool() const noexcept { return valid(); }

	private:
		friend class Recipe;
		explicit Step(std::uint32_t index) noexcept;

		static constexpr std::uint32_t InvalidIndex = ~std::uint32_t{0};
		std::uint32_t m_index = InvalidIndex;
	};

	struct StepLink
	{
		Step before;
		Step after;
	};

	StepLink operator>>(Step before, Step after) noexcept;

	/// Single-use DAG of void callables connected by ordered StepLinks.
	class Recipe
	{
	public:
		Recipe() = default;
		Recipe(const Recipe&) = delete;
		Recipe& operator=(const Recipe&) = delete;
		Recipe(Recipe&&) noexcept = default;
		Recipe& operator=(Recipe&&) noexcept = default;

		/// Add a void() callable.
		template <class F>
		Step step(F&& f)
		{
			if (m_steps.size() >= Step::InvalidIndex)
				return Step();
			m_steps.push_back(Entry{details::Task(std::forward<F>(f)), {}, 0});
			return Step(static_cast<std::uint32_t>(m_steps.size() - 1));
		}

		RecipeResult order(StepLink link) noexcept;
		std::size_t stepCount() const noexcept;

	private:
		friend class details::RecipeJob;
		struct Entry
		{
			details::Task task;
			std::vector<std::size_t> successors;
			std::size_t predecessors = 0;
		};

		bool validStep(const Step& step) const noexcept;
		RecipeResult order(Step before, Step after) noexcept;
		bool reaches(std::size_t start, std::size_t target) const;
		std::size_t recipeStepCount() const noexcept;
		Entry& recipeStep(std::size_t index) noexcept;
		const Entry& recipeStep(std::size_t index) const noexcept;

		std::vector<Entry> m_steps;
		mutable std::vector<std::size_t> m_reachStack;
		mutable std::vector<std::uint32_t> m_reachSeen;
		mutable std::uint32_t m_reachGeneration = 0;
	};

} // namespace multi
