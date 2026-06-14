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
