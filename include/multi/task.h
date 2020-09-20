/*
 *  Created by LuckyNeko on 09/05/2020.
 *  Copyright 2013 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#ifndef _MULTI_TASK_H_
#define _MULTI_TASK_H_

#include <functional>

namespace multi
{
	class Job;

	/*
	 * Task
	 * Object to store a task. Will run locally on deletion.
	 */
	class Task
	{
	public:
		inline Task()
			: m_func(nullptr)
		{
		}

		template <typename T>
		Task(T&& func)
			: m_func(std::move(func))
		{
		}

		Task(const Task&) = delete;

		inline Task(Task&& mv)
		{
			m_func = std::move(mv.m_func);
			mv.m_func = nullptr;
		}

		~Task();

		void run(Job&);

	private:
		std::function<void(Job&)> m_func;
	};
} // namespace multi

#endif // _MULTI_TASK_H_