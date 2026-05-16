/*
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 *
 *  Benchmark workloads for bench-multi.
 *
 *  Tags:
 *    [fast]     – total runtime ~seconds; safe for iterative dev
 *    [slow]     – runtime ~minutes; run when committing perf work
 *    [baseline] – serial reference; shows speedup but adds time
 *
 *  Typical invocations:
 *    ./bench-multi "[fast]"                  # quick iteration
 *    ./bench-multi "[bench]"                 # everything
 *    ./bench-multi --benchmark-samples=30    # faster, fewer samples
 *    ./bench-multi --reporter xml --out r.xml
 */

#include <catch2/catch_all.hpp>

#include "graph.h"
#include "mandelbrot.h"

#include <multi/multi.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <numeric>
#include <random>
#include <vector>

namespace
{
	// ---------------------------------------------------------------------------
	// Parallel helpers — functor objects so generic lambdas can accept them
	// without a std::function wrapper in the bench harness.  The only
	// std::function allocation happens inside multi::range / multi::parallel
	// at the library boundary.
	// ---------------------------------------------------------------------------

	struct parallel_for_baseline_t
	{
		template <class F>
		void operator()(int begin, int end, F&& f) const
		{
			for (int i = begin; i < end; ++i)
				f(i);
		}
	};

	struct parallel_for_items_t
	{
		template <class F>
		void operator()(int begin, int end, F&& f) const
		{
			multi::range(begin, end, 1, std::forward<F>(f));
		}
	};

	struct parallel_for_chunks_t
	{
		template <class F>
		void operator()(int begin, int end, F&& f) const
		{
			int count = end - begin;
			if (count <= 0)
				return;
			// K is the oversubscription factor: chunks = (workers+caller) * K.
			// Picked from chunk_factor_scan: K=4 is within ~6% of best on
			// every shape we measured (uniform, imbalanced, heavy capture).
			// K=1 wins on uniform (no extra dispatch) but loses ~75% on
			// imbalanced (no steal granularity); K=8+ is the reverse.
			const size_t K = 4;
			size_t chunks = (multi::threadCount() + 1) * K;
			if (chunks < 2)
				chunks = 2;
			if (static_cast<int>(chunks) > count)
				chunks = static_cast<size_t>(count);
			multi::range(chunks, begin, end, 1, std::forward<F>(f));
		}
	};

	struct parallel_invoke_t
	{
		template <class A, class B>
		void operator()(A&& a, B&& b) const
		{
			multi::parallel(multi::details::Task(std::forward<A>(a)), multi::details::Task(std::forward<B>(b)));
		}
	};

	constexpr parallel_for_baseline_t baseline{};
	constexpr parallel_for_items_t    items{};
	constexpr parallel_for_chunks_t   chunks{};
	constexpr parallel_invoke_t       invoke_multi{};

	// ---------------------------------------------------------------------------
	// Nested tree-sum helpers
	// ---------------------------------------------------------------------------

	void treeSumSerial(int depth, int threshold, int leafWork,
					   uint64_t seed, uint64_t& out)
	{
		if (depth <= threshold)
		{
			uint64_t acc = seed;
			for (int k = 0; k < leafWork; ++k)
				acc = acc * 6364136223846793005ULL + 1442695040888963407ULL;
			out = acc;
			return;
		}
		uint64_t a = 0, b = 0;
		treeSumSerial(depth - 1, threshold, leafWork, seed, a);
		treeSumSerial(depth - 1, threshold, leafWork, seed + 1, b);
		out = a + b;
	}

	void treeSum(int depth, int threshold, int leafWork,
				 uint64_t seed, uint64_t& out)
	{
		if (depth <= threshold)
		{
			uint64_t acc = seed;
			for (int k = 0; k < leafWork; ++k)
				acc = acc * 6364136223846793005ULL + 1442695040888963407ULL;
			out = acc;
			return;
		}
		uint64_t a = 0, b = 0;
		invoke_multi(
			[depth, threshold, leafWork, seed, &a]()
			{ treeSum(depth - 1, threshold, leafWork, seed, a); },
			[depth, threshold, leafWork, seed, &b]()
			{ treeSum(depth - 1, threshold, leafWork, seed + 1, b); });
		out = a + b;
	}

} // namespace

// ---------------------------------------------------------------------------
// empty_tasks — pure dispatch overhead, no work per task.
// No baseline BENCHMARK: an empty serial for-loop is DCE'd by the compiler
// and would report ~0, which is not a meaningful comparison point.
// ---------------------------------------------------------------------------
TEST_CASE("empty_tasks", "[bench][fast]")
{
	SECTION("500 tasks")
	{
		const int n = 500;
		BENCHMARK("multi(items)") { items(0, n, [](int) {}); };
		BENCHMARK("multi(chunks)") { chunks(0, n, [](int) {}); };
	}
	SECTION("1k tasks")
	{
		const int n = 1000;
		BENCHMARK("multi(items)") { items(0, n, [](int) {}); };
		BENCHMARK("multi(chunks)") { chunks(0, n, [](int) {}); };
	}
	SECTION("5k tasks")
	{
		const int n = 5000;
		BENCHMARK("multi(items)") { items(0, n, [](int) {}); };
		BENCHMARK("multi(chunks)") { chunks(0, n, [](int) {}); };
	}
}

