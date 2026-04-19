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
#include <atomic>
#include <map>
#include <stdexcept>

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

TEST_CASE("range step-less overload defaults step to 1")
{
	multi::Context context;
	context.start(2);

	std::atomic<int> sum(0);
	context.range(0, 10, [&](int i)
				  { sum += i; });
	CHECK(sum == 45);

	context.stop();
}

TEST_CASE("Handle::detach drops the wait without cancelling the task")
{
	multi::Context context;
	context.start(2);

	auto done = std::make_shared<std::atomic<int>>(0);
	{
		multi::Handle h = context.async([done]()
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
			(*done)++;
		});
		h.detach();
		CHECK(h.valid() == false);
		// Dropping a detached handle must not wait; the scope exits immediately.
	}

	// stop() joins workers, so the detached task will have run by then.
	context.stop();
	CHECK(*done == 1);
}

TEST_CASE("Handle move-assign waits on old future before overwriting")
{
	multi::Context context;
	context.start(2);

	std::atomic<int> firstRan(0);
	auto h = context.async([&]()
						   {
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
		firstRan = 1; });

	// Overwriting must not drop first task's wait: RAII-wait invariant.
	h = context.async([&]() {});
	CHECK(firstRan == 1);

	h.wait();
	context.stop();
}

TEST_CASE("Handle move-assign swallows exception from old future")
{
	multi::Context context;
	context.start(2);

	// Exception from the dropped handle must not propagate out of operator=
	// (matching ~Handle). It is simply discarded.
	auto h = context.async([]()
						   { throw std::runtime_error("old"); });
	CHECK_NOTHROW(h = context.async([]() {}));
	h.wait();

	context.stop();
}

TEST_CASE("async handle::wait steals to avoid self-deadlock with 1 worker")
{
	// With threadCount==1 the sole worker is the one running the outer task.
	// The inner async drops its Handle; without stealing in Handle::wait
	// the outer task would block forever waiting for a worker that is itself.
	multi::Context context;
	context.start(1);

	std::atomic<int> counter(0);
	auto outer = context.async([&]()
							   {
		context.async([&]()
		{
			counter++;
		});
		counter++; });
	outer.wait();

	CHECK(counter == 2);
	context.stop();
}

TEST_CASE("async rethrows task exception and pool survives")
{
	multi::Context context;
	context.start(2);

	auto thrower = context.async([]()
								 { throw std::runtime_error("boom"); });
	CHECK_THROWS_AS(thrower.wait(), std::runtime_error);

	// Pool still accepts new work after a throwing task.
	std::atomic<int> x(0);
	context.async([&]()
				  { x = 42; })
		.wait();
	CHECK(x == 42);

	context.stop();
}

TEST_CASE("parallel accepts lvalue functors without extra top-level copies")
{
	multi::Context context;
	context.start(2);

	// Counts copies at the parallel() entry boundary. Before the forwarding
	// fix, by-value TASKS... parameters forced an eager copy of each lvalue
	// task; now arguments bind to forwarding references, so lvalues flow
	// through as references until std::function ingests them.
	struct CopyCounter
	{
		static std::atomic<int>& copies()
		{
			static std::atomic<int> n{0};
			return n;
		}
		std::atomic<int>* counter;
		CopyCounter(std::atomic<int>* c)
			: counter(c)
		{
		}
		CopyCounter(const CopyCounter& o)
			: counter(o.counter)
		{
			copies()++;
		}
		CopyCounter(CopyCounter&&) = default;
		CopyCounter& operator=(const CopyCounter&) = default;
		CopyCounter& operator=(CopyCounter&&) = default;
		void operator()() const { (*counter)++; }
	};

	std::atomic<int> counter(0);
	CopyCounter a(&counter), b(&counter);
	int before = CopyCounter::copies().load();
	context.parallel(a, b);
	int after = CopyCounter::copies().load();

	CHECK(counter == 2);
	// std::function's type-erased storage copies once per task; anything
	// more means parallel() itself re-copied the functor at its boundary.
	CHECK((after - before) <= 2);

	context.stop();
}

TEST_CASE("parallel rethrows first exception and still runs siblings")
{
	multi::Context context;
	context.start(2);

	std::atomic<int> otherRan(0);
	CHECK_THROWS_AS(
		context.parallel(
			multi::Task([]()
						{ throw std::runtime_error("boom"); }),
			multi::Task([&]()
						{ otherRan++; })),
		std::runtime_error);
	CHECK(otherRan == 1);

	context.stop();
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