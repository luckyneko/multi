/*
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 *
 *  Template method definitions for multi::Handle<T>. Included from
 *  multi/handle.h, which is itself included by multi/context.h *after*
 *  class Context has been fully defined — so Context::tryRunSteal is
 *  visible here.
 */

namespace multi
{
	template <class T>
	Handle<T>::Handle(std::future<T>&& hdl, Context* ctx)
		: m_handle(std::move(hdl))
		, m_context(ctx)
	{
	}

	template <class T>
	Handle<T>::Handle(Handle&& a) noexcept
	{
		m_handle = std::move(a.m_handle);
		m_context = a.m_context;
		a.clear();
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
	void Handle<T>::waitInternal()
	{
		if (!valid())
			return;

		if (m_context != nullptr)
		{
			while (m_handle.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
			{
				if (!m_context->tryRunSteal())
					std::this_thread::yield();
			}
		}
		else
		{
			m_handle.wait();
		}
	}

	template <class T>
	void Handle<T>::wait()
	{
		if (!valid())
			return;
		waitInternal();
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
		// Mirror std::future::get behaviour on an invalid handle by going
		// straight through to the underlying shared_future (which throws
		// future_error(no_state)). For void T this also rethrows any stored
		// exception via wait()/get().
		waitInternal();
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

		// Pure timed wait — do NOT participate in stealing. If we did, the
		// caller could steal the very task we're waiting on and be obliged
		// to run it to completion, blowing through the requested deadline
		// (deadlines aren't preemption points). Callers who want to help
		// while waiting use wait()/get(); callers who want a bounded wait
		// use this overload.
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
		// would silently discard a's wait. Swallow exceptions to match ~Handle;
		// callers that need to observe failures must wait()/get() explicitly
		// first.
		try
		{
			wait();
		}
		catch (...)
		{
		}

		m_handle = std::move(a.m_handle);
		m_context = a.m_context;
		a.clear();
		return *this;
	}

} // namespace multi
