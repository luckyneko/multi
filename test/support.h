/*
 *  Created by LuckyNeko on 22/11/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include <atomic>
#include <chrono>
#include <thread>

/*
 * Helpers shared by the test files. Every test file links into the one
 * test-multi binary, so what a file defines for itself stays in a single
 * anonymous namespace at the top of that file; what more than one file can use
 * lives here, under a name.
 */
namespace support
{
	/// Poll @p pred until it returns true or @p timeout has passed, sleeping a
	/// millisecond between polls.
	/// @return Whether pred came true. Bounding a wait this way makes a
	///         regression fail an assertion instead of hanging the suite.
	template <class Pred>
	bool waitFor(Pred pred, std::chrono::steady_clock::duration timeout)
	{
		const auto deadline = std::chrono::steady_clock::now() + timeout;
		while (!pred())
		{
			if (std::chrono::steady_clock::now() >= deadline)
				return false;
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		return true;
	}

	/// Spin, without stealing pool work, until another thread sets @p flag.
	/// Use it to know that a task has started on a worker before the test goes
	/// on: a waiter that steals could otherwise take the task and run it itself.
	inline void awaitFlag(const std::atomic<bool>& flag)
	{
		while (!flag.load(std::memory_order_acquire))
			std::this_thread::yield();
	}
} // namespace support
