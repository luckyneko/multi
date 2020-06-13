/*
 *  Created by LuckyNeko on 30/04/2020.
 *  Copyright 2020 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#ifndef _UTILS_H_
#define _UTILS_H_

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

using Colour = std::array<float, 3>;

// Write PPM
void writePPM(const std::string& filename, uint8_t* rgbBuffer, int width, int height)
{
	FILE* f = fopen(filename.c_str(), "wb");
	fprintf(f, "P6\n%d %d\n255\n", width, height);
	fwrite(rgbBuffer, sizeof(uint8_t), width * height * 3, f);
	fclose(f);
}

// HSV to RGB
void HSVtoRGB(const Colour& hsv, Colour& rgb)
{
	const float h = hsv[0];
	const float s = hsv[1];
	const float v = hsv[2];

	float c = s * v;
	float x = c * (1.0f - fabs(fmod(h / 60.0f, 2) - 1.0f));
	float m = v - c;

	if(h >= 300.0f)
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

#endif // _UTILS_H_