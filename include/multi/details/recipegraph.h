/*
 *  Created by LuckyNeko on 13/07/2026.
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "multi/details/task.h"

#include <array>
#include <cstddef>
#include <iterator>
#include <utility>
#include <vector>

namespace multi::details
{
	class RecipeSuccessors
	{
	public:
		class const_iterator
		{
		public:
			using difference_type = std::ptrdiff_t;
			using iterator_category = std::forward_iterator_tag;
			using value_type = std::size_t;
			using reference = std::size_t;

			const_iterator() = default;
			const_iterator(const RecipeSuccessors* owner, std::size_t index) noexcept
				: m_owner(owner)
				, m_index(index)
			{
			}

			std::size_t operator*() const noexcept { return m_owner->at(m_index); }

			const_iterator& operator++() noexcept
			{
				++m_index;
				return *this;
			}

			bool operator==(const const_iterator& other) const noexcept
			{
				return m_owner == other.m_owner && m_index == other.m_index;
			}

			bool operator!=(const const_iterator& other) const noexcept
			{
				return !(*this == other);
			}

		private:
			const RecipeSuccessors* m_owner = nullptr;
			std::size_t m_index = 0;
		};

		std::size_t size() const noexcept { return m_size; }
		void reserve(std::size_t count)
		{
			if (count > InlineCapacity)
				m_overflow.reserve(count - InlineCapacity);
		}

		void push_back(std::size_t value)
		{
			if (m_size < InlineCapacity)
				m_inline[m_size] = value;
			else
				m_overflow.push_back(value);
			++m_size;
		}

		bool contains(std::size_t value) const noexcept
		{
			for (std::size_t i = 0; i < m_size; ++i)
			{
				if (at(i) == value)
					return true;
			}
			return false;
		}

		const_iterator begin() const noexcept { return const_iterator(this, 0); }
		const_iterator end() const noexcept { return const_iterator(this, m_size); }

	private:
		static constexpr std::size_t InlineCapacity = 4;

		std::size_t at(std::size_t index) const noexcept
		{
			if (index < InlineCapacity)
				return m_inline[index];
			return m_overflow[index - InlineCapacity];
		}

		std::array<std::size_t, InlineCapacity> m_inline{};
		std::vector<std::size_t> m_overflow;
		std::size_t m_size = 0;
	};

	struct RecipeStep
	{
		Task task;
		RecipeSuccessors successors;
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
