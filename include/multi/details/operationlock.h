/*
 *  Created by LuckyNeko on 14/06/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include <atomic>
#include <climits>
#include <thread>

namespace multi::details
{
	/*
	 * OperationLock
	 * Packs an active flag (high bit) and a concurrent-operation counter (lower
	 * bits) into a single atomic word.
	 *
	 * try_lock / unlock satisfy the Lockable concept for std::unique_lock with
	 * std::try_to_lock. The seq_cst on try_lock's fetch_add and deactivate's
	 * fetch_and provides the Dekker-style guarantee: either try_lock sees the
	 * active bit was clear (and backs the count out) or deactivate sees the
	 * incremented count and spins until unlock brings it to zero.
	 */
	class OperationLock
	{
	public:
		void activate() noexcept
		{
			// Relaxed: called before threads are spawned; thread creation itself
			// provides the synchronization that makes the flag visible to workers.
			m_state.fetch_or(k_activeBit, std::memory_order_relaxed);
		}

		void deactivate() noexcept
		{
			m_state.fetch_and(~k_activeBit, std::memory_order_seq_cst);
			while (m_state.load(std::memory_order_seq_cst) != 0)
				std::this_thread::yield();
		}

		bool isActive(std::memory_order order = std::memory_order_relaxed) const noexcept
		{
			return (m_state.load(order) & k_activeBit) != 0;
		}

		bool try_lock() noexcept
		{
			const size_t prev = m_state.fetch_add(1, std::memory_order_seq_cst);
			if (prev & k_activeBit)
				return true;
			m_state.fetch_sub(1, std::memory_order_seq_cst);
			return false;
		}

		void unlock() noexcept
		{
			m_state.fetch_sub(1, std::memory_order_seq_cst);
		}

		void lock() = delete;

	private:
		static constexpr size_t k_activeBit = size_t{1} << (sizeof(size_t) * CHAR_BIT - 1);
		std::atomic<size_t> m_state{0};
	};

} // namespace multi::details
