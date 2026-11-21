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
#include <cstddef>
#include <cstdint>
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

#if defined(_WIN32)
#	define WIN32_LEAN_AND_MEAN
#	define NOMINMAX
#	include <windows.h>
#else
#	include <time.h>
#endif

namespace
{
	constexpr std::size_t DATA_SET_MAX_SIZE = 32;

	// CPU time the calling thread has used so far, or a negative duration where
	// the platform cannot tell.
	std::chrono::microseconds threadCpuTime()
	{
#if defined(_WIN32)
		FILETIME creation, exited, kernel, user;
		if (!GetThreadTimes(GetCurrentThread(), &creation, &exited, &kernel, &user))
			return std::chrono::microseconds(-1);
		const auto hundredNs = [](const FILETIME& t)
		{ return (static_cast<std::uint64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
		return std::chrono::microseconds(static_cast<std::int64_t>((hundredNs(kernel) + hundredNs(user)) / 10));
#elif defined(CLOCK_THREAD_CPUTIME_ID)
		timespec ts{};
		if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) != 0)
			return std::chrono::microseconds(-1);
		return std::chrono::seconds(ts.tv_sec) + std::chrono::microseconds(ts.tv_nsec / 1000);
#else
		return std::chrono::microseconds(-1);
#endif
	}
} // namespace

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
	auto outer = context.async([&]()
							   {
		auto inner = context.async([&]() { counter++; });
		context.waitAll(inner);  // explicit participation
		counter++; });
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
	context.waitAll(context.async([&]()
								  { x = 42; }));
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
	context.parallel([&]()
					 { a += 1; },
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

// ---------------------------------------------------------------------------
// Long waits. A waiter with nothing to steal spins for a bounded phase and then
// backs off to sleeping between steal attempts (details::WAIT_SPIN_TIME). These
// waits run well past that phase; they check results and stealing, not timing.
// ---------------------------------------------------------------------------

namespace
{
	constexpr auto LONG_TASK = std::chrono::milliseconds(300);

	// Spin, without stealing, until a task flags that a worker has picked it up.
	// A waiter that steals could otherwise take the long task and run it itself.
	void awaitStart(const std::atomic<bool>& started)
	{
		while (!started.load(std::memory_order_acquire))
			std::this_thread::yield();
	}
} // namespace

TEST_CASE("Context: waitAll and waitUntil return the result of a long task")
{
	multi::Context context;
	context.start(2);

	std::atomic<bool> valueStarted(false);
	auto value = context.async([&]()
							   {
		valueStarted.store(true, std::memory_order_release);
		std::this_thread::sleep_for(LONG_TASK);
		return 42; });
	awaitStart(valueStarted);
	context.waitAll(value);
	int v = 0;
	REQUIRE(value.get(&v));
	CHECK(v == 42);

	int payload = 0;
	std::atomic<bool> setterStarted(false), ready(false);
	auto setter = context.async([&]()
								{
		setterStarted.store(true, std::memory_order_release);
		std::this_thread::sleep_for(LONG_TASK);
		payload = 7;
		ready.store(true, std::memory_order_release); });
	awaitStart(setterStarted);
	context.waitUntil([&]()
					  { return ready.load(std::memory_order_acquire); });
	CHECK(payload == 7);
	context.waitAll(setter);

	context.stop();
}

TEST_CASE("Context: parallel returns once a long sibling finishes")
{
	multi::Context context;
	context.start(2);

	std::atomic<int> ran(0);
	std::atomic<bool> slowStarted(false);
	const auto slow = [&]()
	{
		slowStarted.store(true, std::memory_order_release);
		std::this_thread::sleep_for(LONG_TASK);
		ran += 1;
	};

	// Two tasks take runQueueJob's count == 2 path: task 0 goes to a worker and
	// task 1 runs on the caller, which here holds on until a worker has task 0,
	// so the caller is left waiting on it.
	context.parallel(slow, [&]()
					 {
		awaitStart(slowStarted);
		ran += 10; });
	CHECK(ran == 11);

	// More than two take the batch path.
	ran = 0;
	const auto fast = [&]()
	{ ran += 10; };
	context.parallel(slow, fast, fast, fast);
	CHECK(ran == 31);

	context.stop();
}

TEST_CASE("Context: a long wait still steals work that turns up late")
{
	// The sole worker stays busy past the spin phase, so by the time it queues the
	// inner task the caller has backed off to sleeping. Only the caller can run
	// it: the worker just polls inner.complete(), which never steals.
	multi::Context context;
	context.start(1);

	const std::thread::id caller = std::this_thread::get_id();
	std::atomic<bool> outerStarted(false);
	auto outer = context.async([&]()
							   {
		outerStarted.store(true, std::memory_order_release);
		std::this_thread::sleep_for(LONG_TASK);
		auto inner = context.async([]()
								   { return std::this_thread::get_id(); });
		// Bounded, so a waiter that stopped stealing fails the check below
		// instead of hanging the suite.
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		while (!inner.complete() && std::chrono::steady_clock::now() < deadline)
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		std::thread::id ranOn;
		return inner.get(&ranOn) ? ranOn : std::thread::id(); });
	awaitStart(outerStarted);
	context.waitAll(outer);

	std::thread::id innerRanOn;
	REQUIRE(outer.get(&innerRanOn));
	CHECK(innerRanOn == caller);

	context.stop();
}

TEST_CASE("Context: a long wait stops spinning")
{
	if (threadCpuTime().count() < 0)
		SKIP("no per-thread CPU clock on this platform");

	multi::Context context;
	context.start(2);

	std::atomic<bool> started(false);
	auto hdl = context.async([&]()
							 {
		started.store(true, std::memory_order_release);
		std::this_thread::sleep_for(LONG_TASK); });
	awaitStart(started);
	const auto wallStart = std::chrono::steady_clock::now();
	const auto cpuStart = threadCpuTime();
	context.waitAll(hdl);
	const auto cpu = threadCpuTime() - cpuStart;
	const auto wall = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - wallStart);

	// Spinning through the whole wait costs about as much CPU as wall time; backing
	// off costs about the spin phase. Half is far from both, so load and
	// sanitizers do not move the result across it.
	CAPTURE(cpu.count(), wall.count());
	CHECK(cpu < wall / 2);

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
		context.each(tmpVec.begin(), tmpVec.end(), [](int& v)
					 { v += 1; });

		std::atomic<int> a(0);
		context.each(tmpVec.begin(), tmpVec.end(), [&](int v)
					 { a += v; });
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
					 [](std::pair<const int, int>& p)
					 { p.second += 1; });

		std::atomic<int> a(0);
		context.each(tmpMap.begin(), tmpMap.end(),
					 [&](const std::pair<const int, int>& p)
					 { a += p.second; });
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
		context.each(taskCount, tmpVec.begin(), tmpVec.end(), [](int& v)
					 { v += 1; });

		std::atomic<int> a(0);
		context.each(taskCount, tmpVec.begin(), tmpVec.end(), [&](int v)
					 { a += v; });
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
					 [](std::pair<const int, int>& p)
					 { p.second += 1; });

		std::atomic<int> a(0);
		context.each(taskCount, tmpMap.begin(), tmpMap.end(),
					 [&](const std::pair<const int, int>& p)
					 { a += p.second; });
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
	context.each(policy, v, [&](int x)
				 { eachSum += x; });
	CHECK(eachSum == expected);

	std::atomic<int> rangeSum(0);
	context.range(policy, 0, 50, 1, [&](int i)
				  { rangeSum += i; });
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
				context.range(begin, end, step, [&](int /*i*/)
							  { a += 1; });

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
				context.range(begin, end, step, [&](float /*i*/)
							  { a += 1; });

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
				context.range(taskCount, begin, end, step, [&](int /*i*/)
							  { a += 1; });

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
	context.range(0, 10, [&](int i)
				  { sum += i; });
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
		context.range(std::size_t(1), 0.f, end, step, [&](float /*i*/)
					  { ++visited; });
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
	context.range(std::size_t(1), top - 5, top, 3, [&](int i)
				  { seen.push_back(i); });
	CHECK(seen == std::vector<int>{top - 5, top - 2});

	context.stop();
}

