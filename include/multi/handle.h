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
	class Handle
	{
	public:
		inline Handle() {}
		inline Handle(std::future<void>&& hdl) { m_handle = std::move(hdl); }
		Handle(const Handle&) = delete;
		inline Handle(Handle&& a) { m_handle = std::move(a.m_handle); }
		inline ~Handle() { wait(); }

		inline bool ready() { return valid() && m_handle.wait_for(std::chrono::seconds(0)) == std::future_status::ready; }
		inline bool valid() { return m_handle.valid(); }
		inline void wait()
		{
			if (valid())
				m_handle.wait();
		}

	private:
		std::future<void> m_handle;
	};

} // namespace multi

#endif // _MULTI_HANDLE_H_