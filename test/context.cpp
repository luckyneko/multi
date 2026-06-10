/*
 *  Created by LuckyNeko on 17/05/2020.
 *  Copyright 2020 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <atomic>
#include <catch2/catch_all.hpp>
#include <chrono>
#include <map>
#include <multi/context.h>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
	constexpr std::size_t DATA_SET_MAX_SIZE = 32;
}

// ---------------------------------------------------------------------------
// Context API behaviour — one TEST_CASE per dispatch shape. Thread counts are
// parameterised via GENERATE so each row is independently runnable from the
// test explorer and counts == 0 (single-threaded inline mode) is exercised
// alongside multi-worker counts.
// ---------------------------------------------------------------------------

TEST_CASE("Context: async wait observes side effects")
{
	auto threadCount = GENERATE(std::size_t(0), std::size_t(1), std::size_t(2), std::size_t(4));
	multi::Context context;
	context.start(threadCount);

	std::atomic<int> a(0);
	auto hdl = context.async([&]()
							 {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		++a; });
	if (context.threadCount() > 0)
		CHECK(a == 0);
	hdl.wait();
	CHECK(a == 1);

	CHECK(hdl.valid() == true);
	CHECK(hdl.complete() == true);
	hdl = multi::Handle<>();
	CHECK(hdl.valid() == false);
	CHECK(hdl.complete() == true);

	// Dropped handle still completes the task (RAII-wait or inline).
	a = 1;
	context.async([&]()
				  {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		++a; });
	CHECK(a == 2);

	context.stop();
}

TEST_CASE("Context: waitAll avoids self-deadlock with 1 worker")
{
	// With threadCount==1 the sole worker is the one running the outer task.
	// The inner async submits a task that only the sole worker could run —
	// but that worker is itself the one waiting on the inner Handle. Plain
	// Handle::wait() blocks (and would deadlock here); waitAll lets the
	// waiting thread drain pending work first.
	multi::Context context;
	context.start(1);

	std::atomic<int> counter(0);
	auto outer = context.async([&]() {
		auto inner = context.async([&]() { counter++; });
		context.waitAll(inner);  // explicit participation
		counter++;
	});
	// Main thread isn't a worker — outer.wait() blocks plainly, which is
	// fine: the sole worker will run `outer` once it unwinds inner.
	outer.wait();

	CHECK(counter == 2);
	context.stop();
}

TEST_CASE("Context: async rethrows task exception and pool survives")
{
	multi::Context context;
	context.start(2);

	auto thrower = context.async([]()
								 { throw std::runtime_error("boom"); });
	CHECK_THROWS_AS(thrower.wait(), std::runtime_error);

	// Pool still accepts new work after a throwing task.
	std::atomic<int> x(0);
	context.async([&]() { x = 42; }).wait();
	CHECK(x == 42);

	context.stop();
}

TEST_CASE("Context: parallel runs siblings")
{
	auto threadCount = GENERATE(std::size_t(0), std::size_t(1), std::size_t(2), std::size_t(4));
	multi::Context context;
	context.start(threadCount);

	std::atomic<int> a(2);
	context.parallel([&]() { ++a; },
					 [&]()
					 {
						 std::this_thread::sleep_for(std::chrono::milliseconds(1));
						 a = a * 2;
					 });
	CHECK(a == 6);

	context.stop();
}

TEST_CASE("Context: parallel accepts lvalue functors without extra top-level copies")
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

TEST_CASE("Context: parallel rethrows first exception and runs siblings")
{
	multi::Context context;
	context.start(2);

	std::atomic<int> otherRan(0);
	CHECK_THROWS_AS(
		context.parallel(
			multi::details::Task([]()
						{ throw std::runtime_error("boom"); }),
			multi::details::Task([&]()
						{ otherRan++; })),
		std::runtime_error);
	CHECK(otherRan == 1);

	context.stop();
}

TEST_CASE("Context: each over vector")
{
	auto threadCount = GENERATE(std::size_t(0), std::size_t(1), std::size_t(2), std::size_t(4));
	multi::Context context;
	context.start(threadCount);

	std::vector<int> inVec;
	int inVecSum = 0;
	for (std::size_t i = 0; i < DATA_SET_MAX_SIZE; ++i)
	{
		std::vector<int> tmpVec = inVec;
		context.each(tmpVec.begin(), tmpVec.end(), [](int& v) { v += 1; });

		std::atomic<int> a(0);
		context.each(tmpVec.begin(), tmpVec.end(), [&](int v) { a += v; });
		CHECK(a == inVecSum + static_cast<int>(i));

		inVec.push_back(static_cast<int>(i));
		inVecSum += static_cast<int>(i);
	}

	context.stop();
}

TEST_CASE("Context: each over map")
{
	auto threadCount = GENERATE(std::size_t(0), std::size_t(1), std::size_t(2), std::size_t(4));
	multi::Context context;
	context.start(threadCount);

	std::map<int, int> inMap;
	int inMapSum = 0;
	for (std::size_t i = 0; i < DATA_SET_MAX_SIZE; ++i)
	{
		std::map<int, int> tmpMap = inMap;
		context.each(tmpMap.begin(), tmpMap.end(),
					 [](std::pair<const int, int>& p) { p.second += 1; });

		std::atomic<int> a(0);
		context.each(tmpMap.begin(), tmpMap.end(),
					 [&](const std::pair<const int, int>& p) { a += p.second; });
		CHECK(a == inMapSum + static_cast<int>(i));

		inMap[static_cast<int>(i)] = static_cast<int>(i);
		inMapSum += static_cast<int>(i);
	}

	context.stop();
}

TEST_CASE("Context: each with taskCount over vector")
{
	auto threadCount = GENERATE(std::size_t(0), std::size_t(1), std::size_t(2), std::size_t(4));
	auto taskCount = GENERATE(std::size_t(0), std::size_t(1), std::size_t(2), std::size_t(4));
	multi::Context context;
	context.start(threadCount);

	std::vector<int> inVec;
	int inVecSum = 0;
	for (std::size_t i = 0; i < DATA_SET_MAX_SIZE; ++i)
	{
		std::vector<int> tmpVec = inVec;
		context.each(taskCount, tmpVec.begin(), tmpVec.end(), [](int& v) { v += 1; });

		std::atomic<int> a(0);
		context.each(taskCount, tmpVec.begin(), tmpVec.end(), [&](int v) { a += v; });
		CHECK(a == inVecSum + static_cast<int>(i));

		inVec.push_back(static_cast<int>(i));
		inVecSum += static_cast<int>(i);
	}

	context.stop();
}

TEST_CASE("Context: each with taskCount over map")
{
	auto threadCount = GENERATE(std::size_t(0), std::size_t(1), std::size_t(2), std::size_t(4));
	auto taskCount = GENERATE(std::size_t(0), std::size_t(1), std::size_t(2), std::size_t(4));
	multi::Context context;
	context.start(threadCount);

	std::map<int, int> inMap;
	int inMapSum = 0;
	for (std::size_t i = 0; i < DATA_SET_MAX_SIZE; ++i)
	{
		std::map<int, int> tmpMap = inMap;
		context.each(taskCount, tmpMap.begin(), tmpMap.end(),
					 [](std::pair<const int, int>& p) { p.second += 1; });

		std::atomic<int> a(0);
		context.each(taskCount, tmpMap.begin(), tmpMap.end(),
					 [&](const std::pair<const int, int>& p) { a += p.second; });
		CHECK(a == inMapSum + static_cast<int>(i));

		inMap[static_cast<int>(i)] = static_cast<int>(i);
		inMapSum += static_cast<int>(i);
	}

	context.stop();
}

TEST_CASE("Context: range int")
{
	auto threadCount = GENERATE(std::size_t(0), std::size_t(1), std::size_t(2), std::size_t(4));
	multi::Context context;
	context.start(threadCount);

	for (int begin = 0; begin < static_cast<int>(DATA_SET_MAX_SIZE); ++begin)
	{
		for (int end = 0; end < static_cast<int>(DATA_SET_MAX_SIZE); ++end)
		{
			for (int step = 1; step < static_cast<int>(DATA_SET_MAX_SIZE); ++step)
			{
				std::atomic<int> a(0);
				context.range(begin, end, step, [&](int /*i*/) { a += 1; });

				const int totalRange = std::max(0, (end - begin));
				const int expected = (totalRange / step) + ((totalRange % step > 0) ? 1 : 0);
				CHECK(a == expected);
			}
		}
	}

	context.stop();
}

