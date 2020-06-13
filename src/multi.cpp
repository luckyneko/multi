/*
 *  Created by LuckyNeko on 16/03/2020.
 *  Copyright 2020 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "multi/multi.h"

namespace multi
{
	static Context STATIC_CONTEXT;
	Context*& context()
	{
		static Context* currentContext = &STATIC_CONTEXT;
		return currentContext;
	}
}