/*
 *  Created by LuckyNeko on 15/03/2020.
 *  Copyright 2020 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#ifndef _MULTI_THREADPOOL_H_
#define _MULTI_THREADPOOL_H_

#include "multi/details/jobqueue.h"
#include "multi/details/nulllock.h"

#include <atomic>
#include <condition_variable>
#include <thread>

namespace multi
{
	/*
	 * ThreadPool
	 * Manage set of threads to run Jobs
	 */
	class ThreadPool
	{
	public:
		ThreadPool();
		ThreadPool(const ThreadPool&) = delete;
		~ThreadPool();

		void start(size_t threadCount);
		void stop();

		void queue(const Job& jb);
		inline bool isActive() const { return m_active; }
		inline size_t threadCount() const { return m_threads.size(); }

	private:
		void threadMain();

	private:
		std::vector<std::thread> m_threads;
		JobQueue m_queue;
		NullLock m_lock;
		std::condition_variable_any m_sync;
		std::atomic<bool> m_active;
	};
} // namespace multi

#endif // _MULTI_THREADPOOL_H_