/*
 *  Created by LuckyNeko on 11/03/2020.
 *  Copyright 2020 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#ifndef _MULTI_QUEUE_H_
#define _MULTI_QUEUE_H_

#include <mutex>
#include <queue>

namespace multi
{
	template <typename T>
	class Queue
	{
	public:
		inline void push(const T& value)
		{
			std::lock_guard<std::mutex> lk(m_lock);
			m_queue.push(value);
		}

		template <typename... ARGS>
		void emplace(ARGS&&... args)
		{
			std::lock_guard<std::mutex> lk(m_lock);
			m_queue.emplace(std::forward<ARGS>(args)...);
		}

		inline bool empty()
		{
			std::lock_guard<std::mutex> lk(m_lock);
			return m_queue.empty();
		}

		inline bool pop(T* value)
		{
			std::lock_guard<std::mutex> lk(m_lock);
			if (!m_queue.empty())
			{
				*value = std::move(m_queue.front());
				m_queue.pop();
				return true;
			}
			return false;
		}

	private:
		std::queue<T> m_queue;
		std::mutex m_lock;
	};
} // namespace multi

#endif // _MULTI_QUEUE_H_