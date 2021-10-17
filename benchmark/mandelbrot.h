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

#include <multi/multi.h>
#include <vector>

//------------------------------------------------------------------------------
// Mandelbrot algorithm
// https://rosettacode.org/wiki/Mandelbrot_set#C
int mandelbrotIterations(double x, double y, double er, int numIter)
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

Colour mandelbrotColour(int iter, int numIter)
{
	Colour hsv = {
		360.0f * float(iter) / numIter,
		1.0f,
		iter < numIter ? 1.0f : 0.0f};
	Colour rgb = {0.0f, 0.0f, 0.0f};
	HSVtoRGB(hsv, rgb);
	return rgb;
}

//------------------------------------------------------------------------------
// Single Threaded
void mandelbrotSingle(Graph& g, double er, int numIter)
{
	for (int y = 0; y < g.height(); ++y)
	{
		double Cy = g.getY(y);
		for (int x = 0; x < g.width(); ++x)
		{
			double Cx = g.getX(x);
			int iter = mandelbrotIterations(Cx, Cy, er, numIter);
			auto colour = mandelbrotColour(iter, numIter);
			g.writeColour(colour, x, y);
		}
	}
}

//------------------------------------------------------------------------------
// std::async
void mandelbrotStdAsync(Graph& g, double er, int numIter)
{
	std::vector<std::future<void>> hndls;
	hndls.resize(g.height());

	for (int y = 0; y < (int)g.height(); ++y)
	{
		Graph* gp = &g;
		hndls[y] = std::async([gp, er, numIter, y]()
							  {
								  double Cy = gp->getY(y);
								  for (int x = 0; x < gp->width(); ++x)
								  {
									  double Cx = gp->getX(x);
									  int iter = mandelbrotIterations(Cx, Cy, er, numIter);
									  auto colour = mandelbrotColour(iter, numIter);
									  gp->writeColour(colour, x, y);
								  }
							  });
	}

	for (size_t i = 0; i < hndls.size(); ++i)
		hndls[i].wait();
}

//------------------------------------------------------------------------------
// multi
void mandelbrotMulti(Graph& g, double er, int numIter)
{
	Graph* gp = &g;
	multi::range(0, gp->height(), 1,
				 [gp, er, numIter](int y)
				 {
					 double Cy = gp->getY(y);
					 for (int x = 0; x < gp->width(); ++x)
					 {
						 double Cx = gp->getX(x);
						 int iter = mandelbrotIterations(Cx, Cy, er, numIter);
						 auto colour = mandelbrotColour(iter, numIter);
						 gp->writeColour(colour, x, y);
					 }
				 });
}

#endif // _MANDELBROT_H_