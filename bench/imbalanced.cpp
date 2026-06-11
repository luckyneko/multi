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
		BENCHMARK("serial(baseline)") { run(baseline, buf); };
		BENCHMARK("multi(items)") { run(items, buf); };
		BENCHMARK("multi(chunks)") { run(chunks, buf); };
		REQUIRE(buf.back() != 0);
	}
	SECTION("200 tasks")
	{
		std::vector<uint64_t> buf(200, 0);
		BENCHMARK("serial(baseline)") { run(baseline, buf); };
		BENCHMARK("multi(items)") { run(items, buf); };
		BENCHMARK("multi(chunks)") { run(chunks, buf); };
		REQUIRE(buf.back() != 0);
	}
}
