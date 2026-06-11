/*
 *  Created by LuckyNeko on 20/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <catch2/catch_all.hpp>

#include "workloads.h"

// ---------------------------------------------------------------------------
// async_latency — serial async round-trips (submit one task, wait, repeat).
// Measures Handle + AsyncState lifecycle cost per dispatch.
// ---------------------------------------------------------------------------
TEST_CASE("async_latency", "[bench][fast]")
{
	BENCHMARK("multi(async)")
	{
		for (int i = 0; i < 1000; ++i)
			multi::async([]() {}).wait();
		return 1000;
	};
}
