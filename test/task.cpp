/*
 *  Created by LuckyNeko on 17/05/2020.
 *  Copyright 2020 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <catch2/catch.hpp>
#include <multi/context.h>

multi::Task add(std::atomic<int>& a)
{
	std::this_thread::sleep_for(std::chrono::milliseconds(1));
	return [&](multi::Job&) { a++; };
}

TEST_CASE("multi::Task")
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

	// Task object tracking
	auto tsk = add(a);
	CHECK(a == 1);
	tsk.run();
	CHECK(a == 2);
	tsk = add(a);
	CHECK(a == 2);
	tsk = multi::Task();
	CHECK(a == 2);

	// Multi-thread Job
	std::atomic<int> b(0);
	context.async(add(b));
	CHECK(b == 1);

	// Subtask
	std::atomic<int> c(0);
	context.async([&](multi::Job& jb) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		jb.add(multi::Order::par, add(c));
	});
	CHECK(c == 1);

	// Stop threads
	context.stop();
	REQUIRE(context.threadCount() == 0);
}