/*
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

TEST_CASE("Handle: default-constructed is invalid and complete")
{
	multi::Handle h;
	CHECK_FALSE(h.valid());
	CHECK(h.complete());
	// wait() on a default-constructed Handle must be a no-op (idempotent).
	CHECK_NOTHROW(h.wait());
}

TEST_CASE("Handle: copy is deleted")
{
	// Static checks: copy ctor and copy assign should not exist.
	static_assert(!std::is_copy_constructible_v<multi::Handle>,
	              "Handle must be move-only");
	static_assert(!std::is_copy_assignable_v<multi::Handle>,
	              "Handle must not be copy-assignable");
	static_assert(std::is_move_constructible_v<multi::Handle>,
	              "Handle must be move-constructible");
	static_assert(std::is_move_assignable_v<multi::Handle>,
	              "Handle must be move-assignable");
}

TEST_CASE("Handle: moved-from is invalid")
{
	multi::Context context;
	context.start(2);

	auto h = context.async([]() {});
	REQUIRE(h.valid());

	multi::Handle moved(std::move(h));
	CHECK(moved.valid());
	CHECK_FALSE(h.valid());  // moved-from source has been cleared.
	moved.wait();

	context.stop();
}

TEST_CASE("Handle: detach drops wait but task still runs")
{
	multi::Context context;
	context.start(2);

	auto done = std::make_shared<std::atomic<int>>(0);
	{
		multi::Handle h = context.async([done]()
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
			(*done)++;
		});
		h.detach();
		CHECK(h.valid() == false);
		// Dropping a detached handle must not wait; the scope exits immediately.
	}

	// stop() joins workers, so the detached task will have run by then.
	context.stop();
	CHECK(*done == 1);
}

TEST_CASE("Handle: move-assign waits on old future")
{
	multi::Context context;
	context.start(2);

	std::atomic<int> firstRan(0);
	auto h = context.async([&]()
						   {
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
		firstRan = 1; });

	// Overwriting must not drop first task's wait: RAII-wait invariant.
	h = context.async([&]() {});
	CHECK(firstRan == 1);

	h.wait();
	context.stop();
}

TEST_CASE("Handle: move-assign swallows exception from old future")
{
	multi::Context context;
	context.start(2);

	// Exception from the dropped handle must not propagate out of operator=
	// (matching ~Handle). It is simply discarded.
	auto h = context.async([]()
						   { throw std::runtime_error("old"); });
	CHECK_NOTHROW(h = context.async([]() {}));
	h.wait();

	context.stop();
}