TEST_CASE("Context: range float")
{
	auto threadCount = GENERATE(std::size_t(0), std::size_t(1), std::size_t(2), std::size_t(4));
	multi::Context context;
	context.start(threadCount);

	const float scale = 0.1f;
	for (float begin = 0; begin < scale * DATA_SET_MAX_SIZE; begin += scale)
	{
		for (float end = 0; end < scale * DATA_SET_MAX_SIZE; end += scale)
		{
			for (float step = scale; step < scale * DATA_SET_MAX_SIZE; step += scale)
			{
				std::atomic<int> a(0);
				context.range(begin, end, step, [&](float /*i*/) { a += 1; });

				int expected = 0;
				for (float i = begin; i < end; i += step)
					++expected;

				CHECK(a == expected);
			}
		}
	}

	context.stop();
}

TEST_CASE("Context: range with taskCount")
{
	auto threadCount = GENERATE(std::size_t(0), std::size_t(1), std::size_t(2), std::size_t(4));
	auto taskCount = GENERATE(std::size_t(0), std::size_t(1), std::size_t(2), std::size_t(4));
	multi::Context context;
	context.start(threadCount);

	for (int begin = 0; begin < static_cast<int>(DATA_SET_MAX_SIZE); ++begin)
	{
		for (int end = 0; end < static_cast<int>(DATA_SET_MAX_SIZE); ++end)
		{
			for (int step = 1; step < static_cast<int>(DATA_SET_MAX_SIZE); ++step)
			{
				std::atomic<int> a(0);
				context.range(taskCount, begin, end, step, [&](int /*i*/) { a += 1; });

				const int totalRange = std::max(0, (end - begin));
				const int expected = (totalRange / step) + ((totalRange % step > 0) ? 1 : 0);
				CHECK(a == expected);
			}
		}
	}

	context.stop();
}

