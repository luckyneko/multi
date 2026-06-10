/*
 *  Created by LuckyNeko on 17/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "multi/details/chaselevdeque.h"
#include "multi/details/mpmcqueue.h"
#include "multi/details/task.h"

#include <cstddef>

namespace multi::details
{
	/*
	 * WorkStealDeque
	 * Per-worker deque composed of two lock-free primitives:
	 *  - a fixed-capacity Chase-Lev SPMC deque (LOCAL_CAP) for the owner's
	 *    LIFO fast path,
	 *  - a bounded MPMC ring (OVERFLOW_CAP) for spillover and external submits.
	 *
	 * Push routing is split: the owner uses tryPushLocal to spawn nested work
	 * (Chase-Lev fast path, no MPMC contention); any other thread uses
	 * tryPushRemote to land work on the overflow ring where stealers can also
	 * see it. The owner periodically refills its local Chase-Lev from overflow
	 * when running low so stealers always see fresh stealable work.
	 *
	 * All push/pop entry points are non-blocking (tryX). Caller policy decides
	 * whether to spin, fall back, or run inline on a full ring.
	 */
	class WorkStealDeque
	{
	public:
		// Owner only. Tries local Chase-Lev first; on full, falls back to
		// overflow. Returns false only if both are full.
		bool tryPushLocal(Task&& task);

		// Any non-owner thread. Routes to overflow MPMC ring. Returns false
		// when the ring is full.
		bool tryPushRemote(Task&& task);

		// Owner-only. Refills local from overflow if low, then pops local; falls
		// back to draining one from overflow if local is empty.
		bool pop(Task* task);

		// Any non-owner thread. Tries local Chase-Lev's top; on empty, falls
		// through to overflow (which any thread can drain).
		bool steal(Task* task);

		std::size_t sizeHint() const;
		bool empty() const;

		// Per-half observability for tests, benchmarks, and routing diagnostics.
		std::size_t localSizeHint() const { return m_local.sizeHint(); }
		std::size_t overflowSizeHint() const { return m_overflow.sizeHint(); }

	private:
		static constexpr std::size_t LOCAL_CAP    = 256;
		static constexpr std::size_t OVERFLOW_CAP = 4096;
		static constexpr std::size_t REFILL_LOW   = 16;
		static constexpr std::size_t REFILL_BATCH = 32;

		// Owner-only.
		void refillFromOverflow();

		ChaseLevDeque<Task, LOCAL_CAP> m_local;
		MpmcQueue<Task, OVERFLOW_CAP>  m_overflow;
	};
} // namespace multi::details
