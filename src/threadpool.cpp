/*
 *  Created by LuckyNeko on 15/03/2020.
 *  Copyright 2020 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "multi/details/threadpool.h"

#include <assert.h>
#include <chrono>

namespace multi
{
	ThreadPool::ThreadPool()
		: m_threads()
		, m_queue()
		, m_lock()
		, m_sync()
		, m_active(false)
	{
	}

	ThreadPool::~ThreadPool()
	{
		assert(m_threads.size() == 0);
	}

	void ThreadPool::start(size_t threadCount)
	{
		assert(!m_active);
		if (threadCount != 0)
		{
			m_active = true;

			auto func = std::bind(&ThreadPool::threadMain, this);
			m_threads.reserve(threadCount);
			for (size_t i = 0; i < threadCount; ++i)
				m_threads.emplace_back(func);
		}
	}

	void ThreadPool::stop()
	{
		m_active = false;
		m_sync.notify_all();
		for (auto& thread : m_threads)
			thread.join();
		m_threads.clear();
	}

	void ThreadPool::queue(std::function<void()>&& func)
	{
		if (m_active)
		{
			m_queue.emplace(std::move(func));
			m_sync.notify_one();
		}
		else
		{
			func();
		}
	}

	size_t ThreadPool::threadCount() const
	{
		return m_threads.size();
	}

	void ThreadPool::threadMain()
	{
		std::unique_lock<NullLock> lk(m_lock);
		std::function<void()> func;
		while (m_active)
		{
			m_sync.wait_for(lk, std::chrono::seconds(1), [&]() { return !m_active || !m_queue.empty(); });
			while (m_queue.pop(&func))
			{
				if (func)
					func();
				func = nullptr;
			}
		}
	}
} // namespace multi