// ---------------------------------------------------------------------------
// mandelbrot — uniform CPU-bound work, moderate grain. "Well-behaved".
// ---------------------------------------------------------------------------
TEST_CASE("mandelbrot", "[bench][slow]")
{
	const int width = 512, height = 512, numIter = 256, frames = 10;
	Graph graph(width, height, 2.0,
				-(6.01000070505 / 11.0), -(6.01000070505 / 11.0));

	auto run = [&](auto pf)
	{
		constexpr double escapeRadius = 2.0;
		for (int f = 1; f <= frames; ++f)
		{
			graph.setScale(2.0 / std::pow(1.05, f));
			Graph* gp = &graph;
			pf(0, gp->height(), [gp, numIter](int y)
			   {
				constexpr double er = 2.0;
				double Cy = gp->getY(y);
				for (int x = 0; x < gp->width(); ++x)
				{
					double Cx = gp->getX(x);
					gp->writeColour(mandelbrotColour(mandelbrotIterations(Cx, Cy, er, numIter), numIter), x, y);
				} });
		}
	};

	BENCHMARK("serial (baseline)") { run(baseline); };
	BENCHMARK("multi(items)") { run(items); };
	BENCHMARK("multi(chunks)") { run(chunks); };

	const uint8_t* pix = graph.buffer();
	REQUIRE((pix[0] | pix[1] | pix[2]) != 0);
}

// ---------------------------------------------------------------------------
// tiny_tasks — many short tasks; reveals per-task scheduling overhead.
// Parameterised to show how overhead scales with task count.
// ---------------------------------------------------------------------------
TEST_CASE("tiny_tasks", "[bench][fast]")
{
	constexpr int workPerTask = 500;

	auto run = [&](auto pf, std::vector<uint64_t>& buf)
	{
		uint64_t* out = buf.data();
		const int n = static_cast<int>(buf.size());
		pf(0, n, [out, workPerTask](int i)
		   {
			uint64_t acc = static_cast<uint64_t>(i);
			for (int k = 0; k < workPerTask; ++k)
				acc = acc * 6364136223846793005ULL + 1442695040888963407ULL;
			out[i] = acc; });
	};

	SECTION("1k tasks")
	{
		std::vector<uint64_t> buf(1000, 0);
		BENCHMARK("serial (baseline)") { run(baseline, buf); };
		BENCHMARK("multi(items)") { run(items, buf); };
		BENCHMARK("multi(chunks)") { run(chunks, buf); };
		REQUIRE(buf.back() != 0);
	}
	SECTION("5k tasks")
	{
		std::vector<uint64_t> buf(5000, 0);
		BENCHMARK("serial (baseline)") { run(baseline, buf); };
		BENCHMARK("multi(items)") { run(items, buf); };
		BENCHMARK("multi(chunks)") { run(chunks, buf); };
		REQUIRE(buf.back() != 0);
	}
	SECTION("50k tasks")
	{
		std::vector<uint64_t> buf(50000, 0);
		BENCHMARK("serial (baseline)") { run(baseline, buf); };
		BENCHMARK("multi(items)") { run(items, buf); };
		BENCHMARK("multi(chunks)") { run(chunks, buf); };
		REQUIRE(buf.back() != 0);
	}
}

// ---------------------------------------------------------------------------
// imbalanced — task i does O(i) work; static partitioning suffers, stealing
// wins.
// ---------------------------------------------------------------------------
TEST_CASE("imbalanced", "[bench][slow]")
{
	constexpr int workUnitScale = 8000;

	auto run = [&](auto pf, std::vector<uint64_t>& buf)
	{
		uint64_t* out = buf.data();
		const int n = static_cast<int>(buf.size());
		pf(0, n, [out, workUnitScale](int i)
		   {
			uint64_t acc = static_cast<uint64_t>(i) + 1;
			for (int k = 0; k < i * workUnitScale; ++k)
				acc = acc * 6364136223846793005ULL + 1442695040888963407ULL;
			out[i] = acc; });
	};

	SECTION("100 tasks")
	{
		std::vector<uint64_t> buf(100, 0);
		BENCHMARK("serial (baseline)") { run(baseline, buf); };
		BENCHMARK("multi(items)") { run(items, buf); };
		BENCHMARK("multi(chunks)") { run(chunks, buf); };
		REQUIRE(buf.back() != 0);
	}
	SECTION("200 tasks")
	{
		std::vector<uint64_t> buf(200, 0);
		BENCHMARK("serial (baseline)") { run(baseline, buf); };
		BENCHMARK("multi(items)") { run(items, buf); };
		BENCHMARK("multi(chunks)") { run(chunks, buf); };
		REQUIRE(buf.back() != 0);
	}
}

