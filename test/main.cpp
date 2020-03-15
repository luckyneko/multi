#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>

#include "multi/details/queue.h"
#include "multi/details/threadpool.h"

#include <future>

TEST_CASE( "Test Queue", "[queue]" )
{
	multi::Queue<int> testQueue;
	int out = 0;
	CHECK(testQueue.pop(&out) == false);

	testQueue.push(7);
	CHECK(testQueue.empty() == false);
	CHECK(testQueue.pop(&out) == true);
	CHECK(out == 7);
	CHECK(testQueue.empty() == true);

	testQueue.emplace(9);
	CHECK(testQueue.empty() == false);
	CHECK(testQueue.pop(&out) == true);
	CHECK(out == 9);
	CHECK(testQueue.empty() == true);

	CHECK(testQueue.pop(&out) == false);
}

TEST_CASE( "Test ThreadPool", "[threadpool]" )
{
	multi::ThreadPool testPool;
	REQUIRE(testPool.threadCount() == 0);

	for(auto numThreads : {0, 1, 2, 4, 8, 16})
	{
		// Start
		testPool.start(numThreads);
		REQUIRE(testPool.threadCount() == numThreads);
		std::atomic<int> value(0);

		// FuncA
		auto triggerA = std::make_shared<std::promise<void>>();
		auto handleA = triggerA->get_future();
		testPool.queue([triggerA, &value]()
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
			value += 1;
			triggerA->set_value();
		});

		// FuncB
		auto triggerB = std::make_shared<std::promise<void>>();
		auto handleB = triggerB->get_future();
		testPool.queue([triggerB, &value]()
		{
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