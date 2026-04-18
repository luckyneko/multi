/*
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 *
 *  Unified threading-method adapters. Each Method exposes the same two
 *  primitives (parallel_for over a range of ints, and parallel_invoke for
 *  two tasks), implemented on top of: the calling thread only, std::async
 *  with std::launch::async, std::thread per task, a naive persistent pool,
 *  or multi. Workloads are written once and run against every method.
 */

#ifndef _BENCH_METHODS_H_
#define _BENCH_METHODS_H_

#include "simple_pool.h"

#include <multi/multi.h>

#include <functional>
#include <future>
#include <string>
#include <thread>
#include <vector>

struct Method
{
	std::string name;
	// Flags to let workloads skip methods that would take absurd time
	// (e.g. spawning 100k std::threads for a tiny-task workload).
	bool perTaskThreadSpawn = false;
	// A naive fixed-pool deadlocks when recursion depth approaches worker
	// count (every worker stuck in wait_all), so skip it on nested.
	bool nestedSafe = true;

	std::function<void(int, int, std::function<void(int)>)> parallel_for;
	std::function<void(std::function<void()>, std::function<void()>)> parallel_invoke;
};

inline std::vector<Method> buildMethods(SimplePool& pool)
{
	std::vector<Method> methods;

	// --- single-threaded baseline ---
	{
		Method m;
		m.name = "single";
		m.parallel_for = [](int begin, int end, std::function<void(int)> f)
		{
			for (int i = begin; i < end; ++i)
				f(i);
		};
		m.parallel_invoke = [](std::function<void()> a, std::function<void()> b)
		{
			a();
			b();
		};
		methods.push_back(std::move(m));
	}

	// --- std::async with launch::async (forces async execution) ---
	// The original benchmark used plain std::async, whose implementation is
	// free to defer to synchronous; that's the big correctness fix.
	{
		Method m;
		m.name = "std::async";
		m.perTaskThreadSpawn = true;
		m.parallel_for = [](int begin, int end, std::function<void(int)> f)
		{
			std::vector<std::future<void>> futs;
			futs.reserve(static_cast<size_t>(end - begin));
			for (int i = begin; i < end; ++i)
				futs.emplace_back(std::async(std::launch::async, [f, i]()
											 { f(i); }));
			for (auto& fut : futs)
				fut.wait();
		};
		m.parallel_invoke = [](std::function<void()> a, std::function<void()> b)
		{
			auto fut = std::async(std::launch::async, std::move(a));
			b();
			fut.wait();
		};
		methods.push_back(std::move(m));
	}

	// --- std::thread per task (isolates thread-creation cost) ---
	{
		Method m;
		m.name = "std::thread";
		m.perTaskThreadSpawn = true;
		m.parallel_for = [](int begin, int end, std::function<void(int)> f)
		{
			std::vector<std::thread> ts;
			ts.reserve(static_cast<size_t>(end - begin));
			for (int i = begin; i < end; ++i)
				ts.emplace_back([f, i]()
								{ f(i); });
			for (auto& t : ts)
				t.join();
		};
		m.parallel_invoke = [](std::function<void()> a, std::function<void()> b)
		{
			std::thread t(std::move(a));
			b();
			t.join();
		};
		methods.push_back(std::move(m));
	}

	// --- persistent naive pool (mutex+condvar FIFO) ---
	{
		Method m;
		m.name = "simple_pool";
		m.nestedSafe = false;
		SimplePool* p = &pool;
		m.parallel_for = [p](int begin, int end, std::function<void(int)> f)
		{
			for (int i = begin; i < end; ++i)
				p->submit([f, i]()
						  { f(i); });
			p->wait_all();
		};
		// Run one side on the caller (avoids per-invoke thread-creation)
		// but makes deep recursion risky — see simple_pool.h.
		m.parallel_invoke = [p](std::function<void()> a, std::function<void()> b)
		{
			p->submit(std::move(a));
			b();
			p->wait_all();
		};
		methods.push_back(std::move(m));
	}

	// --- multi: one task per item (fine-grained) ---
	{
		Method m;
		m.name = "multi(items)";
		m.parallel_for = [](int begin, int end, std::function<void(int)> f)
		{
			multi::range(begin, end, 1, [f](int i)
						 { f(i); });
		};
		m.parallel_invoke = [](std::function<void()> a, std::function<void()> b)
		{
			multi::parallel(multi::Task(std::move(a)), multi::Task(std::move(b)));
		};
		methods.push_back(std::move(m));
	}

	// --- multi: chunked (K tasks per worker) ---
	{
		Method m;
		m.name = "multi(chunks)";
		m.parallel_for = [](int begin, int end, std::function<void(int)> f)
		{
			int count = end - begin;
			if (count <= 0)
				return;
			const size_t K = 8;
			size_t chunks = (multi::threadCount() + 1) * K;
			if (chunks < 2)
				chunks = 2;
			if (static_cast<int>(chunks) > count)
				chunks = static_cast<size_t>(count);
			multi::range(chunks, begin, end, 1, [f](int i)
						 { f(i); });
		};
		m.parallel_invoke = [](std::function<void()> a, std::function<void()> b)
		{
			multi::parallel(multi::Task(std::move(a)), multi::Task(std::move(b)));
		};
		methods.push_back(std::move(m));
	}

	return methods;
}

#endif // _BENCH_METHODS_H_
