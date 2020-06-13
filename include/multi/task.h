/*
 *  Created by LuckyNeko on 13/05/2020.
 *  Copyright 2020 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#ifndef _MULTI_TASK_H_
#define _MULTI_TASK_H_

#include <functional>

namespace multi
{
	class JobContext;
	using Task = std::function<void(JobContext&)>;

} // namespace multi

#endif // _MULTI_TASK_H_