/*
 *  Created by LuckyNeko on 20/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <catch2/catch_all.hpp>

#include "workloads.h"

#include <vector>

using namespace bench;

// ---------------------------------------------------------------------------
// empty_tasks — pure dispatch overhead, ~no work per task.
// No baseline BENCHMARK: an empty serial for-loop is DCE'd by the compiler
// and would report ~0, which is not a meaningful comparison point.
// ---------------------------------------------------------------------------
TEST_CASE("empty_tasks", "[bench][fast]")
{
	auto run = [](auto pf, std::vector<int>& buf)
	{
		int* out = buf.data();
		const int n = static_cast<int>(buf.size());
		pf(0, n, [out](int i)
		   { out[i] = i; });
	};

	SECTION("1k tasks")
	{
		std::vector<int> buf(1000, 0);
		BENCHMARK("multi(items)") { run(items, buf); };
		BENCHMARK("multi(chunks)") { run(chunks, buf); };
		REQUIRE(buf.back() == 999);
	}
	SECTION("50k tasks")
	{
		std::vector<int> buf(50000, 0);
		BENCHMARK("multi(items)") { run(items, buf); };
		BENCHMARK("multi(chunks)") { run(chunks, buf); };
		REQUIRE(buf.back() == 49999);
	}
}
