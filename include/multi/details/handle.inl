/*
 *  Created by LuckyNeko on 12/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

namespace multi
{
	template <class T>
	Handle<T>::Handle(std::future<T>&& hdl)
		: m_handle(std::move(hdl))
	{
	}

	template <class T>
	Handle<T>::Handle(Handle&& a) noexcept
		: m_handle(std::move(a.m_handle))
	{
		a.m_handle = std::shared_future<T>();
	}

	template <class T>
	bool Handle<T>::complete() const
	{
		return !valid() || m_handle.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
	}

	template <class T>
	bool Handle<T>::valid() const
	{
		return m_handle.valid();
	}

	template <class T>
	void Handle<T>::wait() const
	{
		if (m_handle.valid())
			m_handle.wait();
	}

	template <class T>
	template <class U, std::enable_if_t<!std::is_void_v<U>, int>>
	bool Handle<T>::get(U* value)
	{
		// A static_assert rather than SFINAE: removing get() from overload
		// resolution would leave only a generic "no matching overload" error,
		// with nothing to fall through to, while this names the fix.
		static_assert(std::is_assignable_v<U&, const T&>,
					  "Handle<T>::get(&out) copy-assigns the result into *out, which this type does not "
					  "allow; for a move-only T, use take(&out) to move the result out instead");

		// complete() is true for an invalid handle, and get() on an invalid
		// shared_future is undefined, so check valid() first.
		if (!valid() || !complete())
			return false;
		*value = m_handle.get();
		return true;
	}

	template <class T>
	template <class U, std::enable_if_t<std::is_same_v<U, T> && !std::is_void_v<U>, int>>
	bool Handle<T>::take(U* out)
	{
		static_assert(!std::is_const_v<U> && std::is_move_assignable_v<U>,
					  "Handle<T>::take(&out) move-assigns the result into *out, so T must be move-assignable "
					  "and not const");

		if (!valid() || !complete())
			return false;

		// Moving out of the shared state is well defined and race-free because
		// this Handle is its only reader:
		// - Handle is the sole owner of m_handle. It cannot be copied, nothing
		//   exposes m_handle, and Context::async moves in the only std::future
		//   for the state (promise::get_future succeeds once). The promise in
		//   AsyncJob writes the value and never reads it back.
		// - complete() saw the state ready, which synchronises with the
		//   promise's set_value, so the value is fully written and no other
		//   thread touches it from here on.
		// - shared_future::get() returns const T&, but the object it refers to
		//   was built by set_value as a non-const T (T is not const, per the
		//   static_assert above), so casting away const to move from it is
		//   allowed.
		// get() rethrows a stored exception before anything is moved, leaving
		// the handle valid. After the move, m_handle is reset so no later call
		// can read the moved-from value; the shared state's reference count
		// orders its destruction after the move, wherever it happens.
		U& stored = const_cast<U&>(m_handle.get());
		*out = std::move(stored);
		m_handle = std::shared_future<T>();
		return true;
	}

	template <class T>
	template <class U, std::enable_if_t<std::is_same_v<U, T> && std::is_void_v<U>, int>>
	bool Handle<T>::get()
	{
		if (!valid() || !complete())
			return false;
		m_handle.get();
		return true;
	}

} // namespace multi
