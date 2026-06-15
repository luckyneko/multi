/*
 *  Created by LuckyNeko on 14/06/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <catch2/catch_all.hpp>
#include <multi/chunkpolicy.h>

using multi::ChunkPolicy;
using Policy = multi::ChunkPolicy::Policy;

// resolve() is constexpr; STATIC_REQUIRE asserts each case at both compile time
// and run time, so the constexpr contract is enforced by the build itself.

TEST_CASE("ChunkPolicy: Manual is an exact count clamped to [1, total]")
{
	STATIC_REQUIRE(ChunkPolicy(64).resolve(1000, 4) == 64); // exact when it fits
	STATIC_REQUIRE(ChunkPolicy(64).resolve(10, 4) == 10);	// clamped down to total
	STATIC_REQUIRE(ChunkPolicy(1).resolve(10, 4) == 1);
	STATIC_REQUIRE(ChunkPolicy(0).resolve(10, 4) == 1); // 0 normalises to 1

	// Bare integers convert implicitly, so each(64, ...) / range(8, ...) compile.
	constexpr ChunkPolicy implicitCount = 16;
	STATIC_REQUIRE(implicitCount.resolve(1000, 4) == 16);
}

TEST_CASE("ChunkPolicy: Auto oversubscribes by CHUNK_FACTOR, clamped to [2, total]")
{
	// (workers + 1) * CHUNK_FACTOR, tied to the constant rather than hardcoded.
	STATIC_REQUIRE(multi::Auto.resolve(10000, 3) == (3 + 1) * multi::CHUNK_FACTOR);
	STATIC_REQUIRE(multi::Auto.resolve(10000, 0) == (0 + 1) * multi::CHUNK_FACTOR);

	// Clamped down when the workload is smaller than the desired task count.
	STATIC_REQUIRE(multi::Auto.resolve(5, 100) == 5);
	STATIC_REQUIRE(multi::Auto.resolve(2, 100) == 2);

	// Regression: total == 1 used to evaluate std::clamp(v, 2, 1) — undefined
	// behaviour (lo > hi). Must be well-defined and yield 1.
	STATIC_REQUIRE(multi::Auto.resolve(1, 8) == 1);

	// The >= 2 floor: a custom factor small enough to want < 2 tasks still gives 2.
	STATIC_REQUIRE(ChunkPolicy(Policy::Auto, 1).resolve(1000, 0) == 2);
	// A custom factor scales oversubscription (Policy is public for this usage).
	STATIC_REQUIRE(ChunkPolicy(Policy::Auto, 2).resolve(1000, 3) == (3 + 1) * 2);
}

TEST_CASE("ChunkPolicy: PerItem is one task per item, ignoring workers")
{
	STATIC_REQUIRE(multi::PerItem.resolve(7, 4) == 7);
	STATIC_REQUIRE(multi::PerItem.resolve(7, 999) == 7);
	STATIC_REQUIRE(multi::PerItem.resolve(1, 4) == 1);
}

TEST_CASE("ChunkPolicy: empty workload resolves to 0 for every policy")
{
	STATIC_REQUIRE(ChunkPolicy(64).resolve(0, 4) == 0);
	STATIC_REQUIRE(multi::Auto.resolve(0, 4) == 0);
	STATIC_REQUIRE(multi::PerItem.resolve(0, 4) == 0);
}
