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

using namespace bench;

namespace
{
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
// nested — recursive fork-join tree via parallel_invoke. Serial baseline
// included; adds ~1.3s but shows the fork-join speedup directly.
// ---------------------------------------------------------------------------
TEST_CASE("nested", "[bench][fast]")
{
	constexpr int depth = 12, threshold = 6, leafWork = 200000;

	uint64_t sink = 0;

	BENCHMARK("serial(baseline)")
	{
		treeSumSerial(depth, threshold, leafWork, 1, sink);
		return sink;
	};
	BENCHMARK("multi(parallel)")
	{
		treeSum(depth, threshold, leafWork, 1, sink);
		return sink;
	};

	REQUIRE(sink != 0);
}
