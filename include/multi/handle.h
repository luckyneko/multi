/*
 *  Created by LuckyNeko on 27/04/2020.
 *  Copyright 2020 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include <chrono>
#include <future>
#include <type_traits>

namespace multi
{
	/*
	 * Handle<T>
	 * Tracks state of an async task. Does not block on destruction.
	 *
	 * The Handle itself offers only a plain, NON-stealing wait(); to wait
	 * while helping drain the pool, use multi::waitAll / waitAny / waitUntil.
	 */
	template <class T = void>
	class Handle
	{
	public:
		Handle() = default;
		explicit Handle(std::future<T>&& hdl);
		Handle(const Handle&) = delete;
		Handle(Handle&& a) noexcept;
		~Handle() = default;

		// Check if job is complete
		bool complete() const;

		// Check if handle has assigned job
		bool valid() const;

		// Block (without participating in work-stealing) until the task
		// completes. A default-constructed Handle returns immediately. Does
		// not rethrow — observe a task exception via get().
		void wait() const;

		// Returns value if complete
		bool get(T* value);

	private:
		std::shared_future<T> m_handle;
	};

} // namespace multi

#include "multi/details/handle.inl"
