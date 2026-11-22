/*
 *  Created by LuckyNeko on 22/11/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include <chrono>
#include <cstddef>
#include <thread>

namespace multi::details
{
	/*
	 * Waiter backoff, for a thread blocked in Context::waitUntil (and so waitAll /
	 * waitAny) or in a parallel / each / range dispatch. The waiter steals pool
	 * work while it waits. While there is nothing to steal it first spins as it
	 * always has, yielding between steal attempts, for WAIT_SPIN_TIME; after that
	 * it sleeps between attempts, doubling from WAIT_SLEEP_MIN up to
	 * WAIT_SLEEP_MAX. A successful steal goes back to spinning.
	 *
	 * Trade-off: a long wait burns at most about WAIT_SPIN_TIME of a core instead
	 * of the whole wait, and in exchange notices completion, or new work to steal,
	 * up to one sleep late. Work queued onto a waiting worker's own deque is not
	 * held up by that: the waiter is not parked, so the push wakes a parked
	 * worker, if there is one, to steal it. Only with no worker parked does it
	 * wait for a thread to come looking, at the latest this waiter's next poll,
	 * up to one sleep away. Waits shorter than WAIT_SPIN_TIME never sleep: that
	 * covers the waits in the benchmarks (microseconds to a few milliseconds)
	 * and a whole 60 Hz frame.
	 *
	 * Worst-case added wake-up latency, once a wait has backed off: WAIT_SLEEP_MAX
	 * plus the sleep's own slack. Linux, Android, macOS and iOS sleep with
	 * sleep_for, whose slack is tens of microseconds. Windows sleeps on a
	 * high-resolution waitable timer instead, whose slack is about 0.5 ms,
	 * because sleep_for there rounds up to the system timer tick (~15.6 ms by
	 * default). Measured on Windows 11, a backed-off waiter noticed completion at
	 * worst 1.56 ms late with the timer, against 15.1 ms with sleep_for. Before
	 * Windows 10 1803, which lacks that timer, it falls back to sleep_for and so
	 * to one tick (1-2 ms once the process has called timeBeginPeriod(1)).
	 * Measured on the same machine: any sleep_for from 1 us to 4 ms took ~15.5 ms,
	 * and yield() ~0.3 us.
	 */
	inline constexpr std::chrono::milliseconds WAIT_SPIN_TIME{20};
	inline constexpr std::chrono::microseconds WAIT_SLEEP_MIN{50};
	inline constexpr std::chrono::microseconds WAIT_SLEEP_MAX{1000};
	/// Failed steal attempts between clock reads while spinning.
	inline constexpr std::size_t WAIT_CLOCK_INTERVAL = 64;

	/// One waiter's place in the backoff above. Not thread-safe: one per wait.
	class WaitBackoff
	{
	public:
		/// A steal succeeded: spin again.
		void reset() noexcept
		{
			m_idlePolls = 0;
			m_sleep = std::chrono::microseconds::zero();
		}

		/// A steal attempt found nothing: yield while spinning, sleep once backed off.
		void idle()
		{
			if (m_sleep == std::chrono::microseconds::zero())
			{
				std::this_thread::yield();
				if (++m_idlePolls % WAIT_CLOCK_INTERVAL == 0)
					checkSpinTime();
			}
			else
			{
				sleep();
			}
		}

	private:
		void checkSpinTime();
		void sleep();

		std::size_t m_idlePolls = 0;
		std::chrono::steady_clock::time_point m_idleSince;
		std::chrono::microseconds m_sleep = std::chrono::microseconds::zero(); // zero while spinning
	};
} // namespace multi::details
