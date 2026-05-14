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
	 * Tracks state of an async task. Will auto wait on destruction.
	 *
	 * T is the value type the task's functor returns (void by default).
	 * `multi::Handle<>` (or `multi::Handle<void>`) is the void case returned
	 * by `multi::async([&]{ ... })`; `multi::Handle<int>` is returned by
	 * `multi::async([&]{ return 42; })`.
	 *
	 * wait()/get() rethrow any exception thrown by the task. The destructor
	 * swallows exceptions so dropping a Handle cannot call std::terminate.
	 *
	 * Handle is intentionally Context-agnostic: it holds no back-pointer to
	 * the pool that produced it. wait()/get()/wait_for/wait_until all block
	 * the calling thread plainly — there is no caller participation in
	 * work-stealing. If you want the calling thread to help drain the pool
	 * while waiting, use `Context::waitAll(h)` (or another `Context::waitX`
	 * primitive — see context.h) instead.
	 */
	template <class T = void>
	class Handle
	{
	public:
		Handle() = default;
		// Internally stores a shared_future so wait()/get() can rethrow the
		// stored exception idempotently without invalidating the handle.
		explicit Handle(std::future<T>&& hdl);
		Handle(const Handle&) = delete;
		Handle(Handle&& a) noexcept;
		~Handle();

		// Check if job is complete
		bool complete() const { return !valid() || m_handle.wait_for(std::chrono::seconds(0)) == std::future_status::ready; }

		// Check if handle has assigned job
		bool valid() const { return m_handle.valid(); }

		// Block until the task completes. Rethrows any exception raised by
		// the task. Idempotent: repeated calls re-throw the same exception.
		void wait();

		// Block until the task completes, then return its result. For T=void
		// this matches wait() (returns void). For non-void T, returns the
		// value the task's functor produced. Idempotent on shared state —
		// safe to call from multiple threads / multiple times.
		T get();

		// Block up to `d` waiting for completion. Returns ready, timeout, or
		// (rarely) deferred — same set as std::future::wait_for. Like wait(),
		// this blocks plainly — it does not participate in work-stealing.
		template <class Rep, class Period>
		std::future_status wait_for(const std::chrono::duration<Rep, Period>& d);

		// Like wait_for but with an absolute deadline.
		template <class Clock, class Duration>
		std::future_status wait_until(const std::chrono::time_point<Clock, Duration>& tp);

		// Release the handle without waiting. The task still runs to completion
		// on its worker, but its result/exception becomes unobservable. Useful
		// for true fire-and-forget when you want to avoid ~Handle's auto-wait.
		void detach() { m_handle = std::shared_future<T>(); }

		Handle& operator=(Handle&& a) noexcept(false);

	private:
		std::shared_future<T> m_handle;
	};

} // namespace multi

#include "multi/details/handle.inl"
