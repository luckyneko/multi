/*
 *  Created by LuckyNeko on 20/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <catch2/catch_all.hpp>

#include "workloads.h"

#include <cmath>
#include <cstdint>

using namespace bench;

inline int mandelbrotIterations(double x, double y, double er, int numIter)
{
	double Zx = 0.0;
	double Zy = 0.0;
	double Zx2 = Zx * Zx;
	double Zy2 = Zy * Zy;

	int iter = 0;
	while (iter < numIter && (Zx2 + Zy2) < (er * er))
	{
		Zy = 2 * Zx * Zy + y;
		Zx = Zx2 - Zy2 + x;
		Zx2 = Zx * Zx;
		Zy2 = Zy * Zy;
		++iter;
	}

	return iter;
}

using Colour = std::array<float, 3>;

inline void HSVtoRGB(const Colour& hsv, Colour& rgb)
{
	const float h = hsv[0];
	const float s = hsv[1];
	const float v = hsv[2];

	float c = s * v;
	float x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
	float m = v - c;

	if (h >= 300.0f)
		rgb = {c + m, 0.0f + m, x + m};
	else if (h >= 240.0f)
		rgb = {x + m, 0.0f + m, c + m};
	else if (h >= 180.0f)
		rgb = {0.0f + m, x + m, c + m};
	else if (h >= 120.0f)
		rgb = {0.0f + m, c + m, x + m};
	else if (h >= 60.0f)
		rgb = {x + m, c + m, 0.0f + m};
	else
		rgb = {c + m, x + m, 0.0f + m};
}

inline Colour mandelbrotColour(int iter, int numIter)
{
	Colour hsv = {
		360.0f * float(iter) / numIter,
		1.0f,
		iter < numIter ? 1.0f : 0.0f};
	Colour rgb = {0.0f, 0.0f, 0.0f};
	HSVtoRGB(hsv, rgb);
	return rgb;
}

// ---------------------------------------------------------------------------
// mandelbrot — uniform CPU-bound work, moderate grain. "Well-behaved".
// ---------------------------------------------------------------------------
TEST_CASE("mandelbrot", "[bench][slow]")
{
	const int width = 512, height = 512, numIter = 256, frames = 10;
	Graph graph(width, height, 2.0,
				-(6.01000070505 / 11.0), -(6.01000070505 / 11.0));

	auto run = [&](auto pf)
	{
		constexpr double escapeRadius = 2.0;
		for (int f = 1; f <= frames; ++f)
		{
			graph.setScale(2.0 / std::pow(1.05, f));
			Graph* gp = &graph;
			pf(0, gp->height(), [gp, numIter](int y)
			   {
				constexpr double er = 2.0;
				double Cy = gp->getY(y);
				for (int x = 0; x < gp->width(); ++x)
				{
					double Cx = gp->getX(x);
					gp->writeColour(mandelbrotColour(mandelbrotIterations(Cx, Cy, er, numIter), numIter), x, y);
				} });
		}
	};

	BENCHMARK("serial(baseline)") { run(baseline); };
	BENCHMARK("multi(items)") { run(items); };
	BENCHMARK("multi(chunks)") { run(chunks); };

	const uint8_t* pix = graph.buffer();
	REQUIRE((pix[0] | pix[1] | pix[2]) != 0);
}
