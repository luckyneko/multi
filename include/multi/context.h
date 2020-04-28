/*
 *  Created by LuckyNeko on 15/03/2020.
 *  Copyright 2013 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#ifndef _MULTI_CONTEXT_H_
#define _MULTI_CONTEXT_H_

#include "multi/details/handle.h"
#include "multi/details/jobnode.h"
#include "multi/details/threadpool.h"
#include <map>

namespace multi
{
	class Context
	{
	public:
		Context();
		Context(const Context&) = delete;
		~Context();

		void start(size_t threadCount = std::thread::hardware_concurrency());
		void stop();
		size_t threadCount() const;

		Handle async(Function&& func);

		inline void parallel(Function&& func)
		{
			queueJobNode(allocJobNode(activeJobNode(), std::move(func)));
		}

		template <typename ITER, typename FUNC>
		void parallelFor(ITER begin, ITER end, FUNC&& func)
		{
			auto parent = activeJobNode();
			for (auto iter = begin; iter != end; ++iter)
				queueJobNode(allocJobNode(parent, [iter, func]() { func(*iter); }));
		}

		template <typename... FUNCS>
		void serial(FUNCS... funcs)
		{
			queueJobNode(allocJobNode(activeJobNode(), std::forward<FUNCS>(funcs)...));
		}

		template <typename ITER, typename FUNC>
		void serialFor(ITER begin, ITER end, FUNC&& func)
		{
			auto parent = activeJobNode();
			JobNode* head = nullptr;
			for (auto iter = end; iter != begin; --iter)
				head = allocJobNode(parent, [iter, func]() { func(*(iter - 1)); }, head);
			queueJobNode(head);
		}

	private:
		template <typename... FUNCS>
		JobNode* allocJobNode(JobNode* parent, Function&& func, FUNCS... funcs)
		{
			return allocJobNode(parent, std::forward<Function>(func), allocJobNode(parent, std::forward<FUNCS>(funcs)...));
		}
		JobNode* allocJobNode(JobNode* parent, Function&& func, JobNode* next = nullptr);
		JobNode*& activeJobNode();
		void queueJobNode(JobNode* node);

	private:
		ThreadPool m_threadPool;
	};
} // namespace multi

#endif // _MULTI_CONTEXT_H_