// ---------------------------------------------------------------------------
// nested — recursive fork-join tree via parallel_invoke. Serial baseline
// included; adds ~1.3s but shows the fork-join speedup directly.
// ---------------------------------------------------------------------------
TEST_CASE("nested", "[bench][fast]")
{
	constexpr int depth = 12, threshold = 6, leafWork = 200000;

	uint64_t sink = 0;

	BENCHMARK("serial (baseline)")
	{
		treeSumSerial(depth, threshold, leafWork, 1, sink);
		return sink;
	};
	BENCHMARK("multi")
	{
		treeSum(depth, threshold, leafWork, 1, sink);
		return sink;
	};

	REQUIRE(sink != 0);
}

// ---------------------------------------------------------------------------
// async_latency — serial async round-trips (submit one task, wait, repeat).
// Measures Handle + AsyncState lifecycle cost per dispatch.
// ---------------------------------------------------------------------------
TEST_CASE("async_latency", "[bench][fast]")
{
	SECTION("100 rounds")
	{
		BENCHMARK("multi::async")
		{
			for (int i = 0; i < 100; ++i)
				multi::async([]() {}).wait();
			return 100;
		};
	}
	SECTION("500 rounds")
	{
		BENCHMARK("multi::async")
		{
			for (int i = 0; i < 500; ++i)
				multi::async([]() {}).wait();
			return 500;
		};
	}
}

// ---------------------------------------------------------------------------
// async_fanout — submit N concurrent async tasks, collect all handles, then
// wait for each. Compare against multi(items) from empty_tasks to see the
// per-task cost of Handle + AsyncState vs the lightweight Job counter.
// ---------------------------------------------------------------------------
TEST_CASE("async_fanout", "[bench][fast]")
{
	auto run = [](int numTasks, std::vector<multi::Handle<>>& handles)
	{
		handles.clear();
		for (int i = 0; i < numTasks; ++i)
			handles.push_back(multi::async([]() {}));
		for (auto& h : handles)
			h.wait();
		return handles.size();
	};

	SECTION("100 tasks")
	{
		std::vector<multi::Handle<>> handles;
		handles.reserve(100);
		BENCHMARK("multi::async (fanout)") { return run(100, handles); };
	}
	SECTION("1k tasks")
	{
		std::vector<multi::Handle<>> handles;
		handles.reserve(1000);
		BENCHMARK("multi::async (fanout)") { return run(1000, handles); };
	}
}

// ---------------------------------------------------------------------------
// heavy_capture — exercises the chunked range path with a user functor whose
// by-value capture exceeds std::function SBO on every common STL (libstdc++
// 16, libc++ 24, MSVC 64). Designed to exhibit the difference between
// capturing func by value vs by reference inside the chunked dispatch
// lambdas: by-value forces one heap allocation per chunk; by-reference keeps
// the chunk lambda small enough to stay in SBO.
//
// The 'multi(items)' row routes through the non-chunked range overload
// (which already captures &func) and serves as a control — it should not
// move when the chunked overload's capture changes.
// ---------------------------------------------------------------------------
TEST_CASE("heavy_capture", "[bench][fast]")
{
	struct HeavyState
	{
		uint64_t data[32]; // 256 bytes
	};
	HeavyState state{};
	for (int i = 0; i < 32; ++i)
		state.data[i] = 0xDEADBEEFCAFEBABEULL ^ static_cast<uint64_t>(i);

	auto run = [&](auto pf, std::vector<uint64_t>& buf)
	{
		uint64_t* out = buf.data();
		const int n = static_cast<int>(buf.size());
		pf(0, n, [out, state](int i)
		   {
			uint64_t acc = static_cast<uint64_t>(i);
			for (int k = 0; k < 100; ++k)
				acc = acc * 6364136223846793005ULL + state.data[k & 31];
			out[i] = acc; });
	};

	SECTION("1k tasks")
	{
		std::vector<uint64_t> buf(1000, 0);
		BENCHMARK("multi(items)") { run(items, buf); };
		BENCHMARK("multi(chunks)") { run(chunks, buf); };
		REQUIRE(buf.back() != 0);
	}
	SECTION("5k tasks")
	{
		std::vector<uint64_t> buf(5000, 0);
		BENCHMARK("multi(items)") { run(items, buf); };
		BENCHMARK("multi(chunks)") { run(chunks, buf); };
		REQUIRE(buf.back() != 0);
	}
}

