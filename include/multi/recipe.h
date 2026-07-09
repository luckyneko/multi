/*
 *  Created by LuckyNeko on 09/07/2026.
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "multi/details/task.h"

#include <atomic>
#include <cstddef>
#include <type_traits>
#include <vector>

namespace multi
{
	class Recipe;

	enum class RecipeResult
	{
		Ok,
		InvalidStep,
		DifferentRecipe,
		WouldCycle,
		DuplicateEdge,
		Running,
	};

	enum class RecipeState
	{
		Idle,
		Running,
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
		Step(Recipe* recipe, std::size_t index, std::size_t generation) noexcept;

		Recipe* m_recipe = nullptr;
		std::size_t m_index = 0;
		std::size_t m_generation = 0;
	};

	/// Reusable DAG of void callables connected by Step::before edges.
	class Recipe
	{
	public:
		Recipe() = default;
		Recipe(const Recipe&) = delete;
		Recipe& operator=(const Recipe&) = delete;
		Recipe(Recipe&&) = delete;
		Recipe& operator=(Recipe&&) = delete;

		/// Add a void() callable. Returns an invalid Step if the recipe is running.
		template <class F,
				  class D = std::decay_t<F>,
				  std::enable_if_t<std::is_invocable_v<D> &&
									   std::is_void_v<std::invoke_result_t<D>>,
								   int> = 0>
		Step step(F&& f);

		/// Remove every step and edge. Existing Step handles become invalid.
		RecipeResult clear() noexcept;

		RecipeState state() const noexcept;
		bool running() const noexcept;

		std::size_t stepCount() const noexcept;
		std::size_t finishedCount() const noexcept;
		float progress() const noexcept;

	private:
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

		std::vector<Entry> m_steps;
		std::size_t m_generation = 1;
		std::atomic<bool> m_running{false};
		std::atomic<std::size_t> m_finished{0};
	};

} // namespace multi

#include "multi/details/recipe.inl"
