/*
 *  Created by LuckyNeko on 15/03/2020.
 *  Copyright 2020 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <array>
#include <catch2/catch.hpp>
#include <multi/multi.h>

TEST_CASE("multi::JobContext::add()")
{
	multi::Context context;
	REQUIRE(context.threadCount() == 0);
	context.start(4);
	REQUIRE(context.threadCount() == 4);

	// Paralell check
	std::atomic<int> a(0);
	context.async([&](multi::JobContext& jb) {
		++a;
		jb.add(multi::Order::par, [&](multi::JobContext& jb) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
			a = a.load() * 3;
		});
		++a;
	});
	CHECK(a == 6);

	// Sequential check
	std::atomic<int> b(0);
	context.async([&](multi::JobContext& jb) {
		jb.add(
			multi::Order::seq, [&](multi::JobContext& jb) {
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
			++b; },
			[&](multi::JobContext& jb) {
				b = b * 2;
			},
			[&](multi::JobContext& jb) {
				++b;
			});
	});
	CHECK(b == 3);

	context.stop();
	REQUIRE(context.threadCount() == 0);
}

TEST_CASE("multi::JobContext::each()")
{
	multi::Context context;
	REQUIRE(context.threadCount() == 0);
	context.start(4);
	REQUIRE(context.threadCount() == 4);

	// Parallel check
	std::atomic<int> a(0);
	std::array<int, 4> b = {1, 2, 3, 4};
	context.async([&](multi::JobContext& jb) {
		jb.each(multi::Order::par, b.begin(), b.end(), [&](multi::JobContext& jb, int i) {
			std::this_thread::sleep_for(std::chrono::milliseconds(i));
			a += i;
		});
	});
	CHECK(a == 10);

	// Seq check
	int c = 1;
	std::array<int, 4> d = {1, 2, 3, 4};
	context.async([&](multi::JobContext& jb) {
		jb.each(multi::Order::seq, d.begin(), d.end(), [&](multi::JobContext& jb, int i) {
			c *= i;
		});
	});
	CHECK(c == 24);

	context.stop();
	REQUIRE(context.threadCount() == 0);
}

TEST_CASE("multi::JobContext::range()")
{
	multi::Context context;
	REQUIRE(context.threadCount() == 0);
	context.start(4);
	REQUIRE(context.threadCount() == 4);

	// Parallel check
	std::atomic<int> a(0);
	std::array<int, 4> b = {1, 2, 3, 4};
	context.async([&](multi::JobContext& jb) {
		jb.range(multi::Order::par, 0, int(b.size()), 1, [&](multi::JobContext& jb, int i) {
			std::this_thread::sleep_for(std::chrono::milliseconds(i));
			a += b[i];
		});
	});
	CHECK(a == 10);

	// Seq check
	int c = 1;
	std::array<int, 4> d = {1, 2, 3, 4};
	context.async([&](multi::JobContext& jb) {
		jb.range(multi::Order::seq, 0, int(d.size()), 1, [&](multi::JobContext& jb, int i) {
			c *= d[i];
		});
	});
	CHECK(c == 24);

	context.stop();
	REQUIRE(context.threadCount() == 0);
}

TEST_CASE("multi::JobContext::until()")
{
	multi::Context context;
	REQUIRE(context.threadCount() == 0);
	context.start(4);
	REQUIRE(context.threadCount() == 4);

	// Seq check
	int c = 1;
	int i = 0;
	std::array<int, 4> d = {1, 2, 3, 4};
	context.async([&](multi::JobContext& jb) {
		jb.until([&]() { return c > 4 && i < d.size(); }, [&](multi::JobContext& jb) { c *= d[i]; ++i; });
	});
	CHECK(c == 6);

	context.stop();
	REQUIRE(context.threadCount() == 0);
}