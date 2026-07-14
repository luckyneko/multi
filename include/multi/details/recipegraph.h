/*
 *  Created by LuckyNeko on 13/07/2026.
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "multi/details/task.h"

#include <cstddef>
#include <utility>
#include <vector>

namespace multi::details
{
	struct RecipeStep
	{
		Task task;
		std::vector<std::size_t> successors;
		std::size_t predecessors = 0;
	};

	class RecipeGraph
	{
	public:
		RecipeGraph() = default;
		explicit RecipeGraph(std::vector<RecipeStep>&& steps) noexcept
			: m_steps(std::move(steps))
		{
		}

		RecipeGraph(const RecipeGraph&) = delete;
		RecipeGraph& operator=(const RecipeGraph&) = delete;
		RecipeGraph(RecipeGraph&&) noexcept = default;
		RecipeGraph& operator=(RecipeGraph&&) noexcept = default;

		std::size_t stepCount() const noexcept { return m_steps.size(); }
		RecipeStep& step(std::size_t index) noexcept { return m_steps[index]; }
		const RecipeStep& step(std::size_t index) const noexcept { return m_steps[index]; }

	private:
		std::vector<RecipeStep> m_steps;
	};
} // namespace multi::details
