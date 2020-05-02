/*
 *  Created by LuckyNeko on 15/03/2020.
 *  Copyright 2020 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>

#include "multi/multi.h"
#include <array>

TEST_CASE("multi::Queue")
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

TEST_CASE("multi::JobNode")
{
	int value = 0; 
	auto func = [&value](){ ++value; };
	multi::JobNode* parent = new multi::JobNode(nullptr,
		func, 
		new multi::JobNode(nullptr,
			func, 
			new multi::JobNode(nullptr, func)
		)
	);
	multi::JobNode* childA = new multi::JobNode(parent, func);
	multi::JobNode* childB = new multi::JobNode(parent, func);

	auto next = parent->run();
	CHECK(value == 1);
	REQUIRE(next == nullptr);

	next = childA->run();
	CHECK(value == 2);
	REQUIRE(next == parent);

	next = next->run();
	CHECK(value == 2);
	REQUIRE(next == nullptr);

	next = childB->run();
	CHECK(value == 3);
	REQUIRE(next == parent);

	next = next->run();
	CHECK(value == 3);
	REQUIRE(next != nullptr);

	next = next->run();
	CHECK(value == 4);
	REQUIRE(next != nullptr);

	next = next->run();
	CHECK(value == 5);
	CHECK(next == nullptr);
}

TEST_CASE("multi::context()")
{
	CHECK(multi::context() != nullptr);
	auto defaultContext = multi::context();

	multi::Context localContext;
	multi::context() = &localContext;
	CHECK(multi::context() == &localContext);

	multi::context() = defaultContext;
}

TEST_CASE("multi::start()")
{
	REQUIRE(multi::threadCount() == 0);
	multi::start(4);
	REQUIRE(multi::threadCount() == 4);
	multi::stop();
	REQUIRE(multi::threadCount() == 0);
}

TEST_CASE("multi::async()")
{
	REQUIRE(multi::threadCount() == 0);
	multi::start(4);
	REQUIRE(multi::threadCount() == 4);

	std::atomic<int> a(0);
	auto hdl = multi::async([&]()
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		++a;
	});
	CHECK(a == 0);
	hdl.wait();
	CHECK(a == 1);
	
	multi::async([&]()
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		++a;
	});
	CHECK(a == 2);

	multi::stop();
	REQUIRE(multi::threadCount() == 0);
}

TEST_CASE("multi::parallel()")
{
	REQUIRE(multi::threadCount() == 0);
	multi::start(4);
	REQUIRE(multi::threadCount() == 4);

	std::atomic<int> a(0);
	multi::async([&]()
	{
		++a;
		multi::parallel([&]()
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
			a = a.load() * 3;
		});
		++a;
	});
	CHECK(a == 6);

	multi::stop();
	REQUIRE(multi::threadCount() == 0);
}

TEST_CASE("multi::parallelFor()")
{
	REQUIRE(multi::threadCount() == 0);
	multi::start(4);
	REQUIRE(multi::threadCount() == 4);

	std::atomic<int> a(0);
	std::array<int, 4> b = {1, 2, 3, 4};
	multi::async([&]()
	{
		multi::parallelFor(b.begin(), b.end(), [&](int i)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(i));
			a += i;
		});
	});
	CHECK(a == 10);

	multi::stop();
	REQUIRE(multi::threadCount() == 0);	
}

TEST_CASE("multi::serial()")
{
	REQUIRE(multi::threadCount() == 0);
	multi::start(4);
	REQUIRE(multi::threadCount() == 4);

	std::atomic<int> a(0);
	multi::async([&]()
	{
		multi::serial([&]()
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
			++a;
		},
		[&]()
		{
			a = a * 2;
		},
		[&]()
		{
			++a;
		});
	});
	CHECK(a == 3);

	multi::stop();
	REQUIRE(multi::threadCount() == 0);	
}

TEST_CASE("multi::serialFor()")
{
	REQUIRE(multi::threadCount() == 0);
	multi::start(4);
	REQUIRE(multi::threadCount() == 4);

	int a = 1;
	std::array<int, 4> b = {1, 2, 3, 4};
	multi::async([&]()
	{
		multi::serialFor(b.begin(), b.end(), [&](int i)
		{
			a *= i;
		});
	});
	CHECK(a == 24);

	multi::stop();
	REQUIRE(multi::threadCount() == 0);
}