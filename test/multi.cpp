/*
 *  Created by LuckyNeko on 15/03/2020.
 *  Copyright 2020 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <atomic>
#include <catch2/catch_all.hpp>
#include <chrono>
#include <multi/multi.h>
#include <stdexcept>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// Smoke tests for the multi.h free-function API. These forwarders delegate
// to the globally-installed Context; here we verify each one routes through
// correctly without re-testing the underlying dispatch shapes (Context tests
// cover those exhaustively).
// ---------------------------------------------------------------------------

TEST_CASE("multi: version header agrees with itself")
{
	// Round-trip sanity: macros + inline constexpr accessors must report
	// the same numbers, the string must format as M.N.P, and the encoded
	// MULTI_VERSION must equal the formula advertised in the header so
	// consumers can rely on `#if MULTI_VERSION >= 10203` comparisons.
	CHECK(multi::version_major == MULTI_VERSION_MAJOR);
	CHECK(multi::version_minor == MULTI_VERSION_MINOR);
	CHECK(multi::version_patch == MULTI_VERSION_PATCH);

	std::string expected = std::to_string(MULTI_VERSION_MAJOR) + "." +
						   std::to_string(MULTI_VERSION_MINOR) + "." +
						   std::to_string(MULTI_VERSION_PATCH);
	CHECK(std::string(multi::version_string) == expected);

	const int encoded = MULTI_VERSION_MAJOR * 10000 + MULTI_VERSION_MINOR * 100 + MULTI_VERSION_PATCH;
	CHECK(MULTI_VERSION == encoded);
}

TEST_CASE("multi: waitAll / waitAny free functions route through global context")
{
	multi::start(2);

	// waitAll: both handles are complete once it returns.
	auto h0 = multi::async([]() { return 1; });
	auto h1 = multi::async([]() { return 2; });
	multi::waitAll(h0, h1);
	CHECK(h0.complete());
	CHECK(h1.complete());

	// waitAny: a slow handle vs an already-complete one. Pre-complete `b`
	// with Handle::wait() (a plain, NON-stealing block) so the caller can't
	// pull the 40ms `a` inline — then waitAny must report b's index.
	auto a = multi::async([]()
						  {
		std::this_thread::sleep_for(std::chrono::milliseconds(40));
		return 1; });
	auto b = multi::async([]() { return 2; });
	b.wait();
	const std::size_t idx = multi::waitAny(a, b);
	CHECK(idx == 1);

	multi::waitAll(a, b);  // drain the slow sibling before stop()

	multi::stop();
}

TEST_CASE("multi: waitUntil free function blocks on a custom predicate")
{
	multi::start(2);

	std::atomic<bool> ready(false);
	auto h = multi::async([&]()
						  {
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
		ready.store(true, std::memory_order_release); });
	multi::waitUntil([&]()
					 { return ready.load(std::memory_order_acquire); });
	CHECK(ready.load());
	multi::waitAll(h);

	multi::stop();
}

TEST_CASE("multi: variadic async free function routes through global context")
{
	multi::start(2);

	auto handles = multi::async(
		[]()
		{ return 1; },
		[]()
		{ return 2; });
	multi::waitAll(handles);
	auto& [h1, h2] = handles;
	int v1 = 0, v2 = 0;
	CHECK(h1.get(&v1));
	CHECK(h2.get(&v2));
	CHECK(v1 == 1);
	CHECK(v2 == 2);

	multi::stop();
}

TEST_CASE("multi: start/stop track threadCount")
{
	REQUIRE(multi::threadCount() == 0);
	multi::start(4);
	REQUIRE(multi::threadCount() == 4);
	multi::stop();
	REQUIRE(multi::threadCount() == 0);
}

TEST_CASE("multi: start() defaults to hardware_concurrency - 1")
{
	// No argument (or a negative count) auto-sizes the pool to one fewer than
	// the hardware thread count, reserving a core for the calling thread.
	const unsigned hw = std::thread::hardware_concurrency();

	REQUIRE(multi::threadCount() == 0);
	multi::start();
	if (hw > 1)
		REQUIRE(multi::threadCount() == static_cast<size_t>(hw - 1));
	multi::stop();
	REQUIRE(multi::threadCount() == 0);
}

TEST_CASE("multi: async handle observed via waitAll")
{
	REQUIRE(multi::threadCount() == 0);
	multi::start(4);

	std::atomic<int> a(0);
	auto hdl = multi::async([&]()
							{
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		++a; });
	CHECK(a == 0);
	multi::waitAll(hdl);
	CHECK(a == 1);

	multi::stop();
	REQUIRE(multi::threadCount() == 0);
}

TEST_CASE("multi: parallel runs siblings")
{
	multi::start(2);
	std::atomic<int> a(0);
	multi::parallel([&]()
					{ a += 1; }, [&]()
					{ a += 2; }, [&]()
					{ a += 4; });
	CHECK(a == 7);
	multi::stop();
}

TEST_CASE("multi: each over vector")
{
	multi::start(2);

	std::vector<int> v(10);
	for (int i = 0; i < 10; ++i)
		v[static_cast<std::size_t>(i)] = i;

	multi::each(v.begin(), v.end(), [](int& x)
				{ x *= 2; });

	std::atomic<int> sum(0);
	multi::each(v.begin(), v.end(), [&](int x)
				{ sum += x; });
	CHECK(sum == 90); // 2*(0+1+...+9)

	multi::stop();
}

TEST_CASE("multi: each over a container (range-based overload)")
{
	multi::start(2);

	std::vector<int> v(10);
	for (int i = 0; i < 10; ++i)
		v[static_cast<std::size_t>(i)] = i;

	// 2-arg form: whole-container, one task per item — mirrors for(auto& x : v).
	multi::each(v, [](int& x)
				{ x *= 2; });

	std::atomic<int> sum(0);
	multi::each(v, [&](int x)
				{ sum += x; });
	CHECK(sum == 90); // 2*(0+1+...+9)

	// 3-arg chunked form: taskCount disambiguates from each(begin, end, func).
	std::atomic<int> chunkSum(0);
	multi::each(size_t(4), v, [&](int x)
				{ chunkSum += x; });
	CHECK(chunkSum == 90);

	// A const container binds through the forwarding reference too.
	const std::vector<int>& cv = v;
	std::atomic<int> constSum(0);
	multi::each(cv, [&](int x)
				{ constSum += x; });
	CHECK(constSum == 90);

	multi::stop();
}

TEST_CASE("multi: range over integers")
{
	multi::start(2);

	std::atomic<int> sum(0);
	multi::range(0, 10, [&](int i)
				 { sum += i; });
	CHECK(sum == 45);

	std::atomic<int> stepSum(0);
	multi::range(0, 10, 2, [&](int i)
				 { stepSum += i; });
	CHECK(stepSum == 20); // 0+2+4+6+8

	std::atomic<int> chunkedSum(0);
	multi::range(std::size_t(3), 0, 10, 1, [&](int i)
				 { chunkedSum += i; });
	CHECK(chunkedSum == 45);

	multi::stop();
}

TEST_CASE("multi: async rethrows task exception")
{
	multi::start(2);

	// A failing task surfaces its exception through get() once complete;
	// the value-returning form is what carries the throw (void handles have
	// no get()).
	auto h = multi::async([]() -> int
						  { throw std::runtime_error("boom"); });
	multi::waitAll(h);
	int v = 0;
	CHECK_THROWS_AS(h.get(&v), std::runtime_error);

	multi::stop();
}
