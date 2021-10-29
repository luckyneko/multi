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
using ImageSize = std::pair<int, int>;
const ImageSize IMAGE_TALL = {64, 4096};
const ImageSize IMAGE_SQUARE = {512, 512};
const ImageSize IMAGE_WIDE = {4096, 64};

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
	printf(" %-9s | %8.3fs | %8.3fms | %8.3f | %8.3f%%\n",
		   name.c_str(),
		   duration,
		   1000.0 * duration / ZOOM_FRAME_COUNT,
		   double(ZOOM_FRAME_COUNT) / duration,
		   100.0 * maxDuration / (duration * std::thread::hardware_concurrency()));
}

struct Report
{
	ImageSize imageSize;
	double singleDuration;
	double asyncDuration;
	double multiDuration;
	double multiFixedDuration;
	double multiOffDuration;

	void print()
	{
		printf("\n");
		printf("# Test %d jobs of %d size\n", imageSize.second, imageSize.first);
		printf("Image Size: %dx%d\n", imageSize.first, imageSize.second);
		printf("\n");
		printf(" METHOD    | TOTAL     | PER FRAME  | FPS      | UTILIZATION\n");
		printf("-----------|-----------|------------|----------|-------------\n");
		log("single", singleDuration, singleDuration);
		log("async ", asyncDuration, singleDuration);
		log("multi ", multiDuration, singleDuration);
		log("multi32", multiFixedDuration, singleDuration);
		log("multi-off", multiOffDuration, singleDuration);
		printf("-----------|-----------|------------|----------|-------------\n");
	}
};

Report runForSize(const ImageSize& imageSize)
{
	Report out;
	out.imageSize = imageSize;
	Graph g(imageSize.first, imageSize.second, ZOOM_GRAPH_SCALE_START, GRAPH_ORIGIN_X, GRAPH_ORIGIN_Y);

	printf("\n");
	printf("Running benchmarks for %dx%d...\n", imageSize.first, imageSize.second);

	out.singleDuration = run(g, "single", &mandelbrotSingle);
	out.asyncDuration = run(g, "async", &mandelbrotStdAsync);

	multi::start(std::thread::hardware_concurrency() - 1);
	out.multiDuration = run(g, "multi", &mandelbrotMulti);
	out.multiFixedDuration = run(g, "multi32", &mandelbrotMultiFixed<32>);
	multi::stop();
	out.multiOffDuration = run(g, "multi-off", &mandelbrotMulti);

	return out;
}

int main()
{
	std::vector<Report> reports;
	reports.push_back(runForSize(IMAGE_TALL));
	reports.push_back(runForSize(IMAGE_SQUARE));
	reports.push_back(runForSize(IMAGE_WIDE));

	for (auto& report : reports)
		report.print();

	return EXIT_SUCCESS;
}
