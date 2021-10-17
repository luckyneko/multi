/*
 *  Created by LuckyNeko on 02/10/2021.
 *  Copyright 2021 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#ifndef _MULTI_TASK_H_
#define _MULTI_TASK_H_

#include <functional>

namespace multi
{
	using Task = std::function<void()>;
} // namespace multi

#endif // _MULTI_TASK_H_