/*
 *  Created by LuckyNeko on 07/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "multi/details/constants.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <type_traits>
#include <utility>

namespace multi::details
{
	/*
	 * ChaseLevDeque
	 * Fixed-capacity SPMC deque (Chase-Lev), lock-free in the common case. The
	 * owner pushes/pops at the bottom; any thread may steal from the top.
	 * Bottom ops (tryPushBottom/tryPopBottom) are single-owner only — calling
	 * them concurrently is undefined.
	 *
	 * For move-only T we claim a slot by CAS before moving out (the canonical
	 * read-before-CAS algorithm needs a copy). Per-slot sequence atomics make
	 * that safe under wraparound: slot i is writable when seq == i, readable
	 * when seq == i + 1; a reader advances seq to i + CAPACITY to release the
	 * slot for the next round. The owner's push spins on seq == b before writing.
	 */
	template <typename T, std::size_t CAPACITY>
	class ChaseLevDeque
	{
		static_assert((CAPACITY & (CAPACITY - 1)) == 0, "CAPACITY must be a power of two");
		static_assert(CAPACITY >= 2, "CAPACITY must be >= 2");
		static_assert(std::is_nothrow_move_assignable_v<T>, "T must be nothrow move-assignable");
		static_assert(std::is_nothrow_move_constructible_v<T>, "T must be nothrow move-constructible");
		static_assert(std::is_default_constructible_v<T>, "T must be default-constructible");

	public:
		ChaseLevDeque()
			: m_top(0)
			, m_bottom(0)
		{
			for (std::size_t i = 0; i < CAPACITY; ++i)
				m_buffer[i].sequence.store(static_cast<int64_t>(i), std::memory_order_relaxed);
		}

		ChaseLevDeque(const ChaseLevDeque&) = delete;
		ChaseLevDeque& operator=(const ChaseLevDeque&) = delete;

		// Convenience overload for rvalue callers; routes through the lvalue
		// overload, so move-on-success semantics are identical.
		bool tryPushBottom(T&& v) { return tryPushBottom(v); }

		// By lvalue reference (not value/rvalue-ref) so the caller's storage is
		// intact on the full path — the move happens only on success. Lets
		// WorkStealDeque::tryPushLocal cascade to overflow with std::move(task)
		// after this returns false.
		bool tryPushBottom(T& v)
		{
			const int64_t b = m_bottom.load(std::memory_order_relaxed);
			const int64_t t = m_top.load(std::memory_order_acquire);
			if (b - t >= static_cast<int64_t>(CAPACITY))
				return false;

			Slot& slot = m_buffer[static_cast<std::size_t>(b) & MASK];
			// Wait for a previous stealer at (b - CAPACITY) to release this slot.
			// Fullness was ruled out above, so this only waits on a slow
			// stealer's release-store, not unboundedly.
			while (slot.sequence.load(std::memory_order_acquire) != b)
				std::this_thread::yield();

			slot.storage = std::move(v);
			slot.sequence.store(b + 1, std::memory_order_release);
			m_bottom.store(b + 1, std::memory_order_release);
			return true;
		}

		bool tryPopBottom(T* out)
		{
			const int64_t b = m_bottom.load(std::memory_order_relaxed) - 1;
			m_bottom.store(b, std::memory_order_relaxed);
			std::atomic_thread_fence(std::memory_order_seq_cst);
			int64_t t = m_top.load(std::memory_order_relaxed);

			if (t > b)
			{
				m_bottom.store(b + 1, std::memory_order_relaxed);
				return false;
			}

			if (t < b)
			{
				Slot& slot = m_buffer[static_cast<std::size_t>(b) & MASK];
				*out = std::move(slot.storage);
				// Pop reuses position b on the next push (not b + CAPACITY).
				slot.sequence.store(b, std::memory_order_release);
				return true;
			}

			// t == b: exactly one element, race against stealers for it.
			const bool won = m_top.compare_exchange_strong(t, t + 1,
														   std::memory_order_seq_cst,
														   std::memory_order_relaxed);
			m_bottom.store(b + 1, std::memory_order_relaxed);
			if (!won)
				return false;

			Slot& slot = m_buffer[static_cast<std::size_t>(b) & MASK];
			*out = std::move(slot.storage);
			// Won the CAS — logically a steal at b, so release to b + CAPACITY.
			slot.sequence.store(b + static_cast<int64_t>(CAPACITY), std::memory_order_release);
			return true;
		}

		bool tryStealTop(T* out)
		{
			int64_t t = m_top.load(std::memory_order_acquire);
			std::atomic_thread_fence(std::memory_order_seq_cst);
			const int64_t b = m_bottom.load(std::memory_order_acquire);
			if (t >= b)
				return false;

			if (!m_top.compare_exchange_strong(t, t + 1,
											   std::memory_order_seq_cst,
											   std::memory_order_relaxed))
				return false;

			// CAS won — slot t is ours. The owner's next push there is gated on
			// seq == t + CAPACITY, which we set only after moving out, so the
			// slot can't be overwritten even if we're preempted before the move.
			Slot& slot = m_buffer[static_cast<std::size_t>(t) & MASK];
			*out = std::move(slot.storage);
			slot.sequence.store(t + static_cast<int64_t>(CAPACITY), std::memory_order_release);
			return true;
		}

		std::size_t sizeHint() const
		{
			const int64_t b = m_bottom.load(std::memory_order_relaxed);
			const int64_t t = m_top.load(std::memory_order_relaxed);
			const int64_t diff = b - t;
			return diff > 0 ? static_cast<std::size_t>(diff) : 0;
		}

		static constexpr std::size_t capacity() { return CAPACITY; }

	private:
		struct Slot
		{
			std::atomic<int64_t> sequence;
			T storage;
		};

		static constexpr std::size_t MASK = CAPACITY - 1;

		alignas(CACHE_LINE_SIZE) std::atomic<int64_t> m_top;
		alignas(CACHE_LINE_SIZE) std::atomic<int64_t> m_bottom;
		alignas(CACHE_LINE_SIZE) std::array<Slot, CAPACITY> m_buffer;
	};
} // namespace multi::details