// ---------------------------------------------------------------------------
// steal_contention — multiple external driver threads issue parallel work
// simultaneously. While each driver waits for its own batch it spins on
// tryStealAny. Without victim-index rotation, every driver hits worker 0's
// deque mutex first on each scan, concentrating contention on a single
// cache line. The async variant pushes the contention harder: each
// Handle::wait re-enters the steal loop on every round-trip with no chunked
// work for the driver to actually steal — pure scan overhead.
// ---------------------------------------------------------------------------
TEST_CASE("steal_contention", "[bench][fast]")
{
	auto driveRange = [](int numDrivers, int callsPerDriver, int chunksPerCall, int itemsPerCall)
	{
		std::atomic<uint64_t> sink(0);
		std::vector<std::thread> drivers;
		drivers.reserve(static_cast<size_t>(numDrivers));
		for (int d = 0; d < numDrivers; ++d)
		{
			drivers.emplace_back([&, callsPerDriver, chunksPerCall, itemsPerCall]()
			{
				for (int c = 0; c < callsPerDriver; ++c)
				{
					multi::range(chunksPerCall, 0, itemsPerCall, 1, [&](int)
					{
						sink.fetch_add(1, std::memory_order_relaxed);
					});
				}
			});
		}
		for (auto& t : drivers)
			t.join();
		return sink.load();
	};

	auto driveAsync = [](int numDrivers, int callsPerDriver)
	{
		std::atomic<uint64_t> sink(0);
		std::vector<std::thread> drivers;
		drivers.reserve(static_cast<size_t>(numDrivers));
		for (int d = 0; d < numDrivers; ++d)
		{
			drivers.emplace_back([&, callsPerDriver]()
			{
				for (int c = 0; c < callsPerDriver; ++c)
				{
					multi::async([&]()
					{
						sink.fetch_add(1, std::memory_order_relaxed);
					}).wait();
				}
			});
		}
		for (auto& t : drivers)
			t.join();
		return sink.load();
	};

	SECTION("4 drivers x range")
	{
		BENCHMARK("range 4x200x32") { return driveRange(4, 200, 32, 1000); };
	}
	SECTION("8 drivers x range")
	{
		BENCHMARK("range 8x200x32") { return driveRange(8, 200, 32, 1000); };
	}
	SECTION("4 drivers x async.wait")
	{
		BENCHMARK("async 4x500") { return driveAsync(4, 500); };
	}
	SECTION("8 drivers x async.wait")
	{
		BENCHMARK("async 8x500") { return driveAsync(8, 500); };
	}
}

// ---------------------------------------------------------------------------
// chunk_factor_scan — one-off study of the K oversubscription factor used by
// parallel_for_chunks_t (chunks = (threadCount+1)*K). Sweeps K across {1, 2,
// 4, 8, 16, 32, 64} on three workload shapes:
//   * uniform   — same work per task, dispatch overhead dominates
//   * imbalanced — task i does O(i) work, exercises stealing
//   * heavy_cap — large user functor capture, exercises chunk lambda size
// Each row labels itself with the K value so the picker is direct.
// ---------------------------------------------------------------------------
TEST_CASE("chunk_factor_scan", "[bench][fast]")
{
	auto run_chunks = [](int K, int begin, int end, auto&& f)
	{
		int count = end - begin;
		if (count <= 0)
			return;
		size_t k = static_cast<size_t>(K);
		size_t c = (multi::threadCount() + 1) * k;
		if (c < 2)
			c = 2;
		if (static_cast<int>(c) > count)
			c = static_cast<size_t>(count);
		multi::range(c, begin, end, 1, std::forward<decltype(f)>(f));
	};

	struct HeavyState
	{
		uint64_t data[32];
	}; // 256 bytes
	HeavyState state{};
	for (int i = 0; i < 32; ++i)
		state.data[i] = 0xDEADBEEFCAFEBABEULL ^ static_cast<uint64_t>(i);

	SECTION("uniform 5k")
	{
		std::vector<uint64_t> buf(5000, 0);
		auto run = [&](int K)
		{
			uint64_t* out = buf.data();
			run_chunks(K, 0, static_cast<int>(buf.size()), [out](int i)
					   {
				uint64_t acc = static_cast<uint64_t>(i);
				for (int k = 0; k < 200; ++k)
					acc = acc * 6364136223846793005ULL + 1442695040888963407ULL;
				out[i] = acc; });
		};
		BENCHMARK("K=1") { run(1); };
		BENCHMARK("K=2") { run(2); };
		BENCHMARK("K=4") { run(4); };
		BENCHMARK("K=8") { run(8); };
		BENCHMARK("K=16") { run(16); };
		BENCHMARK("K=32") { run(32); };
		BENCHMARK("K=64") { run(64); };
		REQUIRE(buf.back() != 0);
	}
	SECTION("imbalanced 200")
	{
		std::vector<uint64_t> buf(200, 0);
		auto run = [&](int K)
		{
			uint64_t* out = buf.data();
			run_chunks(K, 0, static_cast<int>(buf.size()), [out](int i)
					   {
				uint64_t acc = static_cast<uint64_t>(i) + 1;
				for (int k = 0; k < i * 4000; ++k)
					acc = acc * 6364136223846793005ULL + 1442695040888963407ULL;
				out[i] = acc; });
		};
		BENCHMARK("K=1") { run(1); };
		BENCHMARK("K=2") { run(2); };
		BENCHMARK("K=4") { run(4); };
		BENCHMARK("K=8") { run(8); };
		BENCHMARK("K=16") { run(16); };
		BENCHMARK("K=32") { run(32); };
		BENCHMARK("K=64") { run(64); };
		REQUIRE(buf.back() != 0);
	}
	SECTION("heavy_cap 5k")
	{
		std::vector<uint64_t> buf(5000, 0);
		auto run = [&](int K)
		{
			uint64_t* out = buf.data();
			run_chunks(K, 0, static_cast<int>(buf.size()), [out, state](int i)
					   {
				uint64_t acc = static_cast<uint64_t>(i);
				for (int k = 0; k < 100; ++k)
					acc = acc * 6364136223846793005ULL + state.data[k & 31];
				out[i] = acc; });
		};
		BENCHMARK("K=1") { run(1); };
		BENCHMARK("K=2") { run(2); };
		BENCHMARK("K=4") { run(4); };
		BENCHMARK("K=8") { run(8); };
		BENCHMARK("K=16") { run(16); };
		BENCHMARK("K=32") { run(32); };
		BENCHMARK("K=64") { run(64); };
		REQUIRE(buf.back() != 0);
	}
}

