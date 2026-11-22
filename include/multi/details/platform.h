/*
 *  Created by LuckyNeko on 19/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include <chrono>
#include <cstddef>
#include <new>

namespace multi
{
	// GCC throws errors on std::hardware_destructive_interference_size
#if defined(__cpp_lib_hardware_interference_size) && (!defined(__GNUC__) || defined(__clang__))
	static constexpr std::size_t CACHE_LINE_SIZE = std::hardware_destructive_interference_size;
#else
	static constexpr std::size_t CACHE_LINE_SIZE = 64;
#endif

} // namespace multi

// MSVC's C4324 reports that an alignas() padded a struct - which, for a member deliberately given a
// cache line of its own, is the entire point: these types exist so that two threads writing
// adjacent counters do not share one.
//
// Stated HERE, once, rather than at each of the three declarations that need it, and as a pragma
// rather than a build flag so that it travels with the header into every consumer and every build
// system. It WAS /wd4324 in CMakeLists.txt, marked PRIVATE - which covered multi's own sources and
// left every consumer to trip over it, so multi's Windows CI stayed green while a consumer's went
// red. A suppression for something declared in a public header belongs at the declaration.
#if defined(_MSC_VER)
#	define MULTI_BEGIN_CACHE_ALIGNED \
		__pragma(warning(push)) __pragma(warning(disable : 4324))
#	define MULTI_END_CACHE_ALIGNED __pragma(warning(pop))
#else
#	define MULTI_BEGIN_CACHE_ALIGNED
#	define MULTI_END_CACHE_ALIGNED
#endif

/*
 * The library's OS-specific calls. They are defined in src/platform.cpp, the
 * one source file that includes <windows.h> or <pthread.h>: add a new one
 * here and there rather than putting an #if in the code that needs it.
 */
namespace multi::details
{
	/// Name the calling thread, for debuggers and profilers. Best-effort: a
	/// failure is ignored. Pass a short ASCII name (Linux keeps 15 characters).
	/// macOS can only name the current thread, so call this from inside it.
	void setCurrentThreadName(const char* name) noexcept;

	/// Sleep the calling thread for about @p d, as precisely as the platform
	/// can. That is sleep_for everywhere but Windows, where sleep_for rounds up
	/// to the system timer tick (~15.6 ms by default): there it waits on a
	/// high-resolution timer, which wakes within about 0.5 ms, and falls back to
	/// sleep_for where that timer does not exist (before Windows 10 1803).
	void preciseSleep(std::chrono::microseconds d);

	/// CPU time the calling thread has used so far, or a negative duration where
	/// the platform cannot tell. Only the tests read it.
	std::chrono::microseconds threadCpuTime() noexcept;
} // namespace multi::details
