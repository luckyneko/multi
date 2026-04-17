/*
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#ifndef _BENCH_STATS_H_
#define _BENCH_STATS_H_

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <vector>

struct Stats
{
	double min_ms = 0.0;
	double median_ms = 0.0;
	double mean_ms = 0.0;
	double stdev_ms = 0.0;
	int repeats = 0;
};

template <typename FUNC>
Stats measure(int warmup, int repeats, FUNC&& func)
{
	for (int i = 0; i < warmup; ++i)
		func();

	std::vector<double> samples;
	samples.reserve(repeats);
	for (int i = 0; i < repeats; ++i)
	{
		auto t0 = std::chrono::high_resolution_clock::now();
		func();
		auto t1 = std::chrono::high_resolution_clock::now();
		samples.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
	}

	Stats s;
	s.repeats = repeats;
	std::sort(samples.begin(), samples.end());
	s.min_ms = samples.front();
	s.median_ms = samples[samples.size() / 2];

	double sum = 0.0;
	for (double v : samples)
		sum += v;
	s.mean_ms = sum / static_cast<double>(samples.size());

	double var = 0.0;
	for (double v : samples)
	{
		const double d = v - s.mean_ms;
		var += d * d;
	}
	s.stdev_ms = std::sqrt(var / static_cast<double>(samples.size()));
	return s;
}

#endif // _BENCH_STATS_H_
