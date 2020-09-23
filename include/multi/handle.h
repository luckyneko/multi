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
	/*
	 * Handle
	 * Tracks state of async task/job. Will auto wait on destruction.
	 */
	class Handle
	{
	public:
		Handle() = default;
		inline Handle(std::future<void>&& hdl) { m_handle = std::move(hdl); }
		Handle(const Handle&) = delete;
		inline Handle(Handle&& a)
		{
			m_handle = std::move(a.m_handle);
			a.reset();
		}
		inline ~Handle() { wait(); }

		// Check if job is complete
		inline bool complete() const { return !valid() || m_handle.wait_for(std::chrono::seconds(0)) == std::future_status::ready; }

		// Reset handle
		inline void reset() { m_handle = std::future<void>(); }

		// Check if handle has assigned job
		inline bool valid() const { return m_handle.valid(); }

		// Wait for job to complete
		inline void wait()
		{
			if (valid())
				m_handle.wait();
		}

		inline Handle& operator=(Handle&& a)
		{
			m_handle = std::move(a.m_handle);
			a.reset();
			return *this;
		}

	private:
		std::future<void> m_handle;
	};

} // namespace multi

#endif // _MULTI_HANDLE_H_