// ---------------------------------------------------------------------------
// each_iter — exercises multi::each over std::vector (random-access iterator)
// and std::map (bidirectional iterator). These hit two different code paths
// in EachJob / ChunkedEachJob: random-access stores the iterator directly,
// bidirectional materialises a std::vector<T*> pointer table at construction.
// Per-item work matches tiny_tasks's workPerTask so per-task dispatch cost
// is visible against the actual work.
// ---------------------------------------------------------------------------
TEST_CASE("each_iter", "[bench][fast]")
{
	constexpr int workPerItem = 500;

	auto bodyVec = [workPerItem](uint64_t& slot)
	{
		uint64_t acc = slot;
		for (int k = 0; k < workPerItem; ++k)
			acc = acc * 6364136223846793005ULL + 1442695040888963407ULL;
		slot = acc;
	};
	auto bodyMap = [workPerItem](std::pair<const int, uint64_t>& kv)
	{
		uint64_t acc = kv.second;
		for (int k = 0; k < workPerItem; ++k)
			acc = acc * 6364136223846793005ULL + 1442695040888963407ULL;
		kv.second = acc;
	};

	SECTION("vector 1k items")
	{
		std::vector<uint64_t> v(1000);
		for (size_t i = 0; i < v.size(); ++i)
			v[i] = i + 1;
		BENCHMARK("multi::each (random-access)") { multi::each(v.begin(), v.end(), bodyVec); };
		const size_t chunks = (multi::threadCount() + 1) * 4;
		BENCHMARK("multi::each chunks K=4") { multi::each(chunks, v.begin(), v.end(), bodyVec); };
		REQUIRE(v.back() != 0);
	}

	SECTION("vector 10k items")
	{
		std::vector<uint64_t> v(10000);
		for (size_t i = 0; i < v.size(); ++i)
			v[i] = i + 1;
		BENCHMARK("multi::each (random-access)") { multi::each(v.begin(), v.end(), bodyVec); };
		const size_t chunks = (multi::threadCount() + 1) * 4;
		BENCHMARK("multi::each chunks K=4") { multi::each(chunks, v.begin(), v.end(), bodyVec); };
		REQUIRE(v.back() != 0);
	}

	SECTION("map 1k items")
	{
		std::map<int, uint64_t> m;
		for (int i = 0; i < 1000; ++i)
			m.emplace(i, static_cast<uint64_t>(i + 1));
		BENCHMARK("multi::each (bidirectional)") { multi::each(m.begin(), m.end(), bodyMap); };
		const size_t chunks = (multi::threadCount() + 1) * 4;
		BENCHMARK("multi::each chunks K=4") { multi::each(chunks, m.begin(), m.end(), bodyMap); };
		REQUIRE(m.rbegin()->second != 0);
	}

	SECTION("map 10k items")
	{
		std::map<int, uint64_t> m;
		for (int i = 0; i < 10000; ++i)
			m.emplace(i, static_cast<uint64_t>(i + 1));
		BENCHMARK("multi::each (bidirectional)") { multi::each(m.begin(), m.end(), bodyMap); };
		const size_t chunks = (multi::threadCount() + 1) * 4;
		BENCHMARK("multi::each chunks K=4") { multi::each(chunks, m.begin(), m.end(), bodyMap); };
		REQUIRE(m.rbegin()->second != 0);
	}
}

// ---------------------------------------------------------------------------
// reduce_sum — parallel sum of a double vector. Per-element work is trivial
// (one floating add); compares against std::accumulate. The transition
// point (where multi::reduce overtakes the serial baseline) sits between
// per-element memory bandwidth and per-chunk dispatch + combine cost.
// Vary N to see where it lands on your hardware.
// ---------------------------------------------------------------------------
TEST_CASE("reduce_sum", "[bench][fast]")
{
	auto runSerial = [](const std::vector<double>& v) {
		return std::accumulate(v.begin(), v.end(), 0.0);
	};
	auto runMultiDefault = [](const std::vector<double>& v) {
		return multi::reduce(v.begin(), v.end(), 0.0, std::plus<>{});
	};
	auto runMultiOversub = [](const std::vector<double>& v) {
		const std::size_t chunks = (multi::threadCount() + 1) * 4;
		return multi::reduce(chunks, v.begin(), v.end(), 0.0, std::plus<>{});
	};

	SECTION("1k items")
	{
		std::vector<double> v(1000);
		std::iota(v.begin(), v.end(), 1.0);
		BENCHMARK("serial std::accumulate") { return runSerial(v); };
		BENCHMARK("multi::reduce (default chunks)") { return runMultiDefault(v); };
		BENCHMARK("multi::reduce (4x oversub)") { return runMultiOversub(v); };
	}
	SECTION("10k items")
	{
		std::vector<double> v(10000);
		std::iota(v.begin(), v.end(), 1.0);
		BENCHMARK("serial std::accumulate") { return runSerial(v); };
		BENCHMARK("multi::reduce (default chunks)") { return runMultiDefault(v); };
		BENCHMARK("multi::reduce (4x oversub)") { return runMultiOversub(v); };
	}
	SECTION("100k items")
	{
		std::vector<double> v(100000);
		std::iota(v.begin(), v.end(), 1.0);
		BENCHMARK("serial std::accumulate") { return runSerial(v); };
		BENCHMARK("multi::reduce (default chunks)") { return runMultiDefault(v); };
		BENCHMARK("multi::reduce (4x oversub)") { return runMultiOversub(v); };
	}
	SECTION("1M items")
	{
		std::vector<double> v(1000000);
		std::iota(v.begin(), v.end(), 1.0);
		BENCHMARK("serial std::accumulate") { return runSerial(v); };
		BENCHMARK("multi::reduce (default chunks)") { return runMultiDefault(v); };
		BENCHMARK("multi::reduce (4x oversub)") { return runMultiOversub(v); };
	}
}

// ---------------------------------------------------------------------------
// transformReduce_sumOfSquares — per-element x*x followed by +. Adds a
// non-trivial transform op atop the reduce path; useful for sanity-checking
// that the chunked fold inlines the transform cleanly. Baseline is
// std::transform_reduce.
// ---------------------------------------------------------------------------
TEST_CASE("transformReduce_sumOfSquares", "[bench][fast]")
{
	auto sq = [](double x) { return x * x; };

	auto runSerial = [&](const std::vector<double>& v) {
		return std::transform_reduce(v.begin(), v.end(), 0.0, std::plus<>{}, sq);
	};
	auto runMultiDefault = [&](const std::vector<double>& v) {
		return multi::transformReduce(v.begin(), v.end(), 0.0, std::plus<>{}, sq);
	};
	auto runMultiOversub = [&](const std::vector<double>& v) {
		const std::size_t chunks = (multi::threadCount() + 1) * 4;
		return multi::transformReduce(chunks, v.begin(), v.end(), 0.0, std::plus<>{}, sq);
	};

	SECTION("1k items")
	{
		std::vector<double> v(1000);
		std::iota(v.begin(), v.end(), 1.0);
		BENCHMARK("serial std::transform_reduce") { return runSerial(v); };
		BENCHMARK("multi::transformReduce (default chunks)") { return runMultiDefault(v); };
		BENCHMARK("multi::transformReduce (4x oversub)") { return runMultiOversub(v); };
	}
	SECTION("10k items")
	{
		std::vector<double> v(10000);
		std::iota(v.begin(), v.end(), 1.0);
		BENCHMARK("serial std::transform_reduce") { return runSerial(v); };
		BENCHMARK("multi::transformReduce (default chunks)") { return runMultiDefault(v); };
		BENCHMARK("multi::transformReduce (4x oversub)") { return runMultiOversub(v); };
	}
	SECTION("100k items")
	{
		std::vector<double> v(100000);
		std::iota(v.begin(), v.end(), 1.0);
		BENCHMARK("serial std::transform_reduce") { return runSerial(v); };
		BENCHMARK("multi::transformReduce (default chunks)") { return runMultiDefault(v); };
		BENCHMARK("multi::transformReduce (4x oversub)") { return runMultiOversub(v); };
	}
	SECTION("1M items")
	{
		std::vector<double> v(1000000);
		std::iota(v.begin(), v.end(), 1.0);
		BENCHMARK("serial std::transform_reduce") { return runSerial(v); };
		BENCHMARK("multi::transformReduce (default chunks)") { return runMultiDefault(v); };
		BENCHMARK("multi::transformReduce (4x oversub)") { return runMultiOversub(v); };
	}
}

// ---------------------------------------------------------------------------
// transformReduce_expensive — same shape as transformReduce_sumOfSquares but
// with a non-trivial per-element op (mixed trig + sqrt, ~30-60 ns/element).
// Probes the *other* end of the dispatch-cost / per-item-work spectrum: if
// the per-element work is high enough, parallel should win at much smaller
// N than the cheap-op case. Used to size a serial-fallback threshold: a
// threshold tuned only against cheap ops may punt to serial when expensive
// ops would have parallelised profitably.
// ---------------------------------------------------------------------------
TEST_CASE("transformReduce_expensive", "[bench][fast]")
{
	// ~30-60 ns/element on modern hardware — trig + sqrt won't vectorise as
	// tightly as `x*x`, and exercises the chunked fold under realistic
	// "moderately expensive" per-item work.
	auto expensive = [](double x) {
		return std::sin(x) * std::cos(x) + std::sqrt(std::abs(x) + 1.0);
	};

	auto runSerial = [&](const std::vector<double>& v) {
		return std::transform_reduce(v.begin(), v.end(), 0.0, std::plus<>{}, expensive);
	};
	auto runMultiDefault = [&](const std::vector<double>& v) {
		return multi::transformReduce(v.begin(), v.end(), 0.0, std::plus<>{}, expensive);
	};
	auto runMultiOversub = [&](const std::vector<double>& v) {
		const std::size_t chunks = (multi::threadCount() + 1) * 4;
		return multi::transformReduce(chunks, v.begin(), v.end(), 0.0, std::plus<>{}, expensive);
	};

	SECTION("1k items")
	{
		std::vector<double> v(1000);
		std::iota(v.begin(), v.end(), 1.0);
		BENCHMARK("serial std::transform_reduce") { return runSerial(v); };
		BENCHMARK("multi::transformReduce (default chunks)") { return runMultiDefault(v); };
		BENCHMARK("multi::transformReduce (4x oversub)") { return runMultiOversub(v); };
	}
	SECTION("10k items")
	{
		std::vector<double> v(10000);
		std::iota(v.begin(), v.end(), 1.0);
		BENCHMARK("serial std::transform_reduce") { return runSerial(v); };
		BENCHMARK("multi::transformReduce (default chunks)") { return runMultiDefault(v); };
		BENCHMARK("multi::transformReduce (4x oversub)") { return runMultiOversub(v); };
	}
	SECTION("100k items")
	{
		std::vector<double> v(100000);
		std::iota(v.begin(), v.end(), 1.0);
		BENCHMARK("serial std::transform_reduce") { return runSerial(v); };
		BENCHMARK("multi::transformReduce (default chunks)") { return runMultiDefault(v); };
		BENCHMARK("multi::transformReduce (4x oversub)") { return runMultiOversub(v); };
	}
	SECTION("1M items")
	{
		std::vector<double> v(1000000);
		std::iota(v.begin(), v.end(), 1.0);
		BENCHMARK("serial std::transform_reduce") { return runSerial(v); };
		BENCHMARK("multi::transformReduce (default chunks)") { return runMultiDefault(v); };
		BENCHMARK("multi::transformReduce (4x oversub)") { return runMultiOversub(v); };
	}
}

