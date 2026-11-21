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
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

// Handle no longer blocks on its own — it only exposes valid()/complete()/
// get(T*)/take(T*). Waiting (with steal participation) happens through the
// Context / free-function waitAll/waitAny/waitUntil primitives.

namespace
{
	// True when `declval<H&>().take(declval<P*>())` is a valid call.
	template <class H, class P, class = void>
	struct CanTake : std::false_type
	{
	};

	template <class H, class P>
	struct CanTake<H, P, std::void_t<decltype(std::declval<H&>().take(std::declval<P*>()))>> : std::true_type
	{
	};
} // namespace

TEST_CASE("Handle: default-constructed is invalid and complete")
{
	multi::Handle<> h;
	CHECK_FALSE(h.valid());
	// No assigned job ⇒ nothing to wait for ⇒ complete() is trivially true,
	// and wait() returns immediately.
	CHECK(h.complete());
	CHECK_NOTHROW(h.wait());
}

TEST_CASE("Handle: get(&out) on an invalid handle returns false")
{
	// An invalid handle reports complete() (nothing to wait for), but it has
	// no result: get(&out) must say so instead of reading a missing shared
	// state, and must leave *out alone.
	SECTION("default-constructed")
	{
		multi::Handle<int> h;
		int value = -1;
		bool got = true;
		CHECK_NOTHROW(got = h.get(&value));
		CHECK_FALSE(got);
		CHECK(value == -1);
	}

	SECTION("moved-from")
	{
		multi::Context context;
		context.start(2);

		auto h = context.async([]()
							   { return 5; });
		multi::Handle<int> moved(std::move(h));
		REQUIRE_FALSE(h.valid());

		int value = -1;
		bool got = true;
		CHECK_NOTHROW(got = h.get(&value));
		CHECK_FALSE(got);
		CHECK(value == -1);

		context.waitAll(moved);
		CHECK(moved.get(&value));
		CHECK(value == 5);

		context.stop();
	}
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
		ran.store(1, std::memory_order_release); });
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
	CHECK_FALSE(h.valid()); // moved-from source has been cleared.
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
			(*done)++; });
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
	auto h = context.async([]()
						   { return 42; });
	static_assert(std::is_same_v<decltype(h), multi::Handle<int>>,
				  "async should deduce Handle<int> from a lambda returning int");

	context.waitAll(h);
	int value = 0;
	CHECK(h.get(&value)); // true: complete
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
		return 7; });

	int value = -1;
	CHECK_FALSE(h.get(&value)); // not complete yet

	gate.store(true, std::memory_order_release);
	context.waitAll(h);
	CHECK(h.get(&value)); // now complete
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
			std::this_thread::yield(); });

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

	auto h = context.async([]() -> int
						   { throw std::runtime_error("typed"); });
	context.waitAll(h);

	// get() on a completed-but-failed task rethrows; backed by shared_future,
	// a second observation rethrows the same exception rather than entering a
	// "no state" path.
	int value = 0;
	CHECK_THROWS_AS(h.get(&value), std::runtime_error);
	CHECK_THROWS_AS(h.get(&value), std::runtime_error);

	context.stop();
}

TEST_CASE("Handle: take() is offered for every non-void result type")
{
	using UniqueInt = std::unique_ptr<int>;
	static_assert(CanTake<multi::Handle<UniqueInt>, UniqueInt>::value,
				  "take() must move a move-only result out");
	static_assert(CanTake<multi::Handle<int>, int>::value,
				  "take() must also work for copyable results");
	static_assert(CanTake<multi::Handle<std::string>, std::string>::value,
				  "take() must also work for copyable class results");
	static_assert(!CanTake<multi::Handle<>, void>::value,
				  "Handle<void> has no value to take; it keeps bool get()");
	static_assert(!CanTake<multi::Handle<int>, long>::value,
				  "take() moves into a T exactly, not a convertible type");
	static_assert(!CanTake<multi::Handle<int>, const int>::value,
				  "take() cannot write through a pointer to const");
	static_assert(std::is_same_v<decltype(std::declval<multi::Handle<UniqueInt>&>().take(std::declval<UniqueInt*>())), bool>,
				  "take() reports success as bool, like get()");
}

