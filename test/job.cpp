/*
 *  Created by LuckyNeko on 17/05/2020.
 *  Copyright 2020 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <catch2/catch.hpp>
#include <multi/context.h>

multi::Job add(std::atomic<int>& a)
{
	std::this_thread::sleep_for(std::chrono::milliseconds(1));
	return multi::Job([&](multi::JobContext&) { printf("A\n"); a++; });
}

TEST_CASE("multi::Job")
{
	multi::Context context;
	REQUIRE(context.threadCount() == 0);

	// Start threads
	context.start(4);
	REQUIRE(context.threadCount() == 4);

	// Single thread Job
	std::atomic<int> a(0);
	add(a);
	CHECK(a == 1);

	// Multi-thread Job
	std::atomic<int> b(0);
	context.async(add(b));
	CHECK(b == 1);

	// Subtask
	std::atomic<int> c(0);
	context.async([&](multi::JobContext& jb) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		jb.add(add(c));
	});
	CHECK(c == 1);

	// Stop threads
	context.stop();
	REQUIRE(context.threadCount() == 0);
}