/*
 *  Created by LuckyNeko on 15/03/2020.
 *  Copyright 2020 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "multi/context.h"
#include "multi/details/recipejob.h"
#include "multi/recipe.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <thread>
#include <utility>

#if defined(_WIN32)
#	ifndef WIN32_LEAN_AND_MEAN
#		define WIN32_LEAN_AND_MEAN
#	endif
#	ifndef NOMINMAX
#		define NOMINMAX
#	endif
#	include <windows.h>
// Windows 10 1803 and later; older SDKs lack the name.
#	ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#		define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#	endif
#endif

namespace multi::details
{
	namespace
	{
#if defined(_WIN32)
		/*
		 * sleep_for on Windows rounds up to the system timer tick, ~15.6 ms unless
		 * some code in the process has called timeBeginPeriod. A high-resolution
		 * waitable timer wakes within about 0.5 ms and leaves the process's timer
		 * resolution alone.
		 *
		 * Lifetime: one per thread, created at the thread's first backed-off sleep
		 * and closed when the thread exits, by thread_local destruction. A thread
		 * is in at most one sleep at a time: a nested wait runs inside a task that
		 * the outer wait stole, never during the outer wait's sleep. So one timer
		 * serves every wait on its thread, a long wait does not pay for creating
		 * and closing a kernel object, a thread that never backs off never makes
		 * one, and no wait can leave one behind.
		 */
		class HighResTimer
		{
		public:
			HighResTimer() noexcept
				: m_handle(CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
												  TIMER_MODIFY_STATE | SYNCHRONIZE))
			{
			}

			~HighResTimer()
			{
				if (m_handle != nullptr)
					CloseHandle(m_handle);
			}

			HighResTimer(const HighResTimer&) = delete;
			HighResTimer& operator=(const HighResTimer&) = delete;

			// False, having not slept, if the timer could not be created (before
			// Windows 10 1803) or armed, or the wait failed.
			bool sleep(std::chrono::microseconds d) noexcept
			{
				if (m_handle == nullptr)
					return false;
				LARGE_INTEGER due;
				due.QuadPart = -static_cast<LONGLONG>(d.count()) * 10; // relative, in 100 ns units
				if (!SetWaitableTimer(m_handle, &due, 0, nullptr, nullptr, FALSE))
					return false;
				return WaitForSingleObject(m_handle, INFINITE) == WAIT_OBJECT_0;
			}

		private:
			HANDLE m_handle;
		};
#endif

		void sleepFor(std::chrono::microseconds d)
		{
#if defined(_WIN32)
			thread_local HighResTimer timer;
			if (timer.sleep(d))
				return;
#endif
			std::this_thread::sleep_for(d);
		}
	} // namespace

	void WaitBackoff::checkSpinTime()
	{
		// The streak's clock starts at its first check, WAIT_CLOCK_INTERVAL
		// attempts in, so a short wait never reads the clock at all.
		const auto now = std::chrono::steady_clock::now();
		if (m_idlePolls == WAIT_CLOCK_INTERVAL)
			m_idleSince = now;
		else if (now - m_idleSince >= WAIT_SPIN_TIME)
			m_sleep = WAIT_SLEEP_MIN;
	}

	void WaitBackoff::sleep()
	{
		sleepFor(m_sleep);
		m_sleep = std::min(m_sleep * 2, std::chrono::microseconds(WAIT_SLEEP_MAX));
	}
} // namespace multi::details

namespace multi
{
	bool Context::start(int threadCount)
	{
		return m_workerPool.start(threadCount);
	}

	void Context::stop()
	{
		m_workerPool.stop();
	}

	size_t Context::threadCount() const
	{
		return m_workerPool.threadCount();
	}

	RecipeHandle Context::async(Recipe&& recipe)
	{
		auto job = std::make_shared<details::RecipeJob>(
			std::move(recipe),
			[this](details::Task&& task)
			{ m_workerPool.submit(std::move(task)); });
		auto future = job->getFuture();
		job->start();
		return RecipeHandle(Handle<>(std::move(future)), std::move(job));
	}

	bool Context::tryRunSteal()
	{
		details::Task stolen;
		if (m_workerPool.tryStealAny(&stolen))
		{
			try
			{
				stolen();
			}
			catch (...)
			{
			}
			return true;
		}
		return false;
	}
} // namespace multi
