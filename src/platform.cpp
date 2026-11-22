/*
 *  Created by LuckyNeko on 22/11/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "multi/details/platform.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <thread>

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
#else
#	include <time.h>
#	if defined(__APPLE__) || defined(__linux__) || defined(__ANDROID__)
#		include <pthread.h>
#	endif
#endif

namespace multi::details
{
	// macOS only names the current thread, so must be called from within
	// the thread. Names must fit Linux's 16-byte limit.
	void setCurrentThreadName(const char* name) noexcept
	{
#if defined(_WIN32)
		// SetThreadDescription wants UTF-16; worker names are ASCII so a
		// byte-to-wchar copy suffices. Available since Windows 10 1607.
		wchar_t wname[32];
		std::size_t i = 0;
		for (; i + 1 < sizeof(wname) / sizeof(wname[0]) && name[i] != '\0'; ++i)
			wname[i] = static_cast<wchar_t>(static_cast<unsigned char>(name[i]));
		wname[i] = L'\0';
		SetThreadDescription(GetCurrentThread(), wname);
#elif defined(__APPLE__)
		pthread_setname_np(name);
#elif defined(__linux__) || defined(__ANDROID__)
		pthread_setname_np(pthread_self(), name);
#else
		(void)name;
#endif
	}

	void preciseSleep(std::chrono::microseconds d)
	{
#if defined(_WIN32)
		// sleep_for rounds up to the system timer tick, ~15.6 ms unless some
		// code in the process has called timeBeginPeriod. A high-resolution
		// waitable timer wakes within about 0.5 ms and leaves the process's
		// timer resolution alone.
		//
		// One timer per thread, created at the thread's first call and closed
		// when the thread exits, by thread_local destruction. A thread is in
		// at most one sleep at a time, so one timer serves every sleep on it:
		// a long wait does not create and close a kernel object per sleep, a
		// thread that never sleeps here never makes one, and none is left
		// behind.
		struct Timer
		{
			HANDLE handle = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_MODIFY_STATE | SYNCHRONIZE);

			~Timer()
			{
				if (handle != nullptr)
					CloseHandle(handle);
			}
		};
		thread_local Timer timer;

		// Fall through to sleep_for if the timer could not be created (before
		// Windows 10 1803) or armed, or the wait failed.
		if (timer.handle != nullptr)
		{
			LARGE_INTEGER due;
			due.QuadPart = -static_cast<LONGLONG>(d.count()) * 10; // relative, in 100 ns units
			if (SetWaitableTimer(timer.handle, &due, 0, nullptr, nullptr, FALSE) && WaitForSingleObject(timer.handle, INFINITE) == WAIT_OBJECT_0)
				return;
		}
#endif
		std::this_thread::sleep_for(d);
	}

	std::chrono::microseconds threadCpuTime() noexcept
	{
#if defined(_WIN32)
		FILETIME creation, exited, kernel, user;
		if (!GetThreadTimes(GetCurrentThread(), &creation, &exited, &kernel, &user))
			return std::chrono::microseconds(-1);
		const auto hundredNs = [](const FILETIME& t)
		{ return (static_cast<std::uint64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
		return std::chrono::microseconds(static_cast<std::int64_t>((hundredNs(kernel) + hundredNs(user)) / 10));
#elif defined(CLOCK_THREAD_CPUTIME_ID)
		timespec ts{};
		if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) != 0)
			return std::chrono::microseconds(-1);
		return std::chrono::seconds(ts.tv_sec) + std::chrono::microseconds(ts.tv_nsec / 1000);
#else
		return std::chrono::microseconds(-1);
#endif
	}
} // namespace multi::details
