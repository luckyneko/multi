/*
 *  Created by LuckyNeko on 17/05/2020.
 *  Copyright 2020 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <catch2/catch.hpp>
#include <multi/context.h>

#include <algorithm>
#include <array>
#include <map>

static const std::array<size_t, 4> THREAD_COUNT_SET = {0, 1, 2, 4};
static const std::array<size_t, 4> TASK_COUNT_SET = {0, 1, 2, 4};
static const size_t DATA_SET_MAX_SIZE = 32;

void testAsync(multi::Context& context)
{
	// Test handle wait
	std::atomic<int> a(0);
	auto hdl = context.async([&]()
							 {
								 std::this_thread::sleep_for(std::chrono::milliseconds(1));
								 ++a; });
	if (context.threadCount() > 0)
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
					  ++a; });
	CHECK(a == 2);
}

void testParallel(multi::Context& context)
{
	// Test parallel
	std::atomic<int> a(2);
	context.parallel([&]()
					 { ++a; },
					 [&]()
					 {
						 std::this_thread::sleep_for(std::chrono::milliseconds(1));
						 a = a * 2;
					 });
	CHECK(a == 6);
}

void testEach(multi::Context& context)
{
	// Each of vector
	std::vector<int> inVec;
	int inVecSum = 0;
	for (size_t i = 0; i < DATA_SET_MAX_SIZE; ++i)
	{
		// Test mutable Each
		std::vector<int> tmpVec = inVec;
		context.each(tmpVec.begin(), tmpVec.end(), [](int& i)
					 { i += 1; });

		// Test const Each
		std::atomic<int> a(0);
		context.each(tmpVec.begin(), tmpVec.end(), [&](int i)
					 { a += i; });
		CHECK(a == inVecSum + i);

		inVec.push_back(i);
		inVecSum += i;
	}

	// Each of Map
	std::map<int, int> inMap;
	int inMapSum = 0;
	for (size_t i = 0; i < DATA_SET_MAX_SIZE; ++i)
	{
		// Test mutable each map Iterator
		std::map<int, int> tmpMap = inMap;
		context.each(tmpMap.begin(), tmpMap.end(), [](std::pair<const int, int>& p)
					 { p.second += 1; });

		// Test each map Iterator
		std::atomic<int> a(0);
		context.each(tmpMap.begin(), tmpMap.end(), [&](const std::pair<const int, int>& p)
					 { a += p.second; });
		CHECK(a == inMapSum + i);

		inMap[i] = i;
		inMapSum += i;
	}
}

void testEachTaskCount(multi::Context& context, size_t taskCount)
{
	// Each of vector
	std::vector<int> inVec;
	int inVecSum = 0;
	for (size_t i = 0; i < DATA_SET_MAX_SIZE; ++i)
	{
		// Test mutable Each
		std::vector<int> tmpVec = inVec;
		context.each(taskCount, tmpVec.begin(), tmpVec.end(), [](int& i)
					 { i += 1; });

		// Test const Each
		std::atomic<int> a(0);
		context.each(taskCount, tmpVec.begin(), tmpVec.end(), [&](int i)
					 { a += i; });
		CHECK(a == inVecSum + i);

		inVec.push_back(i);
		inVecSum += i;
	}

	// Each of Map
	std::map<int, int> inMap;
	int inMapSum = 0;
	for (size_t i = 0; i < DATA_SET_MAX_SIZE; ++i)
	{
		// Test mutable each map Iterator
		std::map<int, int> tmpMap = inMap;
		context.each(taskCount, tmpMap.begin(), tmpMap.end(), [](std::pair<const int, int>& p)
					 { p.second += 1; });

		// Test each map Iterator
		std::atomic<int> a(0);
		context.each(taskCount, tmpMap.begin(), tmpMap.end(), [&](const std::pair<const int, int>& p)
					 { a += p.second; });
		CHECK(a == inMapSum + i);

		inMap[i] = i;
		inMapSum += i;
	}
}

void testRange(multi::Context& context)
{
	// Test int
	for (int begin = 0; begin < DATA_SET_MAX_SIZE; ++begin)
	{
		for (int end = 0; end < DATA_SET_MAX_SIZE; ++end)
		{
			for (int step = 1; step < DATA_SET_MAX_SIZE; ++step)
			{
				std::atomic<int> a(0);
				context.range(begin, end, step, [&](int i)
							  { a += 1; });

				const int totalRange = std::max(0, (end - begin));
				const int expectedResult = (totalRange / step) + ((totalRange % step > 0) ? 1 : 0);
				CHECK(a == expectedResult);
			}
		}
	}

	// Test float
	const float scale = 0.1f;
	for (float begin = 0; begin < scale * DATA_SET_MAX_SIZE; begin += scale)
	{
		for (float end = 0; end < scale * DATA_SET_MAX_SIZE; end += scale)
		{
			for (float step = scale; step < scale * DATA_SET_MAX_SIZE; step += scale)
			{
				std::atomic<int> a(0);
				context.range(begin, end, step, [&](float i)
							  { a += 1; });

				int expectedResult = 0;
				for (float i = begin; i < end; i += step)
					++expectedResult;

				CHECK(a == expectedResult);
			}
		}
	}
}

void testRangeTaskCount(multi::Context& context, size_t taskCount)
{
	for (int begin = 0; begin < DATA_SET_MAX_SIZE; ++begin)
	{
		for (int end = 0; end < DATA_SET_MAX_SIZE; ++end)
		{
			for (int step = 1; step < DATA_SET_MAX_SIZE; ++step)
			{
				std::atomic<int> a(0);
				context.range(taskCount, begin, end, step, [&](int i)
							  { a += 1; });

				const int totalRange = std::max(0, (end - begin));
				const int expectedResult = (totalRange / step) + ((totalRange % step > 0) ? 1 : 0);
				CHECK(a == expectedResult);
			}
		}
	}
}

TEST_CASE("multi::Context")
{
	multi::Context context;
	REQUIRE(context.threadCount() == 0);

	for (auto threadCount : THREAD_COUNT_SET)
	{
		// Start threads
		context.start(threadCount);
		REQUIRE(context.threadCount() == threadCount);

		testAsync(context);
		testParallel(context);
		testEach(context);
		testRange(context);

		for (auto taskCount : TASK_COUNT_SET)
		{
			testEachTaskCount(context, taskCount);
			testRangeTaskCount(context, taskCount);
		}

		// Stop threads
		context.stop();
		REQUIRE(context.threadCount() == 0);
	}
}