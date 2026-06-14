/*
 *  Created by LuckyNeko on 07/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "multi/details/platform.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace multi::details
{
	/*
	 * MpmcQueue
	 * Bounded MPMC ring buffer (Vyukov). Each cell carries a sequence number
	 * gating push/pop, so producers and consumers never block beyond a single
	 * CAS. Push at seq == enqueuePos, pop at seq == dequeuePos + 1; a successful
	 * push sets seq = pos + 1, a successful pop sets seq = pos + CAPACITY. The
	 * cell sequence's acquire-load / release-store carries the data hand-off, so
	 * the position CAS only arbitrates ownership (relaxed).
	 */
	template <typename T, std::size_t CAPACITY>
	class MpmcQueue
	{
		static_assert((CAPACITY & (CAPACITY - 1)) == 0, "CAPACITY must be a power of two");
		static_assert(CAPACITY >= 2, "CAPACITY must be >= 2");
		static_assert(std::is_nothrow_move_assignable_v<T>, "T must be nothrow move-assignable");
		static_assert(std::is_nothrow_move_constructible_v<T>, "T must be nothrow move-constructible");
		static_assert(std::is_default_constructible_v<T>, "T must be default-constructible");

	public:
		MpmcQueue()
			: m_enqueuePos(0)
			, m_dequeuePos(0)
		{
			for (std::size_t i = 0; i < CAPACITY; ++i)
				m_cells[i].sequence.store(i, std::memory_order_relaxed);
		}

		MpmcQueue(const MpmcQueue&) = delete;
		MpmcQueue& operator=(const MpmcQueue&) = delete;

		// No explicit destructor: storage is a value member, so ~array destroys
		// every T (occupied, moved-from, or default) exactly once. Items left in
		// the ring at teardown are destroyed but not run — draining is the
		// caller's job (see WorkerPool::stop).

		// Convenience overload for rvalue callers; routes through the lvalue one.
		bool tryPush(T&& v) { return tryPush(v); }

		// By lvalue reference: the move happens only on success, so callers can
		// cascade with std::move(task) after a previous attempt returned false
		// (see WorkStealDeque::tryPushLocal).
		bool tryPush(T& v)
		{
			Cell* cell;
			std::size_t pos = m_enqueuePos.load(std::memory_order_relaxed);
			for (;;)
			{
				cell = &m_cells[pos & MASK];
				const std::size_t seq = cell->sequence.load(std::memory_order_acquire);
				const intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
				if (diff == 0)
				{
					if (m_enqueuePos.compare_exchange_weak(pos, pos + 1,
														   std::memory_order_relaxed))
						break;
				}
				else if (diff < 0)
				{
					return false; // full
				}
				else
				{
					pos = m_enqueuePos.load(std::memory_order_relaxed);
				}
			}
			cell->storage = std::move(v);
			cell->sequence.store(pos + 1, std::memory_order_release);
			return true;
		}

		bool tryPop(T* out)
		{
			Cell* cell;
			std::size_t pos = m_dequeuePos.load(std::memory_order_relaxed);
			for (;;)
			{
				cell = &m_cells[pos & MASK];
				const std::size_t seq = cell->sequence.load(std::memory_order_acquire);
				const intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);
				if (diff == 0)
				{
					if (m_dequeuePos.compare_exchange_weak(pos, pos + 1,
														   std::memory_order_relaxed))
						break;
				}
				else if (diff < 0)
				{
					return false; // empty
				}
				else
				{
					pos = m_dequeuePos.load(std::memory_order_relaxed);
				}
			}
			*out = std::move(cell->storage);
			cell->sequence.store(pos + CAPACITY, std::memory_order_release);
			return true;
		}

		// Racy.
		std::size_t sizeHint() const
		{
			const std::size_t e = m_enqueuePos.load(std::memory_order_relaxed);
			const std::size_t d = m_dequeuePos.load(std::memory_order_relaxed);
			return e > d ? e - d : 0;
		}

		static constexpr std::size_t capacity() { return CAPACITY; }

	private:
		// Cell is intentionally not cache-line padded: for a spillover ring the
		// memory cost outweighs the false-sharing win (an accepted Vyukov trade-off).
		struct Cell
		{
			std::atomic<std::size_t> sequence;
			T storage;
		};

		static constexpr std::size_t MASK = CAPACITY - 1;

		alignas(CACHE_LINE_SIZE) std::atomic<std::size_t> m_enqueuePos;
		alignas(CACHE_LINE_SIZE) std::atomic<std::size_t> m_dequeuePos;
		alignas(CACHE_LINE_SIZE) std::array<Cell, CAPACITY> m_cells;
	};
} // namespace multi::details
