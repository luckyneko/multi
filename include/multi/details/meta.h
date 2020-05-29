/*
 *  Created by LuckyNeko on 17/05/2020.
 *  Copyright 2020 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#ifndef _MULTI_META_H_
#define _MULTI_META_H_

#include <utility>

namespace multi
{
	// Run func on all items sequentially
	template <typename FUNC, typename ITEM, typename... ITEMS>
	void doOnAll(FUNC func, ITEM&& item, ITEMS&&... items)
	{
		doOnAll(func, std::forward<ITEM>(item));
		doOnAll(func, std::forward<ITEMS>(items)...);
	}

	// Run func on single item
	template <typename FUNC, typename ITEM>
	void doOnAll(FUNC func, ITEM&& item)
	{
		func(std::forward<ITEM>(item));
	}

} // namespace multi

#endif // _MULTI_META_H_