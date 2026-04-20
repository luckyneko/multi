/*
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 *
 *  Catch2-based benchmark harness. Uses Catch2 v2's built-in microbenchmark
 *  support (auto-tuning iterations, warmup, outlier classification) to
 *  produce repeatable numbers for each workload. Pool is started once for
 *  the whole session and stopped after all benchmarks run.
 */

#define CATCH_CONFIG_RUNNER
#define CATCH_CONFIG_ENABLE_BENCHMARKING
#include <catch2/catch.hpp>

#include <multi/multi.h>

#include <thread>

int main(int argc, char* argv[])
{
	size_t hw = std::thread::hardware_concurrency();
	if (hw == 0)
		hw = 4;

	// multi uses (hw-1) workers because the caller participates in stealing;
	// matches the old benchmark harness so numbers are comparable.
	multi::start(hw > 0 ? hw - 1 : 1);

	int result = Catch::Session().run(argc, argv);

	multi::stop();
	return result;
}
