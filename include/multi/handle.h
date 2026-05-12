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
#include <thread>
#include <type_traits>

namespace multi
{
	class Context;

	/*
	 * Handle<T>
	 * Tracks state of an async task. Will auto wait on destruction.
	 *
	 * T is the value type the task's functor returns (void by default).
	 * `multi::Handle<>` (or just `multi::Handle<void>`) is the void case
	 * returned by `multi::async([&]{ ... })`; `multi::Handle<int>` is what
	 * `multi::async([&]{ return 42; })` returns.
	 *
	 * wait()/get() rethrow any exception thrown by the task. The destructor
	 * swallows exceptions so dropping a Handle cannot call std::terminate.
	 *
	 * All blocking waits participate in work-stealing when a Context was
	 * supplied (async() always supplies one). For Handles with no associated
	 * Context the waits delegate to the underlying shared_future, which
	 * blocks the calling thread normally.
	 */
	template <class T = void>
	class Handle
	{
	public:
		Handle() = default;
		// Internally stores a shared_future so wait()/get() can rethrow the
		// stored exception idempotently without invalidating the handle.
		Handle(std::future<T>&& hdl, Context* ctx = nullptr);
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
		// (rarely) deferred — same set as std::future::wait_for.
		//
		// Unlike wait()/get(), the timed waits deliberately do NOT
		// participate in work-stealing: if they did, the caller could steal
		// the very task it's waiting on and be obliged to run it to
		// completion, blowing through the requested deadline. Callers who
		// want bounded latency use wait_for/wait_until; callers who want to
		// help out while waiting use wait()/get().
		template <class Rep, class Period>
		std::future_status wait_for(const std::chrono::duration<Rep, Period>& d);

		// Like wait_for but with an absolute deadline.
		template <class Clock, class Duration>
		std::future_status wait_until(const std::chrono::time_point<Clock, Duration>& tp);

		// Release the handle without waiting. The task still runs to completion
		// on its worker, but its result/exception becomes unobservable. Useful
		// for true fire-and-forget when you want to avoid ~Handle's auto-wait.
		void detach() { clear(); }

		Handle& operator=(Handle&& a) noexcept(false);

	private:
		// Drop our reference to the shared state without waiting. Used by
		// detach() and by move ops to vacate the source after stealing.
		void clear()
		{
			m_handle = std::shared_future<T>();
			m_context = nullptr;
		}

		// Block until ready, participating in steal if a Context is set.
		// Does NOT call get() — leaves the shared state intact so subsequent
		// wait()/get() calls observe the same value/exception.
		void waitInternal();

		std::shared_future<T> m_handle;
		Context* m_context = nullptr;
	};

} // namespace multi

// Method definitions need Context::tryRunSteal visible. context.h includes
// this header AFTER class Context is fully defined, then proceeds to include
// the .inl that supplies the templated wait/wait_for/wait_until bodies.
#include "multi/details/handle.inl"
