/*
 *  Created by LuckyNeko on 09/05/2020.
 *  Copyright 2013 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#ifndef _MULTI_JOB_H_
#define _MULTI_JOB_H_

#include "multi/jobcontext.h"

namespace multi
{
	/*
	 * Job
	 * Object to store a task. Will run locally on deletion.
	 */
	class Job
	{
	public:
		inline Job(Task&& task)
		{
			m_root = std::move(task);
			m_set = true;
		}

		Job(const Job&) = delete;

		inline Job(Job&& mv)
		{
			m_root = std::move(mv.m_root);
			m_set = mv.m_set;
			mv.m_set = false;
		}

		inline ~Job()
		{
			if (m_set)
			{
				JobContext context;
				m_root(context);
			}
		}

		// Internal use only
		inline Task&& popTask()
		{
			m_set = false;
			return std::move(m_root);
		}

	private:
		Task m_root;
		bool m_set;
	};
} // namespace multi

#endif // _MULTI_JOB_H_