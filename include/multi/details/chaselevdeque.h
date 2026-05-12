/*
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

namespace multi
{
	/*
	 * ChaseLevDeque
	 * Fixed-capacity SPMC deque (Chase-Lev). Owner pushes/pops at the bottom;
	 * any thread may steal from the top. Lock-free in the common case.
	 *
	 * Bottom-end ops (tryPushBottom/tryPopBottom) MUST be called by a single
	 * owner thread. Calling them concurrently from multiple threads is undefined.
	 *
	 * Move-only T support: the canonical Chase-Lev reads the slot before CAS and
	 * discards on failure — that requires a copy. For move-only T we instead
	 * claim the slot via CAS first and *then* move out. That's only safe if the
	 * owner cannot overwrite the slot between a stealer's CAS and read, which can
	 * happen under wraparound (Capacity push/pops can reuse the same slot).
	 *
	 * Per-slot sequence atomics close that window: a slot at index i is writable
	 * when seq == i and readable when seq == i + 1. After a stealer reads, it
	 * advances seq to i + Capacity, releasing the slot for the *next* round of
	 * writes. The owner's push spins on seq == b before writing — bounded by how
	 * long the slowest stealer takes to release its claim (effectively a few
	 * memory ops in practice).
	 */
	template <typename T, std::size_t Capacity>
	class ChaseLevDeque
	{
		static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
		static_assert(Capacity >= 2, "Capacity must be >= 2");
		static_assert(std::is_nothrow_move_assignable_v<T>, "T must be nothrow move-assignable");
		static_assert(std::is_nothrow_move_constructible_v<T>, "T must be nothrow move-constructible");
		static_assert(std::is_default_constructible_v<T>, "T must be default-constructible");

	public:
		ChaseLevDeque()
			: m_top(0)
			, m_bottom(0)
		{
			for (std::size_t i = 0; i < Capacity; ++i)
				m_buffer[i].sequence.store(static_cast<int64_t>(i), std::memory_order_relaxed);
		}

		ChaseLevDeque(const ChaseLevDeque&) = delete;
		ChaseLevDeque& operator=(const ChaseLevDeque&) = delete;

		bool tryPushBottom(T&& v)
		{
			const int64_t b = m_bottom.load(std::memory_order_relaxed);
			const int64_t t = m_top.load(std::memory_order_acquire);
			if (b - t >= static_cast<int64_t>(Capacity))
				return false;

			Slot& slot = m_buffer[static_cast<std::size_t>(b) & MASK];
			// Wait for a previous stealer/popper at position (b - Capacity) to
			// finish releasing this slot. Without the test on (b - t < Capacity)
			// this could be unbounded, but fullness has been ruled out above so
			// the only thing we wait on is a slow stealer's release-store.
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
				// After pop, bottom is decremented — the next push at this
				// slot will reuse position b, not b + Capacity. Release to b.
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
			// We won the CAS, so logically a steal at position b happened.
			// Release to b + Capacity — same as a stealer would.
			slot.sequence.store(b + static_cast<int64_t>(Capacity), std::memory_order_release);
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

			// CAS won — slot t is logically ours. The owner's push at position
			// t + Capacity is gated by sequence == t + Capacity, which we set
			// after moving out below, so the slot can't be overwritten beneath
			// us even if we get preempted between CAS and the move.
			Slot& slot = m_buffer[static_cast<std::size_t>(t) & MASK];
			*out = std::move(slot.storage);
			slot.sequence.store(t + static_cast<int64_t>(Capacity), std::memory_order_release);
			return true;
		}

		std::size_t sizeHint() const
		{
			const int64_t b = m_bottom.load(std::memory_order_relaxed);
			const int64_t t = m_top.load(std::memory_order_relaxed);
			const int64_t diff = b - t;
			return diff > 0 ? static_cast<std::size_t>(diff) : 0;
		}

		static constexpr std::size_t capacity() { return Capacity; }

	private:
		struct Slot
		{
			std::atomic<int64_t> sequence;
			T storage;
		};

		static constexpr std::size_t MASK = Capacity - 1;

		alignas(CACHE_LINE_SIZE) std::atomic<int64_t> m_top;
		alignas(CACHE_LINE_SIZE) std::atomic<int64_t> m_bottom;
		alignas(CACHE_LINE_SIZE) std::array<Slot, Capacity> m_buffer;
	};
} // namespace multi
