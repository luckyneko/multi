/*
 *  Created by LuckyNeko on 09/07/2026.
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "multi/details/task.h"
#include <cstddef>
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
		DifferentRecipe,
		WouldCycle,
		DuplicateEdge,
	};

	/// Light handle to one callable inside a Recipe.
	class Step
	{
	public:
		Step() = default;

		bool valid() const noexcept;
		explicit operator bool() const noexcept { return valid(); }

		/// Require this step to finish successfully before @p successor may run.
		RecipeResult before(Step successor) const noexcept;

	private:
		friend class Recipe;
		Step(Recipe* recipe, std::size_t index) noexcept;

		Recipe* m_recipe = nullptr;
		std::size_t m_index = 0;
	};

	/// Single-use DAG of void callables connected by Step::before edges.
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
			m_steps.push_back(Entry{details::Task(std::forward<F>(f)), {}, 0});
			return Step(this, m_steps.size() - 1);
		}

		std::size_t stepCount() const noexcept;

	private:
		friend class details::RecipeJob;
		friend class Step;
		struct Entry
		{
			details::Task task;
			std::vector<std::size_t> successors;
			std::size_t predecessors = 0;
		};

		bool validStep(const Step& step) const noexcept;
		RecipeResult before(Step predecessor, Step successor) noexcept;
		bool reaches(std::size_t start, std::size_t target) const;
		std::size_t recipeStepCount() const noexcept;
		Entry& recipeStep(std::size_t index) noexcept;
		const Entry& recipeStep(std::size_t index) const noexcept;

		std::vector<Entry> m_steps;
	};

} // namespace multi
