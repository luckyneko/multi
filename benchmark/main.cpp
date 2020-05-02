/*
 *  Created by LuckyNeko on 30/04/2020.
 *  Copyright 2020 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "mandelbrot.h"
#include <chrono>

// Image settings
const int IMAGE_WIDTH = 512;
const int IMAGE_HEIGHT = 512;

// Graph Settings
const double GRAPH_ORIGIN_X = -(6.01000070505 / 11.0);
const double GRAPH_ORIGIN_Y = -(6.01000070505 / 11.0);

// Zoom settings
const double ZOOM_GRAPH_SCALE_START = 2.0;
const double ZOOM_GRAPH_SCALE_END = 0.001;
const int ZOOM_FRAME_COUNT = 100;
const double ZOOM_SCALE = 1.05;

// Mandelbrot Settings
const int NUM_ITER = 256;
const double ESCAPE_RADIUS = 2.0;

template <typename FUNC>
double run(Graph& g, const std::string& name, FUNC&& func, bool writeOutput = false)
{
	printf("\t%-9s...", name.c_str());
	auto start = std::chrono::high_resolution_clock::now();

	for (int frameIdx = 1; frameIdx <= ZOOM_FRAME_COUNT; ++frameIdx)
	{
		double scale = ZOOM_GRAPH_SCALE_START / std::pow(ZOOM_SCALE, frameIdx);
		g.setScale(scale);
		func(g, ESCAPE_RADIUS, NUM_ITER);

		if (writeOutput)
		{
			char filename[50];
			sprintf(filename, "%s-%06d.ppm", name.c_str(), frameIdx);
			writePPM(filename, g.buffer(), g.width(), g.height());
		}
	}

	std::chrono::duration<double> duration = std::chrono::high_resolution_clock::now() - start;
	printf("%8.3fs\n", duration.count());

	return duration.count();
}

void log(const std::string& name, double duration, double maxDuration)
{
	printf(" %-9s | %8.3fs | %8.3fms | %8.3f | %5.3f%%\n",
		   name.c_str(),
		   duration,
		   1000.0 * duration / ZOOM_FRAME_COUNT,
		   double(ZOOM_FRAME_COUNT) / duration,
		   maxDuration / (duration * std::thread::hardware_concurrency()));
}

int main()
{
	multi::start();
	Graph g(IMAGE_WIDTH, IMAGE_HEIGHT, ZOOM_GRAPH_SCALE_START, GRAPH_ORIGIN_X, GRAPH_ORIGIN_Y);

	printf("\n");
	printf("Running benchmarks...\n");

	double singleDuration = run(g, "single", &mandelbrotSingle);
	double asyncDuration = run(g, "async", &mandelbrotStdAsync);
	double multiDuration = run(g, "multi", &mandelbrotMulti);

	// Log Results
	printf("\n");
	printf(" METHOD    | TOTAL     | PER FRAME  | FPS      | UTILIZATION\n");
	printf("-----------|-----------|------------|----------|-------------\n");
	log("single", singleDuration, singleDuration);
	log("async ", asyncDuration, singleDuration);
	log("multi ", multiDuration, singleDuration);

	multi::stop();

	return EXIT_SUCCESS;
}
