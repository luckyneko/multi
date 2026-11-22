/*
 *  Created by LuckyNeko on 15/03/2020.
 *  Copyright 2020 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <algorithm>
#include <atomic>
#include <catch2/catch_all.hpp>
#include <chrono>
#include <cstddef>
#include <multi/multi.h>
#include <mutex>
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

	// waitAll: both handles are complete once it returns.
	auto h0 = multi::async([]()
						   { return 1; });
	auto h1 = multi::async([]()
						   { return 2; });
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
	auto b = multi::async([]()
						  { return 2; });
	b.wait();
	const std::size_t idx = multi::waitAny(a, b);
	CHECK(idx == 1);

	multi::waitAll(a, b); // drain the slow sibling before stop()

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

	// 2-arg form: whole-container, chunked like multi::Auto — mirrors for(auto& x : v).
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

TEST_CASE("multi: range over unsigned indices")
{
	multi::start(2);

	const std::size_t count = 10;

	std::atomic<std::size_t> sum(0);
	multi::range(std::size_t{0}, count, [&](std::size_t i)
				 { sum += i; });
	CHECK(sum == 45);

	// A const callable through both policies: every index once, and in order for Serial.
	std::atomic<std::size_t> perItem(0);
	const auto add = [&](std::size_t i)
	{ perItem += i; };
	multi::range(multi::PerItem, std::size_t{0}, count, std::size_t{1}, add);
	CHECK(perItem == 45);

	std::vector<std::size_t> seen;
	const auto record = [&](std::size_t i)
	{ seen.push_back(i); };
	multi::range(multi::Serial, std::size_t{0}, count, std::size_t{1}, record);
	CHECK(seen == std::vector<std::size_t>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9});

	multi::stop();
}

TEST_CASE("multi: ChunkPolicy resolves the four policies")
{
	using multi::ChunkPolicy;

	// Exact: clamped to [1, total]; 0 normalises to 1; total 0 stays 0.
	CHECK(ChunkPolicy(4).resolve(100, 8) == 4);
	CHECK(ChunkPolicy(0).resolve(100, 8) == 1);
	CHECK(ChunkPolicy(999).resolve(10, 8) == 10);
	CHECK(ChunkPolicy(4).resolve(0, 8) == 0);

	// PerItem: one task per item.
	CHECK(multi::PerItem.resolve(37, 8) == 37);

	// Serial: one task, whatever the size.
	CHECK(multi::Serial.resolve(37, 8) == 1);

	// Auto: clamp((workers+1)*CHUNK_FACTOR, 2, total).
	const std::size_t k = multi::CHUNK_FACTOR;
	CHECK(multi::Auto.resolve(1000, 7) == (7 + 1) * k); // below total
	CHECK(multi::Auto.resolve(10, 7) == 10);			// clamped to total
	CHECK(multi::Auto.resolve(100, 0) == std::max<std::size_t>(k, 2));
	CHECK(multi::Auto.resolve(0, 7) == 0); // empty
}

TEST_CASE("multi: each/range accept ChunkPolicy policies")
{
	multi::start(2);

	std::vector<int> v(100);
	for (int i = 0; i < 100; ++i)
		v[static_cast<std::size_t>(i)] = i;
	const int expected = 4950; // sum 0..99

	{
		std::atomic<int> s(0);
		multi::each(multi::Auto, v, [&](int x)
					{ s += x; });
		CHECK(s == expected);
	}
	{
		std::atomic<int> s(0);
		multi::each(multi::PerItem, v, [&](int x)
					{ s += x; });
		CHECK(s == expected);
	}
	{
		std::atomic<int> s(0);
		multi::each(8, v, [&](int x) // bare int -> ChunkPolicy
					{ s += x; });
		CHECK(s == expected);
	}
	{
		std::atomic<int> s(0);
		multi::range(multi::Auto, 0, 100, 1, [&](int i)
					 { s += i; });
		CHECK(s == expected);
	}
	{
		std::atomic<int> s(0);
		multi::range(multi::PerItem, 0, 100, 1, [&](int i)
					 { s += i; });
		CHECK(s == expected);
	}
	{
		std::vector<int> seen;
		multi::each(multi::Serial, v, [&](int x)
					{ seen.push_back(x); });
		CHECK(seen == v);
	}
	{
		std::vector<int> seen;
		multi::range(multi::Serial, 0, 100, 1, [&](int i)
					 { seen.push_back(i); });
		CHECK(seen == v);
	}

	multi::stop();
}

TEST_CASE("multi: policy-less each and range chunk like multi::Auto")
{
	multi::start(2);

	// A throw at item 0 skips the rest of chunk 0, whose length gives away the
	// chunk count the global context picked. One task per item would skip nothing.
	constexpr int total = 1000;
	const std::size_t chunks = multi::Auto.resolve(total, multi::threadCount());
	const int firstChunk = static_cast<int>(total / chunks + (total % chunks != 0 ? 1 : 0));
	std::vector<int> expected{0};
	for (int i = firstChunk; i < total; ++i)
		expected.push_back(i);

	std::vector<int> items(total);
	for (int i = 0; i < total; ++i)
		items[static_cast<std::size_t>(i)] = i;

	const auto visitedWhenItemZeroThrows = [](const auto& dispatch)
	{
		std::mutex mutex;
		std::vector<int> seen;
		CHECK_THROWS_AS(dispatch([&](int i)
								 {
				{
					const std::lock_guard<std::mutex> lock(mutex);
					seen.push_back(i);
				}
				if (i == 0)
					throw std::runtime_error("item 0"); }),
						std::runtime_error);
		std::sort(seen.begin(), seen.end());
		return seen;
	};

	CHECK(visitedWhenItemZeroThrows([&](const auto& f)
									{ multi::range(0, total, f); }) == expected);
	CHECK(visitedWhenItemZeroThrows([&](const auto& f)
									{ multi::range(0, total, 1, f); }) == expected);
	CHECK(visitedWhenItemZeroThrows([&](const auto& f)
									{ multi::each(items.begin(), items.end(), f); }) == expected);
	CHECK(visitedWhenItemZeroThrows([&](const auto& f)
									{ multi::each(items, f); }) == expected);

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
