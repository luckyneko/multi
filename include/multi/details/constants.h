/*
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#ifndef _MULTI_CONSTANTS_H_
#define _MULTI_CONSTANTS_H_

#include <new>

namespace multi
{
#ifdef __cpp_lib_hardware_interference_size
	static constexpr size_t CACHE_LINE_SIZE = std::hardware_destructive_interference_size;
#else
	static constexpr size_t CACHE_LINE_SIZE = 64;
#endif

} // namespace multi

#endif // _MULTI_CONSTANTS_H_