TEST_CASE("Handle: take() moves a move-only result out")
{
	multi::Context context;
	context.start(2);

	auto h = context.async([]()
						   { return std::make_unique<int>(1); });
	static_assert(std::is_same_v<decltype(h), multi::Handle<std::unique_ptr<int>>>,
				  "async should deduce Handle<unique_ptr<int>>");

	context.waitAll(h);
	std::unique_ptr<int> out;
	CHECK(h.take(&out));
	REQUIRE(out != nullptr);
	CHECK(*out == 1);

	context.stop();
}

TEST_CASE("Handle: take() moves rather than copies a copyable result")
{
	multi::Context context;
	context.start(2);

	// The task records its vector's buffer; a move hands that same buffer to
	// `out`, where a copy would allocate a new one.
	std::atomic<const int*> buffer(nullptr);
	auto h = context.async([&]()
						   {
		std::vector<int> v(1000, 3);
		buffer.store(v.data(), std::memory_order_release);
		return v; });

	context.waitAll(h);
	std::vector<int> out{1, 2};
	CHECK(h.take(&out));
	CHECK(out.size() == 1000);
	CHECK(out.data() == buffer.load(std::memory_order_acquire));

	context.stop();
}

TEST_CASE("Handle: take() returns false before the task completes")
{
	multi::Context context;
	context.start(1);

	// Only CHECKs before the gate opens: an aborting REQUIRE would leave the
	// worker spinning and stop() waiting on it.
	std::atomic<bool> gate(false);
	auto h = context.async([&]()
						   {
		while (!gate.load(std::memory_order_acquire))
			std::this_thread::yield();
		return std::make_unique<int>(7); });

	std::unique_ptr<int> out;
	CHECK_FALSE(h.take(&out)); // not complete yet
	CHECK(out == nullptr);	   // untouched
	CHECK(h.valid());		   // nothing was consumed

	gate.store(true, std::memory_order_release);
	context.waitAll(h);
	CHECK(h.take(&out));
	REQUIRE(out != nullptr);
	CHECK(*out == 7);

	context.stop();
}

TEST_CASE("Handle: take() rethrows and keeps the handle")
{
	multi::Context context;
	context.start(2);

	auto h = context.async([]() -> std::unique_ptr<int>
						   { throw std::runtime_error("take"); });
	context.waitAll(h);

	// A failed task has nothing to move out, so take() rethrows and leaves the
	// handle as it was: a second call rethrows the same exception.
	std::unique_ptr<int> out;
	CHECK_THROWS_AS(h.take(&out), std::runtime_error);
	CHECK(h.valid());
	CHECK_THROWS_AS(h.take(&out), std::runtime_error);
	CHECK(out == nullptr);

	context.stop();
}

TEST_CASE("Handle: a successful take() leaves the handle invalid")
{
	multi::Context context;
	context.start(2);

	auto h = context.async([]()
						   { return 42; });
	context.waitAll(h);

	int value = 0;
	CHECK(h.take(&value));
	CHECK(value == 42);

	// The result has been moved out, so the handle no longer refers to it:
	// it reads as invalid and complete, and get()/take() report false without
	// touching their destination.
	CHECK_FALSE(h.valid());
	CHECK(h.complete());
	CHECK_NOTHROW(h.wait());
	context.waitAll(h); // returns at once

	int again = -1;
	CHECK_FALSE(h.get(&again));
	CHECK_FALSE(h.take(&again));
	CHECK(again == -1);

	context.stop();
}

TEST_CASE("Handle: take() on an invalid handle returns false")
{
	SECTION("default-constructed")
	{
		multi::Handle<std::unique_ptr<int>> h;
		auto out = std::make_unique<int>(-1);
		bool took = true;
		CHECK_NOTHROW(took = h.take(&out));
		CHECK_FALSE(took);
		REQUIRE(out != nullptr);
		CHECK(*out == -1);
	}

	SECTION("moved-from")
	{
		multi::Context context;
		context.start(2);

		auto h = context.async([]()
							   { return std::make_unique<int>(3); });
		multi::Handle<std::unique_ptr<int>> moved(std::move(h));

		std::unique_ptr<int> out;
		bool took = true;
		CHECK_NOTHROW(took = h.take(&out));
		CHECK_FALSE(took);
		CHECK(out == nullptr);

		context.waitAll(moved);
		CHECK(moved.take(&out));
		CHECK((out != nullptr && *out == 3));

		context.stop();
	}
}
