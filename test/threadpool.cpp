/*
 *  Created by LuckyNeko on 17/05/2020.
 *  Copyright 2020 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <catch2/catch.hpp>
#include <future>
#include <multi/details/threadpool.h>

TEST_CASE("multi::ThreadPool")
{
	multi::ThreadPool testPool;
	REQUIRE(testPool.threadCount() == 0);

	for (auto numThreads : {0, 1, 2, 4, 8, 16})
	{
		// Start
		testPool.start(numThreads);
		REQUIRE(testPool.threadCount() == numThreads);
		std::atomic<int> value(0);

		// FuncA
		auto triggerA = std::make_shared<std::promise<void>>();
		auto handleA = triggerA->get_future();
		testPool.queue([triggerA, &value]() {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
			value += 1;
			triggerA->set_value();
		});

		// FuncB
		auto triggerB = std::make_shared<std::promise<void>>();
		auto handleB = triggerB->get_future();
		testPool.queue([triggerB, &value]() {
			value += 2;
			triggerB->set_value();
		});

		handleA.wait();
		handleB.wait();
		CHECK(value == 3);

		// Stop
		testPool.stop();
		REQUIRE(testPool.threadCount() == 0);
	}
}