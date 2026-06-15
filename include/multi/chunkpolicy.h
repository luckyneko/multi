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
	/// Oversubscription factor for multi::Auto: tasks = (workers + caller) * CHUNK_FACTOR.
	inline constexpr std::size_t CHUNK_FACTOR = 4;

	/**
	 * @brief How many tasks a chunked each()/range() dispatch splits work into.
	 *
	 * Carries one of three intents, resolved to a concrete count once the work
	 * size and pool size are known:
	 *   - a bare integer (implicit conversion) — exactly that many tasks, so
	 *     `each(64, ...)` compiles;
	 *   - multi::Auto — `(workers + 1) * CHUNK_FACTOR` oversubscription, clamped;
	 *   - multi::PerItem — one task per item.
	 */
	class ChunkPolicy
	{
	public:
		/// Resolution strategy carried by a ChunkPolicy.
		enum class Policy
		{
			Auto,	 ///< Oversubscribe by CHUNK_FACTOR per worker, clamped to >= 2.
			Manual,	 ///< Exact task count (from the implicit size_t constructor).
			PerItem, ///< One task per item.
		};

		/// Implicit by design: a bare integer is an exact task count.
		/// @param taskCount Tasks to split into (resolves clamped to [1, total]).
		constexpr ChunkPolicy(std::size_t taskCount) noexcept
			: m_policy(Policy::Manual)
			, m_value(taskCount)
		{
		}

		/// @param p     Resolution strategy.
		/// @param value Strategy parameter; 0 selects the per-policy default
		///              (CHUNK_FACTOR for Auto, unused otherwise).
		explicit constexpr ChunkPolicy(Policy p, std::size_t value = 0) noexcept
			: m_policy(p)
			, m_value(value != 0 ? value : (p == Policy::Auto ? CHUNK_FACTOR : 0))
		{
		}

		/// Resolve to a concrete task count for a workload and pool size.
		/// @param total   Items to dispatch.
		/// @param workers Worker-thread count (from Context::threadCount()).
		/// @return Task count: Manual >= 1, Auto >= 2, PerItem == total, each
		///         clamped to <= total; 0 when @p total is 0 (empty dispatch).
		constexpr std::size_t resolve(std::size_t total, std::size_t workers) const noexcept
		{
			if (total == 0)
				return 0;
			switch (m_policy)
			{
				case Policy::Auto:
					// "At least 2, at most total" via min/max — not std::clamp,
					// whose lo <= hi precondition breaks when total == 1.
					return std::min(std::max((workers + 1) * m_value, std::size_t(2)), total);
				case Policy::Manual:
					return std::clamp(m_value, std::size_t(1), total);
				case Policy::PerItem:
					return total;
			}
			return total; // unreachable; satisfies -Wreturn-type for an out-of-range enum
		}

	private:
		Policy m_policy;
		std::size_t m_value;
	};

	/// Oversubscribe: roughly `(workers + 1) * CHUNK_FACTOR` tasks (clamped to >= 2).
	inline constexpr ChunkPolicy Auto{ChunkPolicy::Policy::Auto};
	/// One task per item.
	inline constexpr ChunkPolicy PerItem{ChunkPolicy::Policy::PerItem};

} // namespace multi
