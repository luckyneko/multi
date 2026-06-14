/*
 *  Created by LuckyNeko on 13/06/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include <algorithm>
#include <cstddef>

namespace multi
{
	/*
	 * ChunkPolicy — how many tasks a chunked each()/range() dispatch should split
	 * its work into. One of three intents, resolved to a concrete count once the
	 * work size and pool size are known:
	 *   - a bare integer  → exactly that many tasks (implicit, so each(64, …) works)
	 *   - multi::Auto      → (workers+1)*value oversubscription, clamped
	 *   - multi::PerItem   → one task per item
	 */

	// K — chunk oversubscription factor for Auto: tasks = (workers + caller) * K.
	inline constexpr std::size_t CHUNK_FACTOR = 4;

	class ChunkPolicy
	{
	public:
		enum class Policy
		{
			Auto,
			Manual,
			PerItem,
		};

		// Implicit on purpose: a bare integer is an exact task count.
		constexpr ChunkPolicy(std::size_t taskCount) noexcept
			: m_policy(Policy::Manual)
			, m_value(taskCount)
		{
		}

		explicit constexpr ChunkPolicy(Policy p, std::size_t value = 0) noexcept
			: m_policy(p)
			, m_value(value == 0 ? defaultValue(p) : value)
		{
		}

		constexpr bool isPerItem() const noexcept
		{
			return m_policy == Policy::PerItem;
		}

		// Concrete task count for `total` items across `workers` worker threads.
		// Clamps mirror the historical makeSetup: Exact >= 1, Auto >= 2, all <= total;
		// total == 0 yields 0 (empty dispatch).
		constexpr std::size_t resolve(std::size_t total, std::size_t workers) const noexcept
		{
			if (total == 0)
				return 0;
			switch (m_policy)
			{
				case Policy::Auto:
					return std::clamp((workers + 1) * m_value, std::size_t(2), total);
				case Policy::Manual:
					return std::clamp(m_value, std::size_t(1), total);
				case Policy::PerItem:
					return total;
			}
			return m_value;
		}

	private:
		constexpr static std::size_t defaultValue(Policy p)
		{
			switch (p)
			{
				case Policy::Auto:
					return CHUNK_FACTOR;
				case Policy::Manual:
					return 0;
				case Policy::PerItem:
					return 0;
			}
			return 0;
		}

	private:
		Policy m_policy;
		std::size_t m_value;
	};

	inline constexpr ChunkPolicy Auto{ChunkPolicy::Policy::Auto};
	inline constexpr ChunkPolicy PerItem{ChunkPolicy::Policy::PerItem};

} // namespace multi
