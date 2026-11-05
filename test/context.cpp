/*
 *  Created by LuckyNeko on 17/05/2020.
 *  Copyright 2020 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <algorithm>
#include <atomic>
#include <catch2/catch_all.hpp>
#include <chrono>
#include <limits>
#include <list>
#include <map>
#include <multi/context.h>
#include <mutex>
#include <numeric>
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
	context.waitAll(hdl);
	CHECK(a == 1);

	// A completed handle still holds its (now-ready) state.
	CHECK(hdl.valid() == true);
	CHECK(hdl.complete() == true);

	context.stop();
}

TEST_CASE("Context: waitAll avoids self-deadlock with 1 worker")
{
	// With threadCount==1 the sole worker is the one running the outer task.
	// The inner async submits a task that only the sole worker could run —
	// but that worker is itself the one waiting on the inner Handle. waitAll
	// lets the waiting thread drain pending work first instead of deadlocking.
	multi::Context context;
	context.start(1);

	std::atomic<int> counter(0);
	auto outer = context.async([&]() {
		auto inner = context.async([&]() { counter++; });
		context.waitAll(inner);  // explicit participation
		counter++;
	});
	// Main thread isn't a worker; waitAll lets it participate in stealing so
	// the sole worker isn't the only thread that can make progress.
	context.waitAll(outer);

	CHECK(counter == 2);
	context.stop();
}

TEST_CASE("Context: async rethrows task exception and pool survives")
{
	multi::Context context;
	context.start(2);

	auto thrower = context.async([]() -> int
								 { throw std::runtime_error("boom"); });
	context.waitAll(thrower);
	int v = 0;
	CHECK_THROWS_AS(thrower.get(&v), std::runtime_error);

	// Pool still accepts new work after a throwing task.
	std::atomic<int> x(0);
	context.waitAll(context.async([&]() { x = 42; }));
	CHECK(x == 42);

	context.stop();
}

TEST_CASE("Context: parallel runs siblings")
{
	auto threadCount = GENERATE(std::size_t(0), std::size_t(1), std::size_t(2), std::size_t(4));
	multi::Context context;
	context.start(threadCount);

	// Each sibling contributes a distinct addend so the result is independent
	// of the order in which the two tasks run. Don't reintroduce an
	// order-dependent read-modify-write (e.g. a = a * 2) gated on a sleep --
	// that races the sibling's increment and fails intermittently under CI load.
	std::atomic<int> a(2);
	context.parallel([&]() { a += 1; },
					 [&]()
					 {
						 std::this_thread::sleep_for(std::chrono::milliseconds(1));
						 a += 3;
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

TEST_CASE("Context: each/range ChunkPolicy policies on a local pool")
{
	// Auto resolves against THIS context's worker count, not the global one.
	auto threadCount = GENERATE(std::size_t(0), std::size_t(1), std::size_t(2), std::size_t(4));
	auto policy = GENERATE(multi::Auto, multi::PerItem);
	multi::Context context;
	context.start(threadCount);

	std::vector<int> v(50);
	int expected = 0;
	for (int i = 0; i < 50; ++i)
	{
		v[static_cast<std::size_t>(i)] = i;
		expected += i;
	}

	std::atomic<int> eachSum(0);
	context.each(policy, v, [&](int x) { eachSum += x; });
	CHECK(eachSum == expected);

	std::atomic<int> rangeSum(0);
	context.range(policy, 0, 50, 1, [&](int i) { rangeSum += i; });
	CHECK(rangeSum == expected);

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

TEST_CASE("Context: range over one float chunk visits exactly its count")
{
	// One chunk is where stepping by addition drifts furthest from the
	// multiplied bound: 0 to 24*0.1f by 0.1f is 25 indices, not 26.
	multi::Context context;
	context.start(2);

	const float step = 0.1f;
	for (int n = 1; n < 256; ++n)
	{
		const float end = static_cast<float>(n) * step;

		int expected = 0;
		for (float i = 0.f; i < end; i += step)
			++expected;

		int visited = 0;
		context.range(std::size_t(1), 0.f, end, step, [&](float /*i*/) { ++visited; });
		CHECK(visited == expected);
	}

	context.stop();
}

TEST_CASE("Context: chunked float range hands out the same values as per-item")
{
	auto threadCount = GENERATE(std::size_t(0), std::size_t(2), std::size_t(4));
	multi::Context context;
	context.start(threadCount);

	const auto valuesOf = [&](multi::ChunkPolicy policy)
	{
		std::mutex mutex;
		std::vector<float> values;
		context.range(policy, 0.f, 25.6f, 0.1f, [&](float v)
					  {
			const std::lock_guard<std::mutex> lock(mutex);
			values.push_back(v); });
		std::sort(values.begin(), values.end());
		return values;
	};

	const std::vector<float> perItem = valuesOf(multi::PerItem);
	CHECK(valuesOf(1) == perItem);
	CHECK(valuesOf(7) == perItem);
	CHECK(valuesOf(multi::Auto) == perItem);

	context.stop();
}

TEST_CASE("Context: range near the top of a signed type stays in bounds")
{
	// Stepping past the last index would overflow here, which is undefined.
	multi::Context context;
	context.start(2);

	const int top = std::numeric_limits<int>::max();
	std::vector<int> seen;
	context.range(std::size_t(1), top - 5, top, 3, [&](int i) { seen.push_back(i); });
	CHECK(seen == std::vector<int>{top - 5, top - 2});

	context.stop();
}

