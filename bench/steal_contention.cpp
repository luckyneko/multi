/*
 *  Created by LuckyNeko on 20/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <catch2/catch_all.hpp>

#include "workloads.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// steal_contention — multiple external driver threads issue parallel work
// simultaneously. While each driver waits for its own batch it spins on
// tryStealAny. Without victim-index rotation, every driver hits worker 0's
// deque mutex first on each scan, concentrating contention on a single
// cache line. The async variant pushes the contention harder: each
// Handle::wait re-enters the steal loop on every round-trip with no chunked
// work for the driver to actually steal — pure scan overhead.
//
// The driver threads are spawned ONCE per section (in DriverPool's ctor,
// outside the timed region) and released per sample through a generation
// barrier. Spawning/joining inside the BENCHMARK body would fold tens of
// microseconds of OS thread-lifecycle cost — and its large variance — into
// every measurement, drowning out the steal-loop contention we want to see.
// ---------------------------------------------------------------------------
namespace
{
	// A fixed set of worker threads that each run `work(driverId)` once per
	// round. runRound() releases them all and blocks until the last finishes,
	// so a Catch2 sample measures only the work + the round's wake/join sync,
	// never thread creation.
	class DriverPool
	{
	public:
		DriverPool(int numDrivers, std::function<void(int)> work)
			: m_work(std::move(work))
		{
			m_threads.reserve(static_cast<size_t>(numDrivers));
			for (int d = 0; d < numDrivers; ++d)
				m_threads.emplace_back([this, d]() { loop(d); });
		}

		~DriverPool()
		{
			{
				std::lock_guard<std::mutex> lk(m_mx);
				m_stop = true;
				++m_generation;
			}
			m_cvGo.notify_all();
			for (auto& t : m_threads)
				t.join();
		}

		DriverPool(const DriverPool&) = delete;
		DriverPool& operator=(const DriverPool&) = delete;

		// Run one round across all drivers and wait for completion.
		void runRound()
		{
			{
				std::lock_guard<std::mutex> lk(m_mx);
				m_pending = static_cast<int>(m_threads.size());
				++m_generation;
			}
			m_cvGo.notify_all();

			std::unique_lock<std::mutex> lk(m_mx);
			m_cvDone.wait(lk, [this]() { return m_pending == 0; });
		}

	private:
		void loop(int driverId)
		{
			uint64_t seen = 0;
			for (;;)
			{
				std::unique_lock<std::mutex> lk(m_mx);
				m_cvGo.wait(lk, [this, &seen]() { return m_generation != seen; });
				seen = m_generation;
				if (m_stop)
					return;
				lk.unlock();

				m_work(driverId);

				lk.lock();
				if (--m_pending == 0)
					m_cvDone.notify_one();
			}
		}

		std::vector<std::thread> m_threads;
		std::function<void(int)> m_work;
		std::mutex m_mx;
		std::condition_variable m_cvGo;
		std::condition_variable m_cvDone;
		uint64_t m_generation = 0;
		int m_pending = 0;
		bool m_stop = false;
	};
} // namespace

TEST_CASE("steal_contention", "[bench][fast]")
{
	std::atomic<uint64_t> sink(0);

	auto rangeWork = [&sink](int callsPerDriver, int chunksPerCall, int itemsPerCall)
	{
		return [&sink, callsPerDriver, chunksPerCall, itemsPerCall](int)
		{
			for (int c = 0; c < callsPerDriver; ++c)
			{
				multi::range(chunksPerCall, 0, itemsPerCall, 1, [&sink](int)
				{
					sink.fetch_add(1, std::memory_order_relaxed);
				});
			}
		};
	};

	auto asyncWork = [&sink](int callsPerDriver)
	{
		return [&sink, callsPerDriver](int)
		{
			for (int c = 0; c < callsPerDriver; ++c)
			{
				multi::waitAll(multi::async([&sink]()
				{
					sink.fetch_add(1, std::memory_order_relaxed);
				}));
			}
		};
	};

	SECTION("4 drivers x range")
	{
		DriverPool pool(4, rangeWork(200, 32, 1000));
		BENCHMARK("multi(range)") { pool.runRound(); return sink.load(); };
	}
	SECTION("8 drivers x range")
	{
		DriverPool pool(8, rangeWork(200, 32, 1000));
		BENCHMARK("multi(range)") { pool.runRound(); return sink.load(); };
	}
	SECTION("4 drivers x async.wait")
	{
		DriverPool pool(4, asyncWork(500));
		BENCHMARK("multi(async)") { pool.runRound(); return sink.load(); };
	}
	SECTION("8 drivers x async.wait")
	{
		DriverPool pool(8, asyncWork(500));
		BENCHMARK("multi(async)") { pool.runRound(); return sink.load(); };
	}
}
