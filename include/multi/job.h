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
	class Job
	{
	public:
		inline Job(Task&& task) { m_root = std::move(task); }
		Job(const Job&) = delete;
		inline Job(Job&& mv) { m_root = std::move(mv.popTask()); }
		inline ~Job()
		{
			if (m_root)
			{
				JobContext context;
				m_root(context);
			}
		}

		inline Task&& popTask()
		{
			auto t = std::move(m_root);
			m_root = nullptr;
			return std::move(t);
		}

	private:
		Task m_root;
	};
} // namespace multi

#endif // _MULTI_JOB_H_