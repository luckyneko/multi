/*
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 *
 *  Each workload stresses a different property of the scheduler:
 *    mandelbrot   — uniform CPU-bound, moderate grain. "Well-behaved".
 *    tiny_tasks   — many very small tasks; reveals per-task overhead.
 *    imbalanced   — task[i] has O(i) work; reveals load-balance quality.
 *    nested       — recursive parallel_invoke tree; reveals nested-safety
 *                   and scaling of fork-join.
 */

#ifndef _BENCH_WORKLOADS_H_
#define _BENCH_WORKLOADS_H_

#include "graph.h"
#include "mandelbrot.h"
#include "methods.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct Workload
{
	std::string name;
	std::string description;
	int totalTaskCount = 0; // used to skip per-task-thread methods on large workloads
	std::function<void(Method&)> run;
};

// --- Mandelbrot frames ---
inline Workload makeMandelbrotWorkload(int width, int height, int numIter, int frames)
{
	auto graph = std::make_shared<Graph>(width, height, 2.0,
										 -(6.01000070505 / 11.0), -(6.01000070505 / 11.0));

	Workload w;
	w.name = "mandelbrot";
	char buf[128];
	std::snprintf(buf, sizeof(buf), "%dx%d, %d iters, %d frames", width, height, numIter, frames);
	w.description = buf;
	w.totalTaskCount = height * frames;
	w.run = [graph, numIter, frames](Method& m)
	{
		const double escapeRadius = 2.0;
		for (int f = 1; f <= frames; ++f)
		{
			double scale = 2.0 / std::pow(1.05, f);
			graph->setScale(scale);
			Graph* gp = graph.get();
			m.parallel_for(0, gp->height(), [gp, escapeRadius, numIter](int y)
						   {
				double Cy = gp->getY(y);
				for (int x = 0; x < gp->width(); ++x)
				{
					double Cx = gp->getX(x);
					int iter = mandelbrotIterations(Cx, Cy, escapeRadius, numIter);
					auto colour = mandelbrotColour(iter, numIter);
					gp->writeColour(colour, x, y);
				} });
		}
	};
	return w;
}

// --- Tiny tasks ---
// Writes a computed value back to a buffer to prevent DCE.
inline Workload makeTinyWorkload(int numTasks, int workPerTask)
{
	auto buf = std::make_shared<std::vector<uint64_t>>(numTasks, 0);
	Workload w;
	w.name = "tiny_tasks";
	char descBuf[128];
	std::snprintf(descBuf, sizeof(descBuf), "%d tasks x %d iters (~per-task overhead)",
				  numTasks, workPerTask);
	w.description = descBuf;
	w.totalTaskCount = numTasks;
	w.run = [buf, numTasks, workPerTask](Method& m)
	{
		uint64_t* out = buf->data();
		m.parallel_for(0, numTasks, [out, workPerTask](int i)
					   {
			uint64_t acc = static_cast<uint64_t>(i);
			for (int k = 0; k < workPerTask; ++k)
				acc = acc * 6364136223846793005ULL + 1442695040888963407ULL;
			out[i] = acc; });
	};
	return w;
}

// --- Imbalanced ---
// Task i does work proportional to i (linear). Late tasks are much longer
// than early ones; static partitioning suffers, dynamic stealing wins.
inline Workload makeImbalancedWorkload(int numTasks, int workUnitScale)
{
	auto buf = std::make_shared<std::vector<uint64_t>>(numTasks, 0);
	Workload w;
	w.name = "imbalanced";
	char descBuf[128];
	std::snprintf(descBuf, sizeof(descBuf), "%d tasks, work ~ i*%d (load-balance)",
				  numTasks, workUnitScale);
	w.description = descBuf;
	w.totalTaskCount = numTasks;
	w.run = [buf, numTasks, workUnitScale](Method& m)
	{
		uint64_t* out = buf->data();
		m.parallel_for(0, numTasks, [out, workUnitScale](int i)
					   {
			int iters = i * workUnitScale;
			uint64_t acc = static_cast<uint64_t>(i) + 1;
			for (int k = 0; k < iters; ++k)
				acc = acc * 6364136223846793005ULL + 1442695040888963407ULL;
			out[i] = acc; });
	};
	return w;
}

// --- Nested recursive tree-sum ---
inline void treeSum(Method& m, int depth, int threshold, int leafWork,
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
	uint64_t a = 0;
	uint64_t b = 0;
	m.parallel_invoke(
		[&m, depth, threshold, leafWork, seed, &a]()
		{
			treeSum(m, depth - 1, threshold, leafWork, seed, a);
		},
		[&m, depth, threshold, leafWork, seed, &b]()
		{
			treeSum(m, depth - 1, threshold, leafWork, seed + 1, b);
		});
	out = a + b;
}

inline Workload makeNestedWorkload(int depth, int threshold, int leafWork)
{
	auto sink = std::make_shared<uint64_t>(0);
	Workload w;
	w.name = "nested";
	char descBuf[128];
	std::snprintf(descBuf, sizeof(descBuf),
				  "depth=%d threshold=%d (%d leaves x %d iters)",
				  depth, threshold, 1 << (depth - threshold), leafWork);
	w.description = descBuf;
	// Count of parallel_invoke calls, which gates thread-per-task methods.
	w.totalTaskCount = (1 << (depth - threshold)) - 1;
	w.run = [sink, depth, threshold, leafWork](Method& m)
	{
		treeSum(m, depth, threshold, leafWork, 1, *sink);
	};
	return w;
}

#endif // _BENCH_WORKLOADS_H_
