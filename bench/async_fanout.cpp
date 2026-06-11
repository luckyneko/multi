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
			multi::waitAll(h);
		return handles.size();
	};

	SECTION("100 tasks")
	{
		std::vector<multi::Handle<>> handles;
		handles.reserve(100);
		BENCHMARK("multi(async)") { return run(100, handles); };
	}
	SECTION("1k tasks")
	{
		std::vector<multi::Handle<>> handles;
		handles.reserve(1000);
		BENCHMARK("multi(async)") { return run(1000, handles); };
	}
}
