/*
 *  Created by LuckyNeko on 27/04/2020.
 *  Copyright 2020 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#ifndef _MULTI_HANDLE_H_
#define _MULTI_HANDLE_H_

#include <future>

namespace multi
{
	class Context;

	/*
	 * Handle
	 * Tracks state of async task/job. Will auto wait on destruction.
	 *
	 * wait() rethrows any exception thrown by the task. The destructor
	 * swallows exceptions so dropping a Handle cannot call std::terminate.
	 */
	class Handle
	{
	public:
		Handle() = default;
		// Internally stores a shared_future so wait() can rethrow the stored
		// exception idempotently without invalidating the handle.
		Handle(std::future<void>&& hdl, Context* ctx = nullptr);
		Handle(const Handle&) = delete;
		Handle(Handle&& a);
		~Handle();

		// Check if job is complete
		inline bool complete() const { return !valid() || m_handle.wait_for(std::chrono::seconds(0)) == std::future_status::ready; }

		// Reset handle
		inline void reset()
		{
			m_handle = std::shared_future<void>();
			m_context = nullptr;
		}

		// Check if handle has assigned job
		inline bool valid() const { return m_handle.valid(); }

		// Wait for job to complete. Rethrows any exception raised by the task.
		// Idempotent: repeated calls re-throw the same exception.
		void wait();

		Handle& operator=(Handle&& a);

	private:
		std::shared_future<void> m_handle;
		Context* m_context = nullptr;
	};

} // namespace multi

#endif // _MULTI_HANDLE_H_
