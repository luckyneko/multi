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
	/**
	 * @brief Tracks the state of an async task; does not block on destruction.
	 *
	 * @tparam T Result type produced by the task (void by default).
	 *
	 * Offers only a plain, non-stealing wait(). To wait while helping drain the
	 * pool, use multi::waitAll / waitAny / waitUntil.
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

		/// @return True if the task has completed, or the handle is empty.
		bool complete() const;

		/// @return True if this handle refers to a task.
		bool valid() const;

		/// Block (without work-stealing) until the task completes. A
		/// default-constructed handle returns immediately. Does not rethrow;
		/// observe a task exception via get().
		void wait() const;

		/// Non-blocking result fetch.
		/// @param value Destination, written only on success.
		/// @return True (and writes @p value) if complete; false otherwise.
		///         Rethrows a stored task exception.
		template <class U = T, std::enable_if_t<!std::is_void_v<U>, int> = 0>
		bool get(U* value);

		/// Non-blocking completion fetch for void handles.
		/// @return True if complete; false otherwise.
		///         Rethrows a stored task exception.
		template <class U = T, std::enable_if_t<std::is_same_v<U, T> && std::is_void_v<U>, int> = 0>
		bool get();

	private:
		std::shared_future<T> m_handle;
	};

} // namespace multi

#include "multi/details/handle.inl"
