/*
 *  Created by LuckyNeko on 15/03/2020.
 *  Copyright 2013 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#ifndef _MULTI_CONTEXT_H_
#define _MULTI_CONTEXT_H_

#include "multi/details/icontext.h"
#include "multi/details/threadpool.h"
#include "multi/handle.h"
#include "multi/job.h"
#include <memory>

namespace multi
{
	class JobNode;

	/*
	 * Context
	 * Holds multi-wide state.
	 */
	class Context : public iContext
	{
	public:
		Context() = default;
		Context(const Context&) = delete;

		void start(size_t threadCount = std::thread::hardware_concurrency());
		void stop();
		size_t threadCount() const;

		// Launch job/task onto a thread
		// @return Handle to wait on parallel job.
		Handle async(Task&& task);

	protected:
		// iContext
		JobNode* allocJobNode(JobNode* parent, Task&& task, JobNode* next = nullptr) final;
		void deallocJobNode(JobNode* node) final;
		void queueJobNode(JobNode* node) final;

	private:
		ThreadPool m_threadPool;
	};
} // namespace multi

#endif // _MULTI_CONTEXT_H_