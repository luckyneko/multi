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
	Context& context()
	{
		static Context g_context;
		return g_context;
	}
}