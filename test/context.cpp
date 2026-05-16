/*
 *  Created by LuckyNeko on 17/05/2020.
 *  Copyright 2020 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <algorithm>
#include <array>
#include <atomic>
#include <catch2/catch_all.hpp>
#include <chrono>
#include <functional>
#include <list>
#include <map>
#include <multi/context.h>
#include <numeric>
#include <random>
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


// ---------------------------------------------------------------------------
// Context: reduce / transformReduce
// ---------------------------------------------------------------------------

TEST_CASE("Context: reduce sums an integer vector across thread counts")
{
	const auto threadCount = GENERATE(std::size_t(0), std::size_t(1), std::size_t(2), std::size_t(4));
	multi::Context context;
	context.start(threadCount);

	std::vector<int> v(1000);
	std::iota(v.begin(), v.end(), 1);  // 1..1000, expected sum 500500
	const int expected = std::accumulate(v.begin(), v.end(), 0);

	// Default chunk count (= threadCount).
	CHECK(context.reduce(v.begin(), v.end(), 0, std::plus<>{}) == expected);
	// Explicit oversubscription should also produce the same total.
	CHECK(context.reduce(std::size_t(16), v.begin(), v.end(), 0, std::plus<>{}) == expected);
	// init must contribute exactly once.
	CHECK(context.reduce(v.begin(), v.end(), 7, std::plus<>{}) == expected + 7);

	context.stop();
}

TEST_CASE("Context: reduce returns init for an empty range")
{
	multi::Context context;
	context.start(2);

	std::vector<int> empty;
	CHECK(context.reduce(empty.begin(), empty.end(), 42, std::plus<>{}) == 42);
	// Same path with an explicit taskCount must not produce a different answer.
	CHECK(context.reduce(std::size_t(8), empty.begin(), empty.end(), 42, std::plus<>{}) == 42);

	context.stop();
}

TEST_CASE("Context: reduce works on non-random-access iterators (std::list)")
{
	multi::Context context;
	context.start(4);

	std::list<int> l;
	for (int i = 1; i <= 100; ++i)
		l.push_back(i);
	// Materialised pointer-table path inside TransformReduceJob.
	CHECK(context.reduce(l.begin(), l.end(), 0, std::plus<>{}) == 5050);

	context.stop();
}

TEST_CASE("Context: transformReduce computes sum of squares")
{
	multi::Context context;
	context.start(4);

	std::vector<int> v(100);
	std::iota(v.begin(), v.end(), 1);  // 1..100
	const int expected = std::transform_reduce(
		v.begin(), v.end(), 0, std::plus<>{}, [](int x) { return x * x; });

	const int got = context.transformReduce(
		v.begin(), v.end(), 0, std::plus<>{}, [](int x) { return x * x; });
	CHECK(got == expected);

	context.stop();
}

TEST_CASE("Context: reduce rethrows the first exception from a chunk")
{
	multi::Context context;
	context.start(4);

	std::vector<int> v(100, 1);
	auto throwingOp = [](int a, int b) -> int {
		if (b == 1)
			throw std::runtime_error("boom");
		return a + b;
	};

	CHECK_THROWS_AS(context.reduce(v.begin(), v.end(), 0, throwingOp), std::runtime_error);

	context.stop();
}

TEST_CASE("Context: reduce supports non-arithmetic T (string concat)")
{
	multi::Context context;
	context.start(2);

	// String concat is associative but NOT commutative — for this test we
	// pick a 1-chunk taskCount so the library can only fold one way. This
	// exercises the path where T is a heavier movable type (heap-backed
	// std::string), separate from the trivially-copyable int path.
	std::vector<std::string> v = {"a", "b", "c", "d"};
	const std::string got = context.reduce(
		std::size_t(1), v.begin(), v.end(), std::string("="), std::plus<>{});
	CHECK(got == "=abcd");

	context.stop();
}

// ---------------------------------------------------------------------------
// Context: parallelAsync
// ---------------------------------------------------------------------------

TEST_CASE("Context: parallelAsync returns tuple of typed Handles")
{
	multi::Context context;
	context.start(4);

	// Heterogeneous return types — the tuple carries each precisely typed.
	auto handles = context.parallelAsync(
		[]() { return 42; },
		[]() { return std::string("hello"); },
		[]() { return 3.14; });

	static_assert(std::tuple_size_v<decltype(handles)> == 3,
	              "parallelAsync should produce one tuple element per functor");
	static_assert(std::is_same_v<std::tuple_element_t<0, decltype(handles)>, multi::Handle<int>>,
	              "element 0 should be Handle<int>");
	static_assert(std::is_same_v<std::tuple_element_t<1, decltype(handles)>, multi::Handle<std::string>>,
	              "element 1 should be Handle<std::string>");
	static_assert(std::is_same_v<std::tuple_element_t<2, decltype(handles)>, multi::Handle<double>>,
	              "element 2 should be Handle<double>");

	auto& [hInt, hStr, hDbl] = handles;
	CHECK(hInt.get() == 42);
	CHECK(hStr.get() == "hello");
	CHECK(hDbl.get() == Catch::Approx(3.14));

	context.stop();
}

TEST_CASE("Context: parallelAsync accepts void-returning functors")
{
	multi::Context context;
	context.start(2);

	std::atomic<int> counter(0);
	auto handles = context.parallelAsync(
		[&]() { counter.fetch_add(1, std::memory_order_relaxed); },
		[&]() { counter.fetch_add(10, std::memory_order_relaxed); });

	static_assert(std::is_same_v<std::tuple_element_t<0, decltype(handles)>, multi::Handle<void>>,
	              "void-returning functor should yield Handle<void>");

	auto& [h1, h2] = handles;
	h1.wait();
	h2.wait();
	CHECK(counter.load() == 11);

	context.stop();
}

TEST_CASE("Context: parallelAsync exceptions surface per-handle, siblings unaffected")
{
	multi::Context context;
	context.start(2);

	auto handles = context.parallelAsync(
		[]() -> int { throw std::runtime_error("first"); },
		[]() { return 7; });

	auto& [hThrow, hOk] = handles;
	// Each Handle carries its own AsyncJob — sibling exceptions don't
	// cross-contaminate (contrast with `parallel(a, b)` which captures
	// only the first exception across the shared ParallelJob).
	CHECK_THROWS_AS(hThrow.get(), std::runtime_error);
	CHECK(hOk.get() == 7);

	context.stop();
}

TEST_CASE("Context: parallelAsync with empty pack returns empty tuple")
{
	multi::Context context;
	context.start(2);

	auto handles = context.parallelAsync();
	static_assert(std::tuple_size_v<decltype(handles)> == 0,
	              "no functors should give an empty tuple");
	(void)handles;  // silence unused-variable on stricter compilers

	context.stop();
}

TEST_CASE("Context: parallelAsync handles can be observed via waitAll")
{
	// Single-worker context: the caller must participate to drain pending
	// async tasks; demonstrates that waitAll works against an
	// individual element of the parallelAsync tuple.
	multi::Context context;
	context.start(1);

	std::atomic<int> counter(0);
	auto outer = context.async([&]() {
		auto handles = context.parallelAsync(
			[&]() { counter.fetch_add(1, std::memory_order_relaxed); return 1; },
			[&]() { counter.fetch_add(2, std::memory_order_relaxed); return 2; });
		auto& [h1, h2] = handles;
		context.waitAll(h1);
		context.waitAll(h2);
		CHECK(h1.get() == 1);
		CHECK(h2.get() == 2);
	});
	outer.wait();
	CHECK(counter.load() == 3);

	context.stop();
}

// ---------------------------------------------------------------------------
// Context: waitUntil / waitAll / waitAny
// ---------------------------------------------------------------------------

TEST_CASE("Context: waitUntil drains the pool while waiting on a custom predicate")
{
	// Main thread (a non-worker) calls waitUntil on an atomic flag flipped
	// by a dispatched task — demonstrates that waitUntil works for non-
	// Handle wait conditions (counters, external events). Named Handle
	// captured explicitly so the temporary's dtor doesn't auto-wait
	// before waitUntil even runs.
	multi::Context context;
	context.start(2);

	std::atomic<bool> flag(false);
	std::atomic<int> counter(0);

	auto h = context.async([&]() {
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
		counter.fetch_add(1, std::memory_order_relaxed);
		flag.store(true, std::memory_order_release);
	});

	context.waitUntil([&]() { return flag.load(std::memory_order_acquire); });
	counter.fetch_add(10, std::memory_order_relaxed);

	CHECK(flag.load());
	CHECK(counter.load() == 11);

	h.wait();  // explicit, since waitUntil doesn't observe the Handle
	context.stop();
}

TEST_CASE("Context: waitAll blocks until every handle completes")
{
	multi::Context context;
	context.start(2);

	std::atomic<int> counter(0);
	auto h1 = context.async([&]() {
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
		counter.fetch_add(1, std::memory_order_relaxed);
	});
	auto h2 = context.async([&]() {
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
		counter.fetch_add(2, std::memory_order_relaxed);
	});
	auto h3 = context.async([&]() {
		counter.fetch_add(4, std::memory_order_relaxed);
		return 7;
	});

	context.waitAll(h1, h2, h3);

	// All three observed complete on return — no need to wait individually.
	CHECK(h1.complete());
	CHECK(h2.complete());
	CHECK(h3.complete());
	CHECK(counter.load() == 7);
	CHECK(h3.get() == 7);

	context.stop();
}

TEST_CASE("Context: waitAll on empty pack is an immediate no-op")
{
	multi::Context context;
	context.start(2);

	// Fold over && of zero arguments is the identity (true), so
	// waitUntil's predicate is satisfied on the first check.
	auto t0 = std::chrono::steady_clock::now();
	context.waitAll();
	auto elapsed = std::chrono::steady_clock::now() - t0;
	CHECK(elapsed < std::chrono::milliseconds(5));

	context.stop();
}

TEST_CASE("Context: waitAll on a tuple from parallelAsync")
{
	multi::Context context;
	context.start(4);

	auto handles = context.parallelAsync(
		[]() { return 1; },
		[]() { return std::string("two"); },
		[]() { return 3.0; });

	context.waitAll(handles);

	auto& [h1, h2, h3] = handles;
	CHECK(h1.complete());
	CHECK(h2.complete());
	CHECK(h3.complete());
	CHECK(h1.get() == 1);
	CHECK(h2.get() == "two");
	CHECK(h3.get() == Catch::Approx(3.0));

	context.stop();
}

TEST_CASE("Context: waitAny returns the index of a pre-completed handle")
{
	// Avoid timing flakiness: complete h2 explicitly via plain wait()
	// before calling waitAny. waitUntil's predicate then short-circuits
	// on the first scan (h0/h1 not complete, h2 complete) without
	// participating in stealing. If we instead relied on h0/h1 being
	// "slow enough" that h2 would naturally win, the test thread could
	// steal h0's task and finish it inline first — known property of
	// caller-participate waits.
	multi::Context context;
	context.start(4);

	auto h0 = context.async([]() {
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	});
	auto h1 = context.async([]() {
		std::this_thread::sleep_for(std::chrono::milliseconds(150));
	});
	auto h2 = context.async([]() {
		// trivial — completes almost immediately on its worker
	});

	h2.wait();  // plain blocking wait — does not steal, doesn't disturb h0/h1
	REQUIRE(h2.complete());

	const std::size_t idx = context.waitAny(h0, h1, h2);
	CHECK(idx == 2);

	h0.wait();
	h1.wait();
	context.stop();
}

TEST_CASE("Context: waitAny returns first-complete index when several are already done")
{
	// All handles complete before waitAny runs; the predicate sees them
	// all `complete()` on the first iteration and must return index 0
	// (the leftmost in source order short-circuits the || fold first).
	multi::Context context;
	context.start(2);

	auto h0 = context.async([]() { return 10; });
	auto h1 = context.async([]() { return 20; });
	auto h2 = context.async([]() { return 30; });

	// Force all three to complete first.
	context.waitAll(h0, h1, h2);

	const std::size_t idx = context.waitAny(h0, h1, h2);
	CHECK(idx == 0);

	context.stop();
}

TEST_CASE("Context: waitAny on a tuple from parallelAsync")
{
	multi::Context context;
	context.start(2);

	auto handles = context.parallelAsync(
		[]() { std::this_thread::sleep_for(std::chrono::milliseconds(100)); return 1; },
		[]() { return 2; });

	// Pre-complete h1 via plain wait() so waitAny's first scan finds it
	// without the test thread stealing h0. See the variadic case above
	// for the rationale.
	auto& [h0, h1] = handles;
	h1.wait();

	const std::size_t idx = context.waitAny(handles);
	CHECK(idx == 1);
	CHECK(h1.get() == 2);

	h0.wait();
	context.stop();
}

// ---------------------------------------------------------------------------
// Context: sort
// ---------------------------------------------------------------------------

TEST_CASE("Context: sort sorts a randomly shuffled int vector")
{
	auto threadCount = GENERATE(std::size_t(0), std::size_t(1), std::size_t(2), std::size_t(4));
	multi::Context context;
	context.start(threadCount);

	// 8k items: above the 1024 cutoff so parallel recursion is exercised
	// once thread count > 0; the threadCount=0 case stays serial inline.
	std::vector<int> v(8000);
	std::iota(v.begin(), v.end(), 0);
	std::mt19937 rng(12345);
	std::shuffle(v.begin(), v.end(), rng);

	context.sort(v.begin(), v.end());

	CHECK(std::is_sorted(v.begin(), v.end()));
	CHECK(v.front() == 0);
	CHECK(v.back() == 7999);

	context.stop();
}

TEST_CASE("Context: sort with custom comparator (descending)")
{
	multi::Context context;
	context.start(4);

	std::vector<int> v(3000);
	std::iota(v.begin(), v.end(), 0);
	std::mt19937 rng(67890);
	std::shuffle(v.begin(), v.end(), rng);

	context.sort(v.begin(), v.end(), std::greater<>{});

	CHECK(std::is_sorted(v.begin(), v.end(), std::greater<>{}));
	CHECK(v.front() == 2999);
	CHECK(v.back() == 0);

	context.stop();
}

TEST_CASE("Context: sort handles degenerate inputs")
{
	multi::Context context;
	context.start(4);

	SECTION("empty range")
	{
		std::vector<int> v;
		CHECK_NOTHROW(context.sort(v.begin(), v.end()));
		CHECK(v.empty());
	}

	SECTION("single element")
	{
		std::vector<int> v = {42};
		context.sort(v.begin(), v.end());
		CHECK(v == std::vector<int>{42});
	}

	SECTION("already sorted, large enough to trigger parallel recursion")
	{
		// Already-sorted input is the classic pathological case for naive
		// quicksort (degrades to O(n²) on first-element pivots). Median-
		// of-three should pick the middle and keep this fast.
		std::vector<int> v(4000);
		std::iota(v.begin(), v.end(), 0);
		context.sort(v.begin(), v.end());
		CHECK(std::is_sorted(v.begin(), v.end()));
	}

	SECTION("reverse sorted")
	{
		std::vector<int> v(4000);
		std::iota(v.rbegin(), v.rend(), 0);
		context.sort(v.begin(), v.end());
		CHECK(std::is_sorted(v.begin(), v.end()));
	}

	SECTION("all equal — 3-way partition's win condition")
	{
		// All elements equal: 3-way partition lumps everything in the
		// middle, no recursion needed. Returns after two O(n) scans even
		// for large input. Worst case for plain (2-way) quicksort.
		std::vector<int> v(4000, 7);
		context.sort(v.begin(), v.end());
		CHECK(std::all_of(v.begin(), v.end(), [](int x) { return x == 7; }));
	}

	SECTION("all equal at parallel size — exercises parallel 3-way DNF")
	{
		// Below the sub-threshold short-circuit (2 * LEAF_FLOOR = 8192)
		// the previous sections fall through to std::sort and never hit
		// multi::sort's parallel-DNF path on degenerate input. This
		// section uses a size above that threshold so the parallel DNF
		// actually runs on all-equal data; correctness is the same
		// (every element ends == 42), but we're exercising the algorithm
		// rather than the std::sort fallback.
		std::vector<int> v(20000, 42);
		context.sort(v.begin(), v.end());
		CHECK(std::all_of(v.begin(), v.end(), [](int x) { return x == 42; }));
	}

	context.stop();
}

TEST_CASE("Context: sort on a non-trivial element type")
{
	// Pair-of-strings: exercises move/swap on a type with heap storage,
	// not just trivially-copyable ints. Comparator orders by the int
	// field so the string is just payload that has to migrate correctly
	// through partition/swap.
	multi::Context context;
	context.start(4);

	std::vector<std::pair<int, std::string>> v;
	for (int i = 0; i < 2000; ++i)
		v.emplace_back(i, "item-" + std::to_string(i));
	std::mt19937 rng(0xC0FFEE);
	std::shuffle(v.begin(), v.end(), rng);

	context.sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

	CHECK(std::is_sorted(v.begin(), v.end(),
	                     [](const auto& a, const auto& b) { return a.first < b.first; }));
	CHECK(v.front().second == "item-0");
	CHECK(v.back().second == "item-1999");

	context.stop();
}

TEST_CASE("Context: sort on 1M items matches std::sort")
{
	// Larger input where parallel recursion runs to depth and chunks
	// land in the std::sort fallback. Cross-checks against std::sort
	// for total correctness — every value, not just is_sorted.
	multi::Context context;
	context.start(4);

	constexpr std::size_t N = 1'000'000;
	std::vector<int> v(N);
	std::iota(v.begin(), v.end(), 0);
	std::mt19937 rng(0xBEEF);
	std::shuffle(v.begin(), v.end(), rng);

	std::vector<int> reference = v;
	std::sort(reference.begin(), reference.end());

	context.sort(v.begin(), v.end());

	CHECK(v == reference);

	context.stop();
}

// ---------------------------------------------------------------------------
// Context: merge
// ---------------------------------------------------------------------------

TEST_CASE("Context: merge of two empty ranges produces empty output")
{
	multi::Context context;
	context.start(4);

	std::vector<int> a;
	std::vector<int> b;
	std::vector<int> out(1, -1);  // sentinel — must remain untouched
	context.merge(a.begin(), a.end(), b.begin(), b.end(), out.begin());

	CHECK(out.front() == -1);  // nothing written

	context.stop();
}

TEST_CASE("Context: merge with one empty side copies the other")
{
	multi::Context context;
	context.start(4);

	SECTION("A empty, B non-empty")
	{
		std::vector<int> a;
		std::vector<int> b = {1, 2, 3, 5, 8};
		std::vector<int> out(b.size(), 0);
		context.merge(a.begin(), a.end(), b.begin(), b.end(), out.begin());
		CHECK(out == b);
	}
	SECTION("A non-empty, B empty")
	{
		std::vector<int> a = {1, 1, 2, 3, 5};
		std::vector<int> b;
		std::vector<int> out(a.size(), 0);
		context.merge(a.begin(), a.end(), b.begin(), b.end(), out.begin());
		CHECK(out == a);
	}

	context.stop();
}

TEST_CASE("Context: merge of two single-element ranges")
{
	multi::Context context;
	context.start(2);

	SECTION("a < b")
	{
		std::vector<int> a = {1};
		std::vector<int> b = {2};
		std::vector<int> out(2, 0);
		context.merge(a.begin(), a.end(), b.begin(), b.end(), out.begin());
		CHECK(out == std::vector<int>{1, 2});
	}
	SECTION("b < a")
	{
		std::vector<int> a = {3};
		std::vector<int> b = {1};
		std::vector<int> out(2, 0);
		context.merge(a.begin(), a.end(), b.begin(), b.end(), out.begin());
		CHECK(out == std::vector<int>{1, 3});
	}
	SECTION("equal — A comes first (stable)")
	{
		// std::merge spec: equivalent elements from the first range come
		// first. multi::merge must match.
		std::vector<int> a = {5};
		std::vector<int> b = {5};
		std::vector<int> out(2, 0);
		context.merge(a.begin(), a.end(), b.begin(), b.end(), out.begin());
		CHECK(out == std::vector<int>{5, 5});
	}

	context.stop();
}

TEST_CASE("Context: merge of equal-length sorted runs")
{
	multi::Context context;
	context.start(4);

	// Sizes above MERGE_PARALLEL_THRESHOLD (8192 total) so the co-rank
	// parallel path actually runs. Compare against std::merge result.
	std::vector<int> a(5000), b(5000);
	for (int i = 0; i < 5000; ++i)
	{
		a[i] = 2 * i;       // evens
		b[i] = 2 * i + 1;   // odds
	}

	std::vector<int> got(10000, 0);
	context.merge(a.begin(), a.end(), b.begin(), b.end(), got.begin());

	std::vector<int> expected(10000, 0);
	std::merge(a.begin(), a.end(), b.begin(), b.end(), expected.begin());
	CHECK(got == expected);

	context.stop();
}

TEST_CASE("Context: merge with very imbalanced lengths")
{
	multi::Context context;
	context.start(4);

	SECTION("A=1, B=100k")
	{
		// One-element A streamed into a long B — the co-rank should
		// place the single A element correctly; most of the work is
		// just copying B.
		std::vector<int> a = {50000};
		std::vector<int> b(100000);
		std::iota(b.begin(), b.end(), 0);

		std::vector<int> got(100001, 0);
		context.merge(a.begin(), a.end(), b.begin(), b.end(), got.begin());

		std::vector<int> expected(100001, 0);
		std::merge(a.begin(), a.end(), b.begin(), b.end(), expected.begin());
		CHECK(got == expected);
	}
	SECTION("A=100k, B=1")
	{
		std::vector<int> a(100000);
		std::iota(a.begin(), a.end(), 0);
		std::vector<int> b = {50000};

		std::vector<int> got(100001, 0);
		context.merge(a.begin(), a.end(), b.begin(), b.end(), got.begin());

		std::vector<int> expected(100001, 0);
		std::merge(a.begin(), a.end(), b.begin(), b.end(), expected.begin());
		CHECK(got == expected);
	}

	context.stop();
}

TEST_CASE("Context: merge of identical sorted streams produces interleaved doubles")
{
	// Each stream is 0..9999. The merge result should have each value
	// twice in a row (e.g. 0, 0, 1, 1, 2, 2, ...). Pure stability test
	// at scale — every chunk boundary will land on a duplicate.
	multi::Context context;
	context.start(4);

	std::vector<int> a(10000), b(10000);
	std::iota(a.begin(), a.end(), 0);
	std::iota(b.begin(), b.end(), 0);

	std::vector<int> got(20000, 0);
	context.merge(a.begin(), a.end(), b.begin(), b.end(), got.begin());

	std::vector<int> expected(20000, 0);
	std::merge(a.begin(), a.end(), b.begin(), b.end(), expected.begin());
	CHECK(got == expected);
	REQUIRE(got.size() == 20000);
	// Spot-check: pairs are sorted-and-equal at consecutive indices.
	for (int i = 0; i < 10000; ++i)
	{
		CHECK(got[2 * i] == i);
		CHECK(got[2 * i + 1] == i);
	}

	context.stop();
}

TEST_CASE("Context: merge with custom comparator (descending)")
{
	multi::Context context;
	context.start(4);

	std::vector<int> a(5000), b(5000);
	std::iota(a.rbegin(), a.rend(), 0);  // 4999, 4998, …, 0
	std::iota(b.rbegin(), b.rend(), 0);

	std::vector<int> got(10000, 0);
	context.merge(a.begin(), a.end(), b.begin(), b.end(), got.begin(), std::greater<>{});

	std::vector<int> expected(10000, 0);
	std::merge(a.begin(), a.end(), b.begin(), b.end(), expected.begin(), std::greater<>{});
	CHECK(got == expected);

	context.stop();
}

TEST_CASE("Context: merge stability on duplicates across chunk boundary")
{
	// Pair<int, char> with comparator on the int field — stability is
	// observable via the char tag. After a stable merge, all 'A' tags
	// for a given key precede 'B' tags. Tests the co-rank's strict-vs-
	// non-strict comparison choice; the `(j > 0 && i < m && !comp(...))`
	// branch is the load-bearing line.
	multi::Context context;
	context.start(4);

	using P = std::pair<int, char>;
	std::vector<P> a, b;
	a.reserve(5000);
	b.reserve(5000);
	for (int i = 0; i < 5000; ++i)
	{
		a.emplace_back(i / 5, 'A');   // 1000 distinct keys, each appearing 5x in A
		b.emplace_back(i / 5, 'B');   // same keys, tag 'B' in B
	}

	auto byFirst = [](const P& x, const P& y) { return x.first < y.first; };

	std::vector<P> got(10000);
	context.merge(a.begin(), a.end(), b.begin(), b.end(), got.begin(), byFirst);

	std::vector<P> expected(10000);
	std::merge(a.begin(), a.end(), b.begin(), b.end(), expected.begin(), byFirst);
	CHECK(got == expected);

	// Explicit stability check: within each key group, all 'A' come
	// before all 'B'.
	for (int key = 0; key < 1000; ++key)
	{
		const int start = key * 10;
		// First 5 of each 10-block should be 'A', next 5 'B'.
		for (int j = 0; j < 5; ++j)
			CHECK(got[start + j].second == 'A');
		for (int j = 5; j < 10; ++j)
			CHECK(got[start + j].second == 'B');
	}

	context.stop();
}

TEST_CASE("Context: merge at 1M items cross-checks std::merge")
{
	multi::Context context;
	context.start(4);

	std::vector<int> a(500000), b(500000);
	std::iota(a.begin(), a.end(), 0);          // 0..499999
	std::iota(b.begin(), b.end(), 250000);     // 250000..749999 — overlapping range with A

	std::vector<int> got(1000000, 0);
	context.merge(a.begin(), a.end(), b.begin(), b.end(), got.begin());

	std::vector<int> expected(1000000, 0);
	std::merge(a.begin(), a.end(), b.begin(), b.end(), expected.begin());
	CHECK(got == expected);

	context.stop();
}