// ---------------------------------------------------------------------------
// multi::Serial — every item in order, on the calling thread. These run on a
// started pool, where order is a claim and not a given. The recordings are
// plain vectors on purpose: a stray worker would race on them, and TSAN reports
// that even if the order happens to come out right.
// ---------------------------------------------------------------------------

TEST_CASE("Context: range Serial visits every index in order on the caller")
{
	auto threadCount = GENERATE(std::size_t(0), std::size_t(1), std::size_t(2), std::size_t(4));
	auto step = GENERATE(1, 2, 3);
	multi::Context context;
	context.start(threadCount);

	const std::thread::id caller = std::this_thread::get_id();
	std::vector<int> seen;
	bool onCaller = true;
	context.range(multi::Serial, 0, 1000, step, [&](int i)
				  {
		seen.push_back(i);
		onCaller = onCaller && std::this_thread::get_id() == caller; });

	std::vector<int> expected;
	for (int i = 0; i < 1000; i += step)
		expected.push_back(i);
	CHECK(seen == expected);
	CHECK(onCaller);

	context.stop();
}

TEST_CASE("Context: each Serial visits every item in order on the caller")
{
	auto threadCount = GENERATE(std::size_t(0), std::size_t(1), std::size_t(2), std::size_t(4));
	multi::Context context;
	context.start(threadCount);

	const auto visitOrder = [&](const auto& items)
	{
		const std::thread::id caller = std::this_thread::get_id();
		std::vector<int> seen;
		bool onCaller = true;
		context.each(multi::Serial, items, [&](int x)
					 {
			seen.push_back(x);
			onCaller = onCaller && std::this_thread::get_id() == caller; });
		CHECK(onCaller);
		return seen;
	};

	std::vector<int> expected(1000);
	std::iota(expected.begin(), expected.end(), 0);
	// EachJob stores these two differently: a random-access iterator, and a
	// pointer table built by walking the list.
	CHECK(visitOrder(expected) == expected);
	CHECK(visitOrder(std::list<int>(expected.begin(), expected.end())) == expected);

	context.stop();
}

TEST_CASE("Context: Serial ends the walk at a throw and rethrows it")
{
	auto threadCount = GENERATE(std::size_t(0), std::size_t(1), std::size_t(2), std::size_t(4));
	multi::Context context;
	context.start(threadCount);

	const std::vector<int> stoppedAtSeven{0, 1, 2, 3, 4, 5, 6, 7};
	const auto visit = [](std::vector<int>& seen, int i)
	{
		seen.push_back(i);
		if (i == 7)
			throw std::runtime_error("stop");
	};

	std::vector<int> seenRange;
	CHECK_THROWS_AS(context.range(multi::Serial, 0, 100, 1, [&](int i) { visit(seenRange, i); }), std::runtime_error);
	CHECK(seenRange == stoppedAtSeven);

	std::vector<int> items(100);
	std::iota(items.begin(), items.end(), 0);
	std::vector<int> seenEach;
	CHECK_THROWS_AS(context.each(multi::Serial, items, [&](int i) { visit(seenEach, i); }), std::runtime_error);
	CHECK(seenEach == stoppedAtSeven);

	context.stop();
}

TEST_CASE("Context: Serial runs no other pool work on the caller")
{
	// One worker held busy, and a task queued behind it that only stealing can reach.
	multi::Context context;
	context.start(1);

	std::atomic<bool> started(false), release(false), queuedRan(false);
	auto hold = context.async([&]()
							  {
		started = true;
		while (!release)
			std::this_thread::yield(); });
	while (!started)
		std::this_thread::yield();
	auto queued = context.async([&]()
								{ queuedRan = true; });

	int visited = 0;
	context.range(multi::Serial, 0, 1000, 1, [&](int) { ++visited; });
	CHECK(visited == 1000);
	CHECK_FALSE(queuedRan);

	// The setup can tell the difference: a dispatch that waits by stealing takes it.
	context.range(multi::PerItem, 0, 8, 1, [](int) {});
	CHECK(queuedRan);

	release = true;
	hold.wait();
	queued.wait();
	context.stop();
}

TEST_CASE("Context: Serial called from a pool task stays on that thread")
{
	multi::Context context;
	context.start(2);

	auto hdl = context.async([&]()
							 {
		const std::thread::id self = std::this_thread::get_id();
		std::vector<int> seen;
		bool onSelf = true;
		context.range(multi::Serial, 0, 100, 1, [&](int i)
					  {
			seen.push_back(i);
			onSelf = onSelf && std::this_thread::get_id() == self; });
		return onSelf && seen.size() == 100 && std::is_sorted(seen.begin(), seen.end()); });
	hdl.wait();

	bool inOrderOnSelf = false;
	REQUIRE(hdl.get(&inOrderOnSelf));
	CHECK(inOrderOnSelf);

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
	context.each(multi::Serial, empty, [&](int) { calls++; });
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
	context.range(multi::Serial, 5, 5, 1, [&](int) { calls++; });
	context.range(multi::Serial, 5, 3, 1, [&](int) { calls++; });
	CHECK(calls == 0);

	context.stop();
}

TEST_CASE("Context: range with a non-positive step is a no-op")
{
	multi::Context context;
	context.start(2);

	std::atomic<int> calls(0);
	context.range(0, 10, 0, [&](int) { calls++; });
	context.range(0, 10, -1, [&](int) { calls++; });
	context.range(std::size_t(4), 0, 10, -1, [&](int) { calls++; });
	context.range(0.f, 1.f, 0.f, [&](float) { calls++; });
	context.range(0.f, 1.f, -0.5f, [&](float) { calls++; });
	CHECK(calls == 0);

	context.stop();
}

