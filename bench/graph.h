/*
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#ifndef _BENCH_GRAPH_H_
#define _BENCH_GRAPH_H_

#include "utils.h"

#include <cstdint>
#include <cstdlib>

class Graph
{
public:
	Graph(int w, int h, double s, double originX, double originY)
	{
		m_width = w;
		m_height = h;
		m_rgbBuffer = static_cast<uint8_t*>(std::malloc(static_cast<size_t>(m_width) * m_height * 3));
		m_originX = originX;
		m_originY = originY;

		setScale(s);
	}

	~Graph()
	{
		std::free(m_rgbBuffer);
	}

	void setScale(double s)
	{
		m_scale = s;
		m_pixelSize = m_scale / m_width;
		m_graphXMin = m_originX - ((m_pixelSize * m_width) / 2.0);
		m_graphYMin = m_originY - ((m_pixelSize * m_height) / 2.0);
	}

	double getX(int x) { return m_graphXMin + x * m_pixelSize; }
	double getY(int y) { return m_graphYMin + y * m_pixelSize; }
	int width() { return m_width; }
	int height() { return m_height; }
	uint8_t* buffer() { return m_rgbBuffer; }
	void writeColour(const Colour& rgb, int x, int y)
	{
		int idx = ((y * m_width) + x) * 3;
		m_rgbBuffer[idx + 0] = uint8_t(rgb[0] * 255);
		m_rgbBuffer[idx + 1] = uint8_t(rgb[1] * 255);
		m_rgbBuffer[idx + 2] = uint8_t(rgb[2] * 255);
	}

private:
	int m_width;
	int m_height;
	uint8_t* m_rgbBuffer;
	double m_originX;
	double m_originY;

	double m_scale;
	double m_pixelSize;
	double m_graphXMin;
	double m_graphYMin;
};

#endif // _BENCH_GRAPH_H_
