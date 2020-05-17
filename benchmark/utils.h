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
	float x = c * (1 - abs(fmod(h / 60.0, 2) - 1));
	float m = v - c;

	if (h >= 0 && h < 60)
		rgb = {c + m, x + m, 0 + m};
	else if (h >= 60 && h < 120)
		rgb = {x + m, c + m, 0 + m};
	else if (h >= 120 && h < 180)
		rgb = {0 + m, c + m, x + m};
	else if (h >= 180 && h < 240)
		rgb = {0 + m, x + m, c + m};
	else if (h >= 240 && h < 300)
		rgb = {x + m, 0 + m, c + m};
	else
		rgb = {c + m, 0 + m, x + m};
}

#endif // _UTILS_H_