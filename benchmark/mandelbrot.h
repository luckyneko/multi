/*
 *  Created by LuckyNeko on 30/04/2020.
 *  Copyright 2020 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#ifndef _MANDELBROT_H_
#define _MANDELBROT_H_

#include "graph.h"
#include "utils.h"

// https://rosettacode.org/wiki/Mandelbrot_set#C
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
	};

	return iter;
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

#endif // _MANDELBROT_H_