// ---------------------------------------------------------------------------
// Unsigned indices. The narrow types are the interesting ones: they promote
// through the index arithmetic, and their top is reachable in a test.
// ---------------------------------------------------------------------------

TEMPLATE_TEST_CASE("Context: range unsigned", "", std::uint8_t, std::uint16_t, unsigned, std::size_t)
{
	auto threadCount = GENERATE(std::size_t(0), std::size_t(1), std::size_t(2), std::size_t(4));
	multi::Context context;
	context.start(threadCount);

	for (TestType begin = 0; begin < static_cast<TestType>(DATA_SET_MAX_SIZE); ++begin)
	{
		for (TestType end = 0; end < static_cast<TestType>(DATA_SET_MAX_SIZE); ++end)
		{
			for (TestType step = 1; step < static_cast<TestType>(DATA_SET_MAX_SIZE); ++step)
			{
				std::atomic<int> a(0);
				context.range(begin, end, step, [&](TestType /*i*/)
							  { a += 1; });

				const int totalRange = std::max(0, static_cast<int>(end) - static_cast<int>(begin));
				const int s = static_cast<int>(step);
				const int expected = (totalRange / s) + ((totalRange % s > 0) ? 1 : 0);
				CHECK(a == expected);
			}
		}
	}

	context.stop();
}

