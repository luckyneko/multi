/*
 *  Created by LuckyNeko on 12/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <atomic>
#include <catch2/catch_all.hpp>
#include <chrono>
#include <memory>
#include <multi/context.h>
#include <multi/handle.h>
#include <stdexcept>
#include <thread>
#include <type_traits>

// Handle no longer blocks on its own — it only exposes valid()/complete()/
// get(T*). Waiting (with steal participation) happens through the Context /
// free-function waitAll/waitAny/waitUntil primitives.

TEST_CASE("Handle: default-constructed is invalid and complete")
{
	multi::Handle<> h;
	CHECK_FALSE(h.valid());
	// No assigned job ⇒ nothing to wait for ⇒ complete() is trivially true,
	// and wait() returns immediately.
	CHECK(h.complete());
	CHECK_NOTHROW(h.wait());
}

TEST_CASE("Handle: wait() blocks until the task completes")
{
	multi::Context context;
	context.start(2);

	// wait() is a plain, non-stealing block — once it returns the task has
	// run. (Use multi::waitAll to additionally help drain the pool.)
	std::atomic<int> ran(0);
	auto h = context.async([&]()
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
		ran.store(1, std::memory_order_release);
	});
	h.wait();
	CHECK(ran.load(std::memory_order_acquire) == 1);
	CHECK(h.complete());

	context.stop();
}

TEST_CASE("Handle: is move-only, not copyable or assignable")
{
	// Blocking and the RAII-waiting operator= are gone; Handle is a plain
	// move-only owner of the underlying shared state.
	static_assert(!std::is_copy_constructible_v<multi::Handle<>>,
	              "Handle must be move-only");
	static_assert(!std::is_copy_assignable_v<multi::Handle<>>,
	              "Handle must not be copy-assignable");
	static_assert(std::is_move_constructible_v<multi::Handle<>>,
	              "Handle must be move-constructible");
}

TEST_CASE("Handle: moved-from is invalid")
{
	multi::Context context;
	context.start(2);

	auto h = context.async([]() {});
	REQUIRE(h.valid());

	multi::Handle<> moved(std::move(h));
	CHECK(moved.valid());
	CHECK_FALSE(h.valid());  // moved-from source has been cleared.
	context.waitAll(moved);

	context.stop();
}

TEST_CASE("Handle: destructor does not block")
{
	multi::Context context;
	context.start(2);

	auto done = std::make_shared<std::atomic<int>>(0);
	{
		// Dropping this Handle must NOT wait — the scope exits immediately,
		// without blocking on the task.
		multi::Handle<> h = context.async([done]()
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
			(*done)++;
		});
		(void)h;
	}

	// stop() joins workers, so the orphaned task has run by the time it returns.
	context.stop();
	CHECK(*done == 1);
}

TEST_CASE("Handle: typed async returns the functor's value")
{
	multi::Context context;
	context.start(2);

	// CTAD: `h` is deduced to Handle<int> from the int-returning lambda.
	auto h = context.async([]() { return 42; });
	static_assert(std::is_same_v<decltype(h), multi::Handle<int>>,
	              "async should deduce Handle<int> from a lambda returning int");

	context.waitAll(h);
	int value = 0;
	CHECK(h.get(&value));  // true: complete
	CHECK(value == 42);

	context.stop();
}

TEST_CASE("Handle: get() returns false before the task completes")
{
	multi::Context context;
	context.start(1);

	// Hold the task open until we release the gate, so completion is
	// deterministic relative to the get() probe below.
	std::atomic<bool> gate(false);
	auto h = context.async([&]() -> int
	{
		while (!gate.load(std::memory_order_acquire))
			std::this_thread::yield();
		return 7;
	});

	int value = -1;
	CHECK_FALSE(h.get(&value));  // not complete yet

	gate.store(true, std::memory_order_release);
	context.waitAll(h);
	CHECK(h.get(&value));        // now complete
	CHECK(value == 7);

	context.stop();
}

TEST_CASE("Handle: void get() returns false before completion")
{
	multi::Context context;
	context.start(1);

	std::atomic<bool> gate(false);
	auto h = context.async([&]()
	{
		while (!gate.load(std::memory_order_acquire))
			std::this_thread::yield();
	});

	CHECK_FALSE(h.get());

	gate.store(true, std::memory_order_release);
	context.waitAll(h);
	CHECK(h.get());

	context.stop();
}

TEST_CASE("Handle: get() rethrows and is idempotent")
{
	multi::Context context;
	context.start(2);

	auto h = context.async([]() -> int { throw std::runtime_error("typed"); });
	context.waitAll(h);

	// get() on a completed-but-failed task rethrows; backed by shared_future,
	// a second observation rethrows the same exception rather than entering a
	// "no state" path.
	int value = 0;
	CHECK_THROWS_AS(h.get(&value), std::runtime_error);
	CHECK_THROWS_AS(h.get(&value), std::runtime_error);

	context.stop();
}
