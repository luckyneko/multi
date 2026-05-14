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
#include <functional>
#include <multi/multi.h>
#include <numeric>
#include <stdexcept>
#include <string>
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

	auto h0 = multi::async([]() {
		std::this_thread::sleep_for(std::chrono::milliseconds(40));
		return 1;
	});
	auto h1 = multi::async([]() { return 2; });

	// Pre-complete h1 via plain wait() so waitAny sees a deterministic
	// "first complete" — otherwise the caller-side stealing could pull
	// h0 onto this thread and finish it inline.
	h1.wait();
	const std::size_t idx = multi::waitAny(h0, h1);
	CHECK(idx == 1);

	// waitAll blocks until both complete.
	multi::waitAll(h0, h1);
	CHECK(h0.complete());
	CHECK(h1.complete());

	multi::stop();
}

TEST_CASE("multi: waitUntil free function blocks on a custom predicate")
{
	multi::start(2);

	std::atomic<bool> ready(false);
	auto h = multi::async([&]() {
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
		ready.store(true, std::memory_order_release);
	});
	multi::waitUntil([&]() { return ready.load(std::memory_order_acquire); });
	CHECK(ready.load());
	h.wait();

	multi::stop();
}

TEST_CASE("multi: parallelAsync free function routes through global context")
{
	multi::start(2);

	auto handles = multi::parallelAsync(
		[]() { return 1; },
		[]() { return 2; });
	auto& [h1, h2] = handles;
	CHECK(h1.get() == 1);
	CHECK(h2.get() == 2);

	multi::stop();
}

TEST_CASE("multi: reduce / transformReduce free functions route through global context")
{
	multi::start(4);

	std::vector<int> v(1000);
	std::iota(v.begin(), v.end(), 1);  // 1..1000
	CHECK(multi::reduce(v.begin(), v.end(), 0, std::plus<>{}) == 500500);
	CHECK(multi::transformReduce(v.begin(), v.end(), 0, std::plus<>{},
	                              [](int x) { return x * x; })
	      == 333833500);

	multi::stop();
}

TEST_CASE("multi: context() accessor is swappable")
{
	CHECK(multi::context() != nullptr);
	auto defaultContext = multi::context();

	multi::Context localContext;
	multi::context() = &localContext;
	CHECK(multi::context() == &localContext);

	multi::context() = defaultContext;
}

TEST_CASE("multi: start/stop track threadCount")
{
	REQUIRE(multi::threadCount() == 0);
	multi::start(4);
	REQUIRE(multi::threadCount() == 4);
	multi::stop();
	REQUIRE(multi::threadCount() == 0);
}

TEST_CASE("multi: async returns handle that waits")
{
	REQUIRE(multi::threadCount() == 0);
	multi::start(4);

	std::atomic<int> a(0);
	auto hdl = multi::async([&]()
							{
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		++a; });
	CHECK(a == 0);
	hdl.wait();
	CHECK(a == 1);

	// Dropped Handle still completes its task before scope exit.
	multi::async([&]()
				 {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		++a; });
	CHECK(a == 2);

	multi::stop();
	REQUIRE(multi::threadCount() == 0);
}

TEST_CASE("multi: parallel runs siblings")
{
	multi::start(2);
	std::atomic<int> a(0);
	multi::parallel([&]() { a += 1; }, [&]() { a += 2; }, [&]() { a += 4; });
	CHECK(a == 7);
	multi::stop();
}

TEST_CASE("multi: each over vector")
{
	multi::start(2);

	std::vector<int> v(10);
	for (int i = 0; i < 10; ++i)
		v[static_cast<std::size_t>(i)] = i;

	multi::each(v.begin(), v.end(), [](int& x) { x *= 2; });

	std::atomic<int> sum(0);
	multi::each(v.begin(), v.end(), [&](int x) { sum += x; });
	CHECK(sum == 90);  // 2*(0+1+...+9)

	multi::stop();
}

TEST_CASE("multi: range over integers")
{
	multi::start(2);

	std::atomic<int> sum(0);
	multi::range(0, 10, [&](int i) { sum += i; });
	CHECK(sum == 45);

	std::atomic<int> stepSum(0);
	multi::range(0, 10, 2, [&](int i) { stepSum += i; });
	CHECK(stepSum == 20);  // 0+2+4+6+8

	std::atomic<int> chunkedSum(0);
	multi::range(std::size_t(3), 0, 10, 1, [&](int i) { chunkedSum += i; });
	CHECK(chunkedSum == 45);

	multi::stop();
}

TEST_CASE("multi: async rethrows task exception")
{
	multi::start(2);

	auto h = multi::async([]() { throw std::runtime_error("boom"); });
	CHECK_THROWS_AS(h.wait(), std::runtime_error);

	multi::stop();
}
