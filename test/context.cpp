/*
 *  Created by LuckyNeko on 17/05/2020.
 *  Copyright 2020 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <catch2/catch.hpp>
#include <multi/context.h>

TEST_CASE("multi::Context")
{
	multi::Context context;
	REQUIRE(context.threadCount() == 0);

	// Start threads
	context.start(4);
	REQUIRE(context.threadCount() == 4);

	// Test handle wait
	std::atomic<int> a(0);
	auto hdl = context.async([&](multi::JobContext& jb) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		++a;
	});
	CHECK(a == 0);
	hdl.wait();
	CHECK(a == 1);

	// Test drop handle
	context.async([&](multi::JobContext& jb) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		++a;
	});
	CHECK(a == 2);

	// Stop threads
	context.stop();
	REQUIRE(context.threadCount() == 0);
}