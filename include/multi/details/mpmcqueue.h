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
#include <type_traits>
#include <utility>

namespace multi::details
{
	/*
	 * MpmcQueue
	 * Bounded multi-producer multi-consumer ring buffer (Vyukov). Each cell
	 * carries a sequence number that gates push/pop access, so producers and
	 * consumers never block each other beyond a single CAS.
	 *
	 * Push to cell at sequence == enqueuePos; pop from cell at sequence ==
	 * dequeuePos + 1. After successful push, sequence becomes pos + 1; after
	 * successful pop, sequence becomes pos + Capacity (the next round).
	 */
	template <typename T, std::size_t Capacity>
	class MpmcQueue
	{
		static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
		static_assert(Capacity >= 2, "Capacity must be >= 2");
		static_assert(std::is_nothrow_move_assignable_v<T>, "T must be nothrow move-assignable");
		static_assert(std::is_nothrow_move_constructible_v<T>, "T must be nothrow move-constructible");
		static_assert(std::is_default_constructible_v<T>, "T must be default-constructible");

	public:
		MpmcQueue()
			: m_enqueuePos(0)
			, m_dequeuePos(0)
		{
			for (std::size_t i = 0; i < Capacity; ++i)
				m_cells[i].sequence.store(i, std::memory_order_relaxed);
		}

		MpmcQueue(const MpmcQueue&) = delete;
		MpmcQueue& operator=(const MpmcQueue&) = delete;

		// Convenience overload for rvalue callers (literals, temporaries).
		// Named rvalue-ref is an lvalue inside, so it routes through the
		// primary overload below.
		bool tryPush(T&& v) { return tryPush(v); }

		// Pass by lvalue reference: the move into the cell only happens on
		// the success path, so callers that cascade from a sibling deque
		// (see WorkStealDeque::tryPushLocal) can safely fall through with
		// `std::move(task)` after a previous attempt returned false.
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
			cell->sequence.store(pos + Capacity, std::memory_order_release);
			return true;
		}

		// Racy.
		std::size_t sizeHint() const
		{
			const std::size_t e = m_enqueuePos.load(std::memory_order_relaxed);
			const std::size_t d = m_dequeuePos.load(std::memory_order_relaxed);
			return e > d ? e - d : 0;
		}

		static constexpr std::size_t capacity() { return Capacity; }

	private:
		struct Cell
		{
			std::atomic<std::size_t> sequence;
			T storage;
		};

		static constexpr std::size_t MASK = Capacity - 1;

		alignas(CACHE_LINE_SIZE) std::atomic<std::size_t> m_enqueuePos;
		alignas(CACHE_LINE_SIZE) std::atomic<std::size_t> m_dequeuePos;
		alignas(CACHE_LINE_SIZE) std::array<Cell, Capacity> m_cells;
	};
} // namespace multi::details
