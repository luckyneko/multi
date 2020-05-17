/*
 *  Created by LuckyNeko on 15/03/2020.
 *  Copyright 2020 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#ifndef _MULTI_THREADPOOL_H_
#define _MULTI_THREADPOOL_H_

#include "multi/details/nulllock.h"
#include "multi/details/queue.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <thread>

namespace multi
{
	class ThreadPool
	{
	public:
		ThreadPool();
		ThreadPool(const ThreadPool&) = delete;
		~ThreadPool();

		void start(size_t threadCount = std::thread::hardware_concurrency());
		void stop();

		void queue(std::function<void()>&& func);
		size_t threadCount() const;

	private:
		void threadMain();

	private:
		std::vector<std::thread> m_threads;
		Queue<std::function<void()>> m_queue;
		NullLock m_lock;
		std::condition_variable_any m_sync;
		std::atomic<bool> m_active;
	};
} // namespace multi

#endif // _MULTI_THREADPOOL_H_