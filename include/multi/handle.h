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
#include <utility>

namespace multi
{
	/**
	 * @brief Tracks the state of an async task; does not block on destruction.
	 *
	 * @tparam T Result type produced by the task (void by default).
	 *
	 * Offers only a plain, non-stealing wait(). To wait while helping drain the
	 * pool, use multi::waitAll / waitAny / waitUntil.
	 *
	 * Read the result with get() (copies it, repeatable) or take() (moves it
	 * out, once; the only way for a move-only T).
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
		/// observe a task exception via get() or take().
		void wait() const;

		/// Non-blocking result fetch by copy. Repeatable: the result stays in
		/// the handle, so every call writes the same value.
		/// @param value Destination, copy-assigned only on success. U must be
		///        assignable from const T&; for a move-only T this is a
		///        compile error that names take().
		/// @return True (and writes @p value) if complete; false if the task
		///         has not completed or the handle is invalid (default-
		///         constructed, moved-from or taken), leaving @p value
		///         untouched. Rethrows a stored task exception.
		template <class U = T, std::enable_if_t<!std::is_void_v<U>, int> = 0>
		bool get(U* value);

		/// Non-blocking result fetch by move, for any non-void T, including
		/// move-only ones. Once it succeeds the handle is invalid, as if
		/// moved-from: valid() is false, complete() is true, and later get()
		/// and take() calls return false.
		/// @param out Destination, move-assigned only on success. Must point at
		///        a T exactly (T must be move-assignable and not const).
		/// @return True (and moves the result into @p out) if complete; false
		///         if the task has not completed or the handle is invalid,
		///         leaving @p out untouched. Rethrows a stored task exception
		///         and leaves the handle valid, so a later call rethrows it
		///         again. Not safe to call while another thread uses this
		///         Handle.
		template <class U = T, std::enable_if_t<std::is_same_v<U, T> && !std::is_void_v<U>, int> = 0>
		bool take(U* out);

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
