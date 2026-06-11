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
	SECTION("50k tasks")
	{
		std::vector<uint64_t> buf(50000, 0);
		BENCHMARK("multi(items)") { run(items, buf); };
		BENCHMARK("multi(chunks)") { run(chunks, buf); };
		REQUIRE(buf.back() != 0);
	}
}
