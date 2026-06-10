/*
 *  Created by LuckyNeko on 12/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 *
 *  Template method definitions for multi::Handle<T>. Handle is intentionally
 *  Context-agnostic — no steal-participate logic lives here. Callers that
 *  want to help while waiting use the Context::waitX primitives
 *  (waitAll, waitAny, waitUntil).
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
	Handle<T>::~Handle()
	{
		try
		{
			wait();
		}
		catch (...)
		{
			// A destructor must not throw. Explicit wait()/get() surfaces
			// exceptions to callers that care.
		}
	}

	template <class T>
	void Handle<T>::wait()
	{
		if (!valid())
			return;
		// get() rethrows any stored exception. For non-void T it also fetches
		// the value as const T&; we discard the reference.
		if constexpr (std::is_void_v<T>)
			m_handle.get();
		else
			(void)m_handle.get();
	}

	template <class T>
	T Handle<T>::get()
	{
		// Goes straight through to the underlying shared_future, which both
		// waits and rethrows. On an invalid handle this throws future_error
		// (no_state) — matching std::future semantics.
		return m_handle.get();
	}

	template <class T>
	template <class Clock, class Duration>
	std::future_status Handle<T>::wait_until(const std::chrono::time_point<Clock, Duration>& tp)
	{
		// Match std::future semantics: a Handle without shared state is
		// "already done" — reporting ready avoids leaving callers in a
		// silent timeout loop.
		if (!valid())
			return std::future_status::ready;
		return m_handle.wait_until(tp);
	}

	template <class T>
	template <class Rep, class Period>
	std::future_status Handle<T>::wait_for(const std::chrono::duration<Rep, Period>& d)
	{
		return wait_until(std::chrono::steady_clock::now() + d);
	}

	template <class T>
	Handle<T>& Handle<T>::operator=(Handle&& a) noexcept(false)
	{
		if (this == &a)
			return *this;

		// Preserve the RAII-wait invariant: the current handle's task must
		// complete before we drop it, otherwise `h = async(a); h = async(b);`
		// would silently discard a's wait. Swallow exceptions to match
		// ~Handle; callers that need to observe failures must wait()/get()
		// explicitly first.
		try
		{
			wait();
		}
		catch (...)
		{
		}

		m_handle = std::move(a.m_handle);
		a.m_handle = std::shared_future<T>();
		return *this;
	}

} // namespace multi
