/*
 *  Created by LuckyNeko on 20/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <catch2/catch_all.hpp>

#include "workloads.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// each_iter — exercises multi::each over std::vector (random-access iterator)
// and std::map (bidirectional iterator). These hit two different code paths
// in EachJob: random-access stores the iterator directly,
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
		BENCHMARK("multi(items)") { multi::each(v.begin(), v.end(), bodyVec); };
		const size_t chunks = bench::chunkCount(static_cast<int>(v.size()));
		BENCHMARK("multi(chunks)") { multi::each(chunks, v.begin(), v.end(), bodyVec); };
		REQUIRE(v.back() != 0);
	}

	SECTION("vector 50k items")
	{
		std::vector<uint64_t> v(50000);
		for (size_t i = 0; i < v.size(); ++i)
			v[i] = i + 1;
		BENCHMARK("multi(items)") { multi::each(v.begin(), v.end(), bodyVec); };
		const size_t chunks = bench::chunkCount(static_cast<int>(v.size()));
		BENCHMARK("multi(chunks)") { multi::each(chunks, v.begin(), v.end(), bodyVec); };
		REQUIRE(v.back() != 0);
	}

	SECTION("map 1k items")
	{
		std::map<int, uint64_t> m;
		for (int i = 0; i < 1000; ++i)
			m.emplace(i, static_cast<uint64_t>(i + 1));
		BENCHMARK("multi(items)") { multi::each(m.begin(), m.end(), bodyMap); };
		const size_t chunks = bench::chunkCount(static_cast<int>(m.size()));
		BENCHMARK("multi(chunks)") { multi::each(chunks, m.begin(), m.end(), bodyMap); };
		REQUIRE(m.rbegin()->second != 0);
	}

	SECTION("map 50k items")
	{
		std::map<int, uint64_t> m;
		for (int i = 0; i < 50000; ++i)
			m.emplace(i, static_cast<uint64_t>(i + 1));
		BENCHMARK("multi(items)") { multi::each(m.begin(), m.end(), bodyMap); };
		const size_t chunks = bench::chunkCount(static_cast<int>(m.size()));
		BENCHMARK("multi(chunks)") { multi::each(chunks, m.begin(), m.end(), bodyMap); };
		REQUIRE(m.rbegin()->second != 0);
	}
}