TEST_CASE("Context: range step-less overload defaults step to 1")
{
	multi::Context context;
	context.start(2);

	std::atomic<int> sum(0);
	context.range(0, 10, [&](int i) { sum += i; });
	CHECK(sum == 45);

	context.stop();
}

// ---------------------------------------------------------------------------
// Edge cases — empty inputs and degenerate parameters must be no-ops, not
// crashes. These exercise the count==0 fast paths inside each Job subclass.
// ---------------------------------------------------------------------------

TEST_CASE("Context: each over empty vector is a no-op")
{
	multi::Context context;
	context.start(2);

	std::vector<int> empty;
	std::atomic<int> calls(0);
	context.each(empty.begin(), empty.end(), [&](int) { calls++; });
	CHECK(calls == 0);

	// taskCount overload too.
	context.each(std::size_t(4), empty.begin(), empty.end(), [&](int) { calls++; });
	CHECK(calls == 0);

	context.stop();
}

TEST_CASE("Context: parallel with zero args is a no-op")
{
	multi::Context context;
	context.start(2);

	CHECK_NOTHROW(context.parallel());

	context.stop();
}

TEST_CASE("Context: range with begin >= end is a no-op")
{
	multi::Context context;
	context.start(2);

	std::atomic<int> calls(0);
	context.range(5, 5, 1, [&](int) { calls++; });
	context.range(5, 3, 1, [&](int) { calls++; });
	context.range(std::size_t(4), 5, 5, 1, [&](int) { calls++; });
	context.range(std::size_t(4), 5, 3, 1, [&](int) { calls++; });
	CHECK(calls == 0);

	context.stop();
}

