/*
 *  Created by LuckyNeko on 19/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

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
