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
		BENCHMARK("multi(parallel)")
		{
			std::uint64_t a = 1, b = 2;
			for (int i = 0; i < 100; ++i) round(a, b);
			return a + b;
		};
	}
	SECTION("1k rounds")
	{
		BENCHMARK("multi(parallel)")
		{
			std::uint64_t a = 1, b = 2;
			for (int i = 0; i < 1000; ++i) round(a, b);
			return a + b;
		};
	}
}
