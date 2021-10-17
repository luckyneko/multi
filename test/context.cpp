/*
 *  Created by LuckyNeko on 17/05/2020.
 *  Copyright 2020 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <catch2/catch.hpp>
#include <multi/context.h>

#include <array>

TEST_CASE("multi::Context")
{
	multi::Context context;
	REQUIRE(context.threadCount() == 0);

	std::array<size_t, 4> threadCountSet = {0, 1, 2, 4};
	for (auto threadCount : threadCountSet)
	{
		// Start threads
		context.start(threadCount);
		REQUIRE(context.threadCount() == threadCount);

		// Test handle wait
		std::atomic<int> a(0);
		auto hdl = context.async([&]()
								 {
									 std::this_thread::sleep_for(std::chrono::milliseconds(1));
									 ++a;
								 });
		if (threadCount > 0)
			CHECK(a == 0);
		hdl.wait();
		CHECK(a == 1);

		// Reset handle
		CHECK(hdl.valid() == true);
		CHECK(hdl.complete() == true);
		hdl = multi::Handle();
		CHECK(hdl.valid() == false);
		CHECK(hdl.complete() == true);

		// Test drop handle
		a = 1;
		context.async([&]()
					  {
						  std::this_thread::sleep_for(std::chrono::milliseconds(1));
						  ++a;
					  });
		CHECK(a == 2);

		// Test parallel
		a = 2;
		context.parallel([&]()
						 { ++a; },
						 [&]()
						 {
							 std::this_thread::sleep_for(std::chrono::milliseconds(1));
							 a = a * 2;
						 });
		CHECK(a == 6);

		// Test Each
		a = 0;
		std::vector<int> intList{0, 1, 2, 3, 4};
		context.each(intList.begin(), intList.end(), [&](int i)
					 { a += i; });
		CHECK(a == 10);

		// Test Range
		a = 0;
		context.range(0, 4, 1, [&](int i)
					  { a += i; });
		CHECK(a == 10);

		// Stop threads
		context.stop();
		REQUIRE(context.threadCount() == 0);
	}
}