// ---------------------------------------------------------------------------
// sort_random — parallel sort of a random-shuffled int vector vs std::sort.
// Each sample sorts a *fresh* copy (allocated up front, outside the timed
// portion via BENCHMARK_ADVANCED + Chronometer): after the first sort the
// data is in order and a subsequent std::sort would run in O(n), masking
// the actual cost.
//
// Catch2 calls `meter.measure(fn)` exactly `meter.runs()` times, so we
// pre-build one shuffled copy per run. The shuffle is deterministic per
// section (fixed RNG seed) — different sections see different data.
// ---------------------------------------------------------------------------
TEST_CASE("sort_random", "[bench][fast]")
{
	auto buildShuffled = [](std::size_t n, std::uint32_t seed, std::size_t runs) {
		std::vector<std::vector<int>> out(runs);
		std::mt19937 rng(seed);
		std::vector<int> master(n);
		std::iota(master.begin(), master.end(), 0);
		std::shuffle(master.begin(), master.end(), rng);
		for (auto& v : out)
			v = master;
		return out;
	};

	SECTION("10k items")
	{
		BENCHMARK_ADVANCED("serial std::sort")(Catch::Benchmark::Chronometer meter)
		{
			auto data = buildShuffled(10000, 0xA1, meter.runs());
			std::size_t i = 0;
			meter.measure([&]() { std::sort(data[i].begin(), data[i].end()); return ++i; });
		};
		BENCHMARK_ADVANCED("multi::sort")(Catch::Benchmark::Chronometer meter)
		{
			auto data = buildShuffled(10000, 0xA1, meter.runs());
			std::size_t i = 0;
			meter.measure([&]() { multi::sort(data[i].begin(), data[i].end()); return ++i; });
		};
	}

	SECTION("50k items")
	{
		BENCHMARK_ADVANCED("serial std::sort")(Catch::Benchmark::Chronometer meter)
		{
			auto data = buildShuffled(50000, 0xB1, meter.runs());
			std::size_t i = 0;
			meter.measure([&]() { std::sort(data[i].begin(), data[i].end()); return ++i; });
		};
		BENCHMARK_ADVANCED("multi::sort")(Catch::Benchmark::Chronometer meter)
		{
			auto data = buildShuffled(50000, 0xB1, meter.runs());
			std::size_t i = 0;
			meter.measure([&]() { multi::sort(data[i].begin(), data[i].end()); return ++i; });
		};
	}

	SECTION("100k items")
	{
		BENCHMARK_ADVANCED("serial std::sort")(Catch::Benchmark::Chronometer meter)
		{
			auto data = buildShuffled(100000, 0xB2, meter.runs());
			std::size_t i = 0;
			meter.measure([&]() { std::sort(data[i].begin(), data[i].end()); return ++i; });
		};
		BENCHMARK_ADVANCED("multi::sort")(Catch::Benchmark::Chronometer meter)
		{
			auto data = buildShuffled(100000, 0xB2, meter.runs());
			std::size_t i = 0;
			meter.measure([&]() { multi::sort(data[i].begin(), data[i].end()); return ++i; });
		};
	}

	SECTION("500k items")
	{
		BENCHMARK_ADVANCED("serial std::sort")(Catch::Benchmark::Chronometer meter)
		{
			auto data = buildShuffled(500000, 0xC2, meter.runs());
			std::size_t i = 0;
			meter.measure([&]() { std::sort(data[i].begin(), data[i].end()); return ++i; });
		};
		BENCHMARK_ADVANCED("multi::sort")(Catch::Benchmark::Chronometer meter)
		{
			auto data = buildShuffled(500000, 0xC2, meter.runs());
			std::size_t i = 0;
			meter.measure([&]() { multi::sort(data[i].begin(), data[i].end()); return ++i; });
		};
	}

	SECTION("1M items")
	{
		BENCHMARK_ADVANCED("serial std::sort")(Catch::Benchmark::Chronometer meter)
		{
			auto data = buildShuffled(1000000, 0xC3, meter.runs());
			std::size_t i = 0;
			meter.measure([&]() { std::sort(data[i].begin(), data[i].end()); return ++i; });
		};
		BENCHMARK_ADVANCED("multi::sort")(Catch::Benchmark::Chronometer meter)
		{
			auto data = buildShuffled(1000000, 0xC3, meter.runs());
			std::size_t i = 0;
			meter.measure([&]() { multi::sort(data[i].begin(), data[i].end()); return ++i; });
		};
	}

	SECTION("10M items")
	{
		// Verify the chunked-sort path scales sub-linearly out beyond
		// 1M. 10× the items should be substantially less than 10× the
		// time (Phase 4 acceptance criterion: ratio < 12).
		BENCHMARK_ADVANCED("serial std::sort")(Catch::Benchmark::Chronometer meter)
		{
			auto data = buildShuffled(10000000, 0xC4, meter.runs());
			std::size_t i = 0;
			meter.measure([&]() { std::sort(data[i].begin(), data[i].end()); return ++i; });
		};
		BENCHMARK_ADVANCED("multi::sort")(Catch::Benchmark::Chronometer meter)
		{
			auto data = buildShuffled(10000000, 0xC4, meter.runs());
			std::size_t i = 0;
			meter.measure([&]() { multi::sort(data[i].begin(), data[i].end()); return ++i; });
		};
	}
}

// ---------------------------------------------------------------------------
// parallel_pair — repeated multi::parallel(a, b) dispatch with tiny per-task
// work. Puts the N=2 parallel-invoke path on the hot path so any change to
// that path (e.g. running one task inline vs both via the worker pool)
// surfaces clearly. Each task does a small amount of work to avoid the
// compiler eliding the whole expression.
// ---------------------------------------------------------------------------
TEST_CASE("parallel_pair", "[bench][fast]")
{
	auto round = [](std::uint64_t& a, std::uint64_t& b) {
		// Two ~5ns lambdas — small enough that dispatch overhead is the
		// dominant cost, large enough that the compiler can't trivially
		// fold them away.
		multi::parallel(
			[&]() { a = a * 6364136223846793005ULL + 1442695040888963407ULL; },
			[&]() { b = b * 6364136223846793005ULL + 1442695040888963407ULL; });
	};

	SECTION("100 rounds")
	{
		BENCHMARK("multi::parallel(a, b) x100")
		{
			std::uint64_t a = 1, b = 2;
			for (int i = 0; i < 100; ++i) round(a, b);
			return a + b;
		};
	}
	SECTION("1k rounds")
	{
		BENCHMARK("multi::parallel(a, b) x1k")
		{
			std::uint64_t a = 1, b = 2;
			for (int i = 0; i < 1000; ++i) round(a, b);
			return a + b;
		};
	}
}
