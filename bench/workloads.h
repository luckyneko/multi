/*
 *  Created by LuckyNeko on 20/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 *
 *  Shared helpers for the bench-multi workloads.  Each workload lives in its
 *  own translation unit (empty_tasks.cpp, mandelbrot.cpp, …) and includes this
 *  header for the parallel-for dispatch adaptors below.
 *
 *  Tags:
 *    [fast]     – total runtime ~seconds; safe for iterative dev
 *    [slow]     – runtime ~minutes; run when committing perf work
 *    [baseline] – serial reference; shows speedup but adds time
 *
 *  Benchmark naming — every BENCHMARK label is "impl(variant)", no space:
 *    serial(baseline)  – pure serial reference (report.py keys the speedup
 *                        column off the leading "serial")
 *    multi(items)      – one task per element (PerItem range / each)
 *    multi(chunks)     – chunked dispatch, CHUNK_FACTOR oversubscription
 *    multi(parallel)   – fork/join via multi::parallel
 *    multi(async)      – multi::async
 *    multi(range)      – externally-driven multi::range
 *    multi(recipe)     – multi::Recipe DAG execution
 *  The enclosing TEST_CASE / SECTION carries the size and shape, so labels
 *  stay short and comparable across workloads.
 *
 *  Typical invocations:
 *    ./bench-multi "[fast]"                  # quick iteration
 *    ./bench-multi "[bench]"                 # everything
 *    ./bench-multi --benchmark-samples=30    # faster, fewer samples
 *    ./bench-multi --reporter xml --out r.xml
 */

#pragma once

#include <multi/multi.h>

#include <cstddef>
#include <utility>

namespace bench
{
	// Colour
	using Colour = std::array<float, 3>;

	// Graph class
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

	// ---------------------------------------------------------------------------
	// Parallel helpers — functor objects so generic lambdas can accept them
	// without a std::function wrapper in the bench harness.  The only
	// std::function allocation happens inside multi::range / multi::parallel
	// at the library boundary.
	// ---------------------------------------------------------------------------

	struct parallel_for_baseline_t
	{
		template <class F>
		void operator()(int begin, int end, F&& f) const
		{
			for (int i = begin; i < end; ++i)
				f(i);
		}
	};

	struct parallel_for_items_t
	{
		template <class F>
		void operator()(int begin, int end, F&& f) const
		{
			multi::range(begin, end, 1, std::forward<F>(f));
		}
	};

	// Target chunk count for a parallel-for over `count` items, clamped to [2, count].
	// Mirrors multi::Auto; CHUNK_FACTOR is the single source of truth in multi/chunkpolicy.h.
	inline std::size_t chunkCount(int count)
	{
		std::size_t chunks = (multi::threadCount() + 1) * multi::CHUNK_FACTOR;
		if (chunks < 2)
			chunks = 2;
		if (static_cast<int>(chunks) > count)
			chunks = static_cast<std::size_t>(count);
		return chunks;
	}

	struct parallel_for_chunks_t
	{
		template <class F>
		void operator()(int begin, int end, F&& f) const
		{
			int count = end - begin;
			if (count <= 0)
				return;
			multi::range(chunkCount(count), begin, end, 1, std::forward<F>(f));
		}
	};

	struct parallel_invoke_t
	{
		template <class A, class B>
		void operator()(A&& a, B&& b) const
		{
			multi::parallel(multi::details::Task(std::forward<A>(a)), multi::details::Task(std::forward<B>(b)));
		}
	};

	inline constexpr parallel_for_baseline_t baseline{};
	inline constexpr parallel_for_items_t items{};
	inline constexpr parallel_for_chunks_t chunks{};
	inline constexpr parallel_invoke_t invoke_multi{};

} // namespace bench
