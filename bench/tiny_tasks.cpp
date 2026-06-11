/*
 *  Created by LuckyNeko on 20/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <catch2/catch_all.hpp>

#include "workloads.h"

#include <cstdint>
#include <vector>

using namespace bench;

// ---------------------------------------------------------------------------
// tiny_tasks — many short tasks; reveals per-task scheduling overhead.
// Two task counts (1k, 50k) bracket the scaling: dispatch-dominated at the
// low end, work-dominated at the high end.
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
		BENCHMARK("serial(baseline)") { run(baseline, buf); };
		BENCHMARK("multi(items)") { run(items, buf); };
		BENCHMARK("multi(chunks)") { run(chunks, buf); };
		REQUIRE(buf.back() != 0);
	}
	SECTION("50k tasks")
	{
		std::vector<uint64_t> buf(50000, 0);
		BENCHMARK("serial(baseline)") { run(baseline, buf); };
		BENCHMARK("multi(items)") { run(items, buf); };
		BENCHMARK("multi(chunks)") { run(chunks, buf); };
		REQUIRE(buf.back() != 0);
	}
}
