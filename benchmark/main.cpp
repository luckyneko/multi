/*
 *  Created by LuckyNeko on 30/04/2020.
 *  Copyright 2020 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 *
 *  Benchmark driver: runs every workload against every method, with warmup
 *  + repeats, and reports min / median / stdev alongside speedup over the
 *  single-threaded baseline. Methods flagged as per-task-thread-spawning
 *  are skipped for workloads above a task-count cap.
 */

#include "methods.h"
#include "simple_pool.h"
#include "stats.h"
#include "workloads.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

namespace
{
	const int WARMUP_RUNS = 2;
	const int MEASURED_RUNS = 5;

	// Skip std::async / std::thread when a workload submits more than this
	// many tasks. A per-task-thread-spawn method with 100k tasks is useful
	// as a cautionary data point, but 100k OS threads per run will crash
	// or take minutes; 1k is enough to show the cost without that.
	const int PER_TASK_THREAD_CAP = 2000;

	struct Cell
	{
		std::string method;
		Stats stats;
		bool skipped = false;
	};

	void printHeader(const Workload& wl, size_t threadCount)
	{
		std::printf("# Workload: %s  [%s]\n", wl.name.c_str(), wl.description.c_str());
		std::printf("  threads=%zu  warmup=%d  repeats=%d\n",
					threadCount, WARMUP_RUNS, MEASURED_RUNS);
		std::printf(" %-16s | %10s | %10s | %10s | %9s | %9s\n",
					"METHOD", "MIN (ms)", "MEDIAN", "MEAN", "STDEV", "SPEEDUP");
		std::printf("------------------|------------|------------|------------|-----------|----------\n");
	}

	void printRow(const Cell& cell, double baselineMedian)
	{
		if (cell.skipped)
		{
			std::printf(" %-16s | %10s | %10s | %10s | %9s | %9s\n",
						cell.method.c_str(), "(skipped)", "", "", "", "");
			return;
		}
		double speedup = (baselineMedian > 0.0)
							 ? baselineMedian / cell.stats.median_ms
							 : 0.0;
		std::printf(" %-16s | %10.3f | %10.3f | %10.3f | %9.3f | %8.2fx\n",
					cell.method.c_str(),
					cell.stats.min_ms,
					cell.stats.median_ms,
					cell.stats.mean_ms,
					cell.stats.stdev_ms,
					speedup);
	}
} // namespace

int main()
{
	// Line-buffer stdout so progress is visible when piped or redirected.
	std::setvbuf(stdout, nullptr, _IOLBF, 0);

	size_t hw = std::thread::hardware_concurrency();
	if (hw == 0)
		hw = 4;

	std::printf("multi benchmark\n");
	std::printf("===============\n");
	std::printf("hardware_concurrency = %zu\n\n", hw);

	// multi uses (hw-1) workers because waitRun() executes on the caller,
	// giving it hw effective parallel threads. Other methods use hw.
	SimplePool simplePool(hw);
	multi::start(hw > 0 ? hw - 1 : 1);

	auto methods = buildMethods(simplePool);

	std::vector<Workload> workloads;
	workloads.push_back(makeMandelbrotWorkload(512, 512, 256, 10));
	workloads.push_back(makeTinyWorkload(5000, 500));
	workloads.push_back(makeImbalancedWorkload(200, 8000));
	workloads.push_back(makeNestedWorkload(12, 6, 200000));

	for (auto& wl : workloads)
	{
		printHeader(wl, hw);

		std::vector<Cell> cells;
		cells.reserve(methods.size());

		const bool isNested = (wl.name == "nested");

		for (auto& m : methods)
		{
			Cell c;
			c.method = m.name;
			const bool skipForThreadSpawn =
				m.perTaskThreadSpawn && wl.totalTaskCount > PER_TASK_THREAD_CAP;
			const bool skipForNested = isNested && !m.nestedSafe;
			if (skipForThreadSpawn || skipForNested)
			{
				c.skipped = true;
			}
			else
			{
				c.stats = measure(WARMUP_RUNS, MEASURED_RUNS,
								  [&]()
								  { wl.run(m); });
			}
			cells.push_back(std::move(c));
		}

		// Baseline = single-threaded median; used for speedup column.
		double baseline = 0.0;
		for (const auto& c : cells)
		{
			if (!c.skipped && c.method == "single")
			{
				baseline = c.stats.median_ms;
				break;
			}
		}

		for (const auto& c : cells)
			printRow(c, baseline);

		std::printf("\n");
	}

	multi::stop();
	return EXIT_SUCCESS;
}
