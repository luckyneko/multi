/*
 *  Created by LuckyNeko on 15/03/2020.
 *  Copyright 2013 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#ifndef _MULTI_CONTEXT_H_
#define _MULTI_CONTEXT_H_

#include "multi/handle.h"
#include "multi/job.h"
#include <memory>

namespace multi
{
	class JobNode;
	class ThreadPool;

	class Context
	{
	public:
		Context();
		Context(const Context&) = delete;
		~Context();

		void start(size_t threadCount = std::thread::hardware_concurrency());
		void stop();
		size_t threadCount() const;

		inline Handle async(Job&& job) { return async(job.popTask()); }
		Handle async(Task&& task);

		void queueJobNode(JobNode* node);

	private:
		std::unique_ptr<ThreadPool> m_threadPool;
	};
} // namespace multi

#endif // _MULTI_CONTEXT_H_