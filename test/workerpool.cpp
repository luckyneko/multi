/*
 *  Created by LuckyNeko on 17/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <atomic>
#include <catch2/catch_all.hpp>
#include <chrono>
#include <future>
#include <multi/details/workerpool.h>
#include <thread>
#include <vector>

TEST_CASE("WorkerPool: submit runs inline when not started")
{
	multi::details::WorkerPool pool;
	REQUIRE(pool.threadCount() == 0);

	int value = 0;
	pool.submit([&value]() { value = 42; });
	CHECK(value == 42);

	pool.submit([&value]() { value += 1; });
	pool.submit([&value]() { value += 2; });
	CHECK(value == 45);
}

TEST_CASE("WorkerPool: submit runs inline after stop")
{
	multi::details::WorkerPool pool;
	pool.start(2);
	pool.stop();
	REQUIRE(!pool.isActive());

	int value = 0;
	pool.submit([&value]() { value = 42; });
	CHECK(value == 42);

	pool.submit([&value]() { value += 1; });
	pool.submit([&value]() { value += 2; });
	CHECK(value == 45);
}

TEST_CASE("WorkerPool: submit single tasks")
{
	auto numThreads = GENERATE(1, 2, 4, 8, 16);
	multi::details::WorkerPool pool;
	pool.start(numThreads);

	std::atomic<int> value(0);
	auto triggerA = std::make_shared<std::promise<void>>();
	auto handleA = triggerA->get_future();
	pool.submit([triggerA, &value]()
				{
		value += 1;
		triggerA->set_value(); });

	auto triggerB = std::make_shared<std::promise<void>>();
	auto handleB = triggerB->get_future();
	pool.submit([triggerB, &value]()
				{
		value += 2;
		triggerB->set_value(); });

	handleA.wait();
	handleB.wait();
	CHECK(value == 3);
	pool.stop();
}

TEST_CASE("WorkerPool: submitBatch distributes work")
{
	auto numThreads = GENERATE(1, 2, 4, 8, 16);
	multi::details::WorkerPool pool;
	pool.start(numThreads);

	const int numTasks = 100;
	std::atomic<int> counter(0);
	auto done = std::make_shared<std::promise<void>>();
	auto doneHandle = done->get_future();

	pool.submitBatch(numTasks, [&counter, numTasks, done](size_t) {
		return multi::details::Task([&counter, numTasks, done]() {
			if (++counter == numTasks)
				done->set_value();
		});
	});

	doneHandle.wait();
	CHECK(counter == numTasks);
	pool.stop();
}

TEST_CASE("WorkerPool: empty batch is a no-op")
{
	multi::details::WorkerPool pool;
	pool.start(2);

	pool.submitBatch(0, [](size_t) { return multi::details::Task{}; });

	pool.stop();
}

TEST_CASE("WorkerPool: nested submit completes")
{
	auto numThreads = GENERATE(1, 2, 4, 8, 16);
	multi::details::WorkerPool pool;
	pool.start(numThreads);

	std::atomic<int> value(0);
	auto triggerOuter = std::make_shared<std::promise<void>>();
	auto handleOuter = triggerOuter->get_future();
	auto triggerInner = std::make_shared<std::promise<void>>();
	auto handleInner = triggerInner->get_future();

	pool.submit([&pool, &value, triggerOuter, triggerInner]()
				{
		value += 1;
		pool.submit([&value, triggerInner]()
					{
			value += 2;
			triggerInner->set_value();
		});
		triggerOuter->set_value(); });

	handleOuter.wait();
	handleInner.wait();
	CHECK(value == 3);
	pool.stop();
}

TEST_CASE("WorkerPool: tryStealAny lets caller participate")
{
	auto numThreads = GENERATE(1, 2, 4, 8, 16);
	multi::details::WorkerPool pool;
	pool.start(numThreads);

	const int numTasks = 50;
	std::atomic<int> counter(0);

	pool.submitBatch(numTasks, [&counter](size_t) {
		return multi::details::Task([&counter]() { counter++; });
	});

	multi::details::Task stolen;
	while (pool.tryStealAny(&stolen))
	{
		stolen();
		stolen = {};
	}

	while (counter.load() < numTasks)
		std::this_thread::yield();

	CHECK(counter == numTasks);
	pool.stop();
}

TEST_CASE("WorkerPool: high contention batch")
{
	auto numThreads = GENERATE(1, 2, 4, 8, 16);
	multi::details::WorkerPool pool;
	pool.start(numThreads);

	const int numBatches = 20;
	const int batchSize = 100;
	std::atomic<int> counter(0);
	auto allDone = std::make_shared<std::promise<void>>();
	auto allDoneHandle = allDone->get_future();
	const int totalTasks = numBatches * batchSize;

	for (int b = 0; b < numBatches; ++b)
	{
		pool.submitBatch(batchSize, [&counter, totalTasks, allDone](size_t) {
			return multi::details::Task([&counter, totalTasks, allDone]() {
				if (++counter == totalTasks)
					allDone->set_value();
			});
		});
	}

	allDoneHandle.wait();
	CHECK(counter == totalTasks);
	pool.stop();
}

TEST_CASE("WorkerPool: start returns false when already active")
{
	multi::details::WorkerPool pool;
	pool.start(2);
	CHECK_FALSE(pool.start(2));
	CHECK_FALSE(pool.start(0));
	pool.stop();

	// Stopping clears the active flag; starting again must succeed.
	CHECK(pool.start(1));
	pool.stop();
}

TEST_CASE("WorkerPool: failed start rolls back created workers")
{
	multi::details::WorkerPool pool;
	size_t created = 0;
	CHECK_FALSE(pool.start(4, [&](size_t) {
		if (created++ >= 1)
			throw std::runtime_error("test-injected thread creation failure");
	}));
	CHECK(pool.threadCount() == 0);
	CHECK_FALSE(pool.isActive());

	CHECK(pool.start(2));
	CHECK(pool.threadCount() == 2);
	pool.stop();
}

#ifdef NDEBUG
// Release only: debug builds assert in ~WorkerPool when stop() was skipped.
TEST_CASE("WorkerPool: destructor stops threads on forgotten stop")
{
	std::atomic<int> counter(0);
	{
		multi::details::WorkerPool pool;
		pool.start(2);
		pool.submit([&counter]() { counter++; });
		// Intentionally no stop() — destructor must join threads defensively.
	}
	// If the dtor didn't join, this process would leak threads or crash.
	CHECK(counter.load() >= 0);
}
#endif

// 2000 tasks forces spillover into the per-worker MPMC overflow ring
// (LOCAL_CAP=256 per worker × 2 workers); drain must reach those, not just
// the local Chase-Lev half.
TEST_CASE("WorkerPool: stop drains pending tasks")
{
	multi::details::WorkerPool pool;
	pool.start(2);

	std::atomic<int> counter(0);
	const int numTasks = 2000;

	pool.submitBatch(numTasks, [&counter](size_t) {
		return multi::details::Task([&counter]() { counter.fetch_add(1, std::memory_order_relaxed); });
	});

	pool.stop();

	CHECK(counter == numTasks);
}

// Stresses the submit-vs-stop race: external submitter threads call
// submit/submitBatch in tight loops while the test thread calls stop().
// Without the operation guard, a submitter that passed isActive() could push into
// m_workers after stop() cleared it (UAF). With the guard, every submitted
// task must run exactly once: pushed-and-drained while active, or inline-
// fallback once m_active flips. Repeated trials shake out timing jitter.
TEST_CASE("WorkerPool: submit and stop race without lost tasks", "[stress]")
{
	for (int trial = 0; trial < 8; ++trial)
	{
		multi::details::WorkerPool pool;
		pool.start(2);

		std::atomic<int> totalRun(0);
		std::atomic<int> totalSubmitted(0);
		std::atomic<bool> shouldRun(true);

		auto submitterMain = [&pool, &totalRun, &totalSubmitted, &shouldRun]()
		{
			while (shouldRun.load(std::memory_order_relaxed))
			{
				pool.submit([&totalRun]()
							{ totalRun.fetch_add(1, std::memory_order_relaxed); });
				totalSubmitted.fetch_add(1, std::memory_order_relaxed);
			}
		};

		auto batchSubmitterMain = [&pool, &totalRun, &totalSubmitted, &shouldRun]()
		{
			while (shouldRun.load(std::memory_order_relaxed))
			{
				pool.submitBatch(8, [&totalRun](size_t) {
					return multi::details::Task([&totalRun]() { totalRun.fetch_add(1, std::memory_order_relaxed); });
				});
				totalSubmitted.fetch_add(8, std::memory_order_relaxed);
			}
		};

		std::vector<std::thread> submitters;
		submitters.emplace_back(submitterMain);
		submitters.emplace_back(submitterMain);
		submitters.emplace_back(batchSubmitterMain);

		// Let submitters pump for a moment so stop() races a busy push region.
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
		pool.stop();
		// Submitters now hit !isActive() and inline-fallback; let that run too.
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		shouldRun.store(false);
		for (auto& t : submitters)
			t.join();

		// Every task that was returned from submit/submitBatch must have run
		// exactly once. Drift here would indicate a lost task in the race.
		CHECK(totalRun.load() == totalSubmitted.load());
	}
}

TEST_CASE("WorkerPool: external steal and stop race without touching freed workers", "[stress]")
{
	for (int trial = 0; trial < 8; ++trial)
	{
		multi::details::WorkerPool pool;
		pool.start(2);

		const int numTasks = 4096;
		std::atomic<int> totalRun(0);
		std::atomic<bool> keepStealing(true);

		pool.submitBatch(numTasks, [&totalRun](size_t) {
			return multi::details::Task([&totalRun]() { totalRun.fetch_add(1, std::memory_order_relaxed); });
		});

		auto stealerMain = [&pool, &keepStealing]()
		{
			while (keepStealing.load(std::memory_order_relaxed))
			{
				multi::details::Task stolen;
				if (pool.tryStealAny(&stolen))
					stolen();
				else
					std::this_thread::yield();
			}
		};

		std::thread a(stealerMain);
		std::thread b(stealerMain);

		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		pool.stop();
		keepStealing.store(false, std::memory_order_relaxed);
		a.join();
		b.join();

		CHECK(totalRun.load(std::memory_order_relaxed) == numTasks);
	}
}

// ---------------------------------------------------------------------------
// Worker thread naming — best-effort cross-platform. We can verify the name
// landed on the platforms where pthread_getname_np exists (Linux + Android +
// macOS). Windows has GetThreadDescription but verifying it from a worker
// task requires CoTaskMemFree dance; the build-coverage CI catches the
// Windows path compiling, so skip the runtime verification there.
// ---------------------------------------------------------------------------
#if defined(__APPLE__) || defined(__linux__) || defined(__ANDROID__)
#include <pthread.h>
#include <string>

TEST_CASE("WorkerPool: worker thread name matches multi-N convention")
{
	multi::details::WorkerPool pool;
	pool.start(3);

	// Capture names from each worker via async-style submit. Each task reads
	// its own pthread name, formats it into a per-index slot, and signals.
	std::array<std::string, 3> names;
	auto done = std::make_shared<std::atomic<int>>(0);
	for (std::size_t i = 0; i < 3; ++i)
	{
		pool.submit(multi::details::Task([&, i, done]() {
			char buf[32] = {0};
			pthread_getname_np(pthread_self(), buf, sizeof(buf));
			names[i] = buf;
			done->fetch_add(1, std::memory_order_release);
		}));
	}

	// Spin until all three tasks have run. Round-robin distribution lands
	// one per worker; even if two land on the same worker they execute
	// sequentially and we still get three observations.
	while (done->load(std::memory_order_acquire) < 3)
		std::this_thread::yield();

	for (const auto& n : names)
	{
		CAPTURE(n);
		// Worker index is whichever the round-robin assigned; just check
		// the prefix and that the suffix is a small integer.
		REQUIRE(n.rfind("multi-", 0) == 0);
		const std::string idx = n.substr(6);
		REQUIRE(!idx.empty());
		for (char c : idx)
			CHECK((c >= '0' && c <= '9'));
	}

	pool.stop();
}
#endif