TEMPLATE_TEST_CASE("Context: range unsigned with a ChunkPolicy", "", std::uint8_t, std::uint16_t, unsigned, std::size_t)
{
	auto threadCount = GENERATE(std::size_t(0), std::size_t(1), std::size_t(2), std::size_t(4));
	auto policy = GENERATE(multi::ChunkPolicy(0), multi::ChunkPolicy(1), multi::ChunkPolicy(2), multi::ChunkPolicy(4),
						   multi::Auto, multi::PerItem, multi::Serial);
	multi::Context context;
	context.start(threadCount);

	for (TestType begin = 0; begin < static_cast<TestType>(DATA_SET_MAX_SIZE); ++begin)
	{
		for (TestType end = 0; end < static_cast<TestType>(DATA_SET_MAX_SIZE); ++end)
		{
			for (TestType step = 1; step < static_cast<TestType>(DATA_SET_MAX_SIZE); ++step)
			{
				std::atomic<int> a(0);
				context.range(policy, begin, end, step, [&](TestType /*i*/)
							  { a += 1; });

				const int totalRange = std::max(0, static_cast<int>(end) - static_cast<int>(begin));
				const int s = static_cast<int>(step);
				const int expected = (totalRange / s) + ((totalRange % s > 0) ? 1 : 0);
				CHECK(a == expected);
			}
		}
	}

	context.stop();
}

TEST_CASE("Context: range stays in bounds at the top of an unsigned type")
{
	// One chunk over the lot is where stepping by addition would wrap: the index
	// after the last one does not fit, and a wrapped end visits nothing at all.
	auto policy = GENERATE(multi::ChunkPolicy(1), multi::ChunkPolicy(2), multi::Auto, multi::PerItem, multi::Serial);
	multi::Context context;
	context.start(2);

	const auto visited = [&](auto begin, auto end, auto step)
	{
		using IDX = decltype(begin);
		std::mutex mutex;
		std::vector<IDX> seen;
		context.range(policy, begin, end, step, [&](IDX i)
					  {
			const std::lock_guard<std::mutex> lock(mutex);
			seen.push_back(i); });
		std::sort(seen.begin(), seen.end());
		return seen;
	};

	using U8 = std::uint8_t;
	CHECK(visited(U8{250}, U8{255}, U8{3}) == std::vector<U8>{250, 253});
	CHECK(visited(U8{0}, U8{255}, U8{254}) == std::vector<U8>{0, 254});
	CHECK(visited(U8{250}, U8{255}, U8{200}) == std::vector<U8>{250});
	CHECK(visited(U8{0}, U8{255}, U8{1}).size() == 255);

	// span + step - 1 wraps on the first of these; a round-up written that way counts 0.
	const std::size_t top = std::numeric_limits<std::size_t>::max();
	CHECK(visited(std::size_t{0}, top, top - 1) == std::vector<std::size_t>{0, top - 1});
	CHECK(visited(top - 4, top, std::size_t{3}) == std::vector<std::size_t>{top - 4, top - 1});

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
	CHECK_THROWS_AS(context.range(multi::Serial, 0, 100, 1, [&](int i)
								  { visit(seenRange, i); }),
					std::runtime_error);
	CHECK(seenRange == stoppedAtSeven);

	std::vector<int> items(100);
	std::iota(items.begin(), items.end(), 0);
	std::vector<int> seenEach;
	CHECK_THROWS_AS(context.each(multi::Serial, items, [&](int i)
								 { visit(seenEach, i); }),
					std::runtime_error);
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
	context.range(multi::Serial, 0, 1000, 1, [&](int)
				  { ++visited; });
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
	context.each(empty.begin(), empty.end(), [&](int)
				 { calls++; });
	CHECK(calls == 0);

	// taskCount overload too.
	context.each(std::size_t(4), empty.begin(), empty.end(), [&](int)
				 { calls++; });
	context.each(multi::Serial, empty, [&](int)
				 { calls++; });
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
	context.range(5, 5, 1, [&](int)
				  { calls++; });
	context.range(5, 3, 1, [&](int)
				  { calls++; });
	context.range(std::size_t(4), 5, 5, 1, [&](int)
				  { calls++; });
	context.range(std::size_t(4), 5, 3, 1, [&](int)
				  { calls++; });
	context.range(multi::Serial, 5, 5, 1, [&](int)
				  { calls++; });
	context.range(multi::Serial, 5, 3, 1, [&](int)
				  { calls++; });
	// Unsigned: end < begin must be empty, not a span that underflowed to huge.
	context.range(5u, 3u, [&](unsigned)
				  { calls++; });
	context.range(std::size_t(4), 5u, 3u, 1u, [&](unsigned)
				  { calls++; });
	context.range(std::size_t{5}, std::size_t{0}, [&](std::size_t)
				  { calls++; });
	context.range(0u, 10u, 0u, [&](unsigned)
				  { calls++; });
	CHECK(calls == 0);

	context.stop();
}

TEST_CASE("Context: range with a non-positive step is a no-op")
{
	multi::Context context;
	context.start(2);

	std::atomic<int> calls(0);
	context.range(0, 10, 0, [&](int)
				  { calls++; });
	context.range(0, 10, -1, [&](int)
				  { calls++; });
	context.range(std::size_t(4), 0, 10, -1, [&](int)
				  { calls++; });
	context.range(0.f, 1.f, 0.f, [&](float)
				  { calls++; });
	context.range(0.f, 1.f, -0.5f, [&](float)
				  { calls++; });
	CHECK(calls == 0);

	context.stop();
}
