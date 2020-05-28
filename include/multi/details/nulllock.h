/*
 *  Created by LuckyNeko on 15/03/2020.
 *  Copyright 2020 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#ifndef _MULTI_NULLLOCK_H_
#define _MULTI_NULLLOCK_H_

namespace multi
{
	/*
	 * NullLock
	 * Empty lock class for use with std::condition_variable
	 */
	class NullLock
	{
	public:
		inline void lock() {}
		inline void unlock() {}
	};
} // namespace multi

#endif // _MULTI_NULLLOCK_H_