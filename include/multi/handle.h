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
		bool complete() const { return !valid() || m_handle.wait_for(std::chrono::seconds(0)) == std::future_status::ready; }

		// Check if handle has assigned job
		bool valid() const { return m_handle.valid(); }

		// Wait for job to complete. Rethrows any exception raised by the task.
		// Idempotent: repeated calls re-throw the same exception.
		void wait();

		// Release the handle without waiting. The task still runs to completion
		// on its worker, but its result/exception becomes unobservable. Useful
		// for true fire-and-forget when you want to avoid ~Handle's auto-wait.
		void detach() { clear(); }

		Handle& operator=(Handle&& a);

	private:
		// Drop our reference to the shared state without waiting. Used by
		// detach() and by move ops to vacate the source after stealing.
		void clear()
		{
			m_handle = std::shared_future<void>();
			m_context = nullptr;
		}

		std::shared_future<void> m_handle;
		Context* m_context = nullptr;
	};

} // namespace multi

#endif // _MULTI_HANDLE_H_
