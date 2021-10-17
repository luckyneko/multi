/*
 *  Created by LuckyNeko on 02/10/2021.
 *  Copyright 2021 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#ifndef _MULTI_JOB_H_
#define _MULTI_JOB_H_

#include "multi/task.h"
#include <future>
#include <vector>

namespace multi
{
	class Job
	{
	public:
		Job();
		Job(std::vector<Task>&& tasks);
		Job(const Job& cpy);
		~Job();

		void reset();
		inline bool valid() const { return m_inner != nullptr; }
		inline bool hasWork() const { return valid() && m_inner->next < m_inner->tasks.size(); }
		inline bool complete() const { return !valid() || m_inner->unfinished == 0; }

		bool tryRun();
		void waitRun();

		Job& operator=(const Job& cpy);
		inline bool operator==(const Job& cpy) const { return m_inner == cpy.m_inner; }
		inline bool operator!=(const Job& cpy) const { return m_inner != cpy.m_inner; }

	private:
		struct Inner
		{
			std::vector<Task> tasks;
			std::atomic<size_t> next;
			std::atomic<size_t> unfinished;
			std::atomic<size_t> ref;
		};
		Inner* m_inner;
	};
} // namespace multi

#endif // _MULTI_JOB_H_