/*
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#ifndef _BENCH_UTILS_H_
#define _BENCH_UTILS_H_

#include <array>
#include <cmath>
#include <cstdint>

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

#endif // _BENCH_UTILS_H_
