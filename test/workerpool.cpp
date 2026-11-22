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
#include <cstddef>
#include <future>
#include <memory>
#include <multi/details/workerpool.h>
#include <thread>
#include <utility>
#include <vector>

#include "support.h"

namespace
{
	// How long a parked worker may take to be woken and run a task. Generous
	// for sanitizer builds under ctest -j; a passing run never waits this long.
	constexpr auto k_wakeTimeout = std::chrono::seconds(2);

	// Time for started or just-finished workers to reach cv.wait. Only the
	// unfixed pool depends on it to fail (a worker that has not parked yet
	// still scans the deques); the fixed pool passes whether or not they have.
	constexpr auto k_parkDelay = std::chrono::milliseconds(50);

	// One task through submit() or submitBatch(1, ...): either consumes
	// exactly one round-robin slot, so the slot arithmetic holds for both.
	void submitOne(multi::details::WorkerPool& pool, multi::details::Task&& task, bool batch)
	{
		if (batch)
			pool.submitBatch(1, [&task](size_t)
							 { return std::move(task); });
		else
			pool.submit(std::move(task));
	}
} // namespace

TEST_CASE("WorkerPool: submit runs inline when not started")
{
	multi::details::WorkerPool pool;
	REQUIRE(pool.threadCount() == 0);

	int value = 0;
	pool.submit([&value]()
				{ value = 42; });
	CHECK(value == 42);

	pool.submit([&value]()
				{ value += 1; });
	pool.submit([&value]()
				{ value += 2; });
	CHECK(value == 45);
}

TEST_CASE("WorkerPool: submit runs inline after stop")
{
	multi::details::WorkerPool pool;
	pool.start(2);
	pool.stop();
	REQUIRE(!pool.isActive());

	int value = 0;
	pool.submit([&value]()
				{ value = 42; });
	CHECK(value == 42);

	pool.submit([&value]()
				{ value += 1; });
	pool.submit([&value]()
				{ value += 2; });
	CHECK(value == 45);
}

// ---------------------------------------------------------------------------
// trySubmit: queue or report, never run. submit() runs a task inline when it
// cannot queue it; a caller that must not nest (RecipeJob) uses trySubmit and
// keeps the task.
// ---------------------------------------------------------------------------

TEST_CASE("WorkerPool: trySubmit queues nothing when not started")
{
	multi::details::WorkerPool pool;

	bool ran = false;
	multi::details::Task task([&ran]()
							  { ran = true; });
	CHECK_FALSE(pool.trySubmit(task));
	CHECK_FALSE(ran);

	// Untouched: still the caller's to run.
	REQUIRE(static_cast<bool>(task));
	task();
	CHECK(ran);
}

TEST_CASE("WorkerPool: trySubmit queues nothing after stop")
{
	multi::details::WorkerPool pool;
	pool.start(2);
	pool.stop();

	bool ran = false;
	multi::details::Task task([&ran]()
							  { ran = true; });
	CHECK_FALSE(pool.trySubmit(task));
	CHECK_FALSE(ran);
	CHECK(static_cast<bool>(task));
}

TEST_CASE("WorkerPool: trySubmit queues a task on a running pool")
{
	multi::details::WorkerPool pool;
	pool.start(2);

	std::promise<void> done;
	auto doneFuture = done.get_future();
	multi::details::Task task([&done]()
							  { done.set_value(); });
	CHECK(pool.trySubmit(task));
	CHECK_FALSE(static_cast<bool>(task)); // moved into the deque

	CHECK(doneFuture.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
	pool.stop();
}

TEST_CASE("WorkerPool: trySubmit reports a full deque without running the task")
{
	multi::details::WorkerPool pool;
	pool.start(1);

	// Hold the only worker, so nothing drains its deque.
	std::promise<void> release;
	std::shared_future<void> released = release.get_future().share();
	std::promise<void> holding;
	auto holdingFuture = holding.get_future();
	pool.submit([&holding, released]()
				{
		holding.set_value();
		released.wait(); });
	REQUIRE(holdingFuture.wait_for(std::chrono::seconds(2)) == std::future_status::ready);

	// Fill the worker's overflow ring from this thread until a push is refused.
	std::atomic<std::size_t> ran(0);
	std::size_t queued = 0;
	bool refused = false;
	bool refusedTaskIntact = false;
	for (std::size_t i = 0; i < 100000 && !refused; ++i)
	{
		multi::details::Task task([&ran]()
								  { ran.fetch_add(1); });
		if (pool.trySubmit(task))
		{
			++queued;
		}
		else
		{
			refused = true;
			refusedTaskIntact = static_cast<bool>(task);
		}
	}
	CHECK(refused);
	CHECK(refusedTaskIntact);
	CHECK(queued > 0);
	CHECK(ran.load() == 0); // nothing ran on this thread, and the worker is held

	release.set_value();
	pool.stop(); // drains what was queued
	CHECK(ran.load() == queued);
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

	pool.submitBatch(numTasks, [&counter, numTasks, done](size_t)
					 { return multi::details::Task([&counter, numTasks, done]()
												   {
			if (++counter == numTasks)
				done->set_value(); }); });

	doneHandle.wait();
	CHECK(counter == numTasks);
	pool.stop();
}

TEST_CASE("WorkerPool: empty batch is a no-op")
{
	multi::details::WorkerPool pool;
	pool.start(2);

	pool.submitBatch(0, [](size_t)
					 { return multi::details::Task{}; });

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

	pool.submitBatch(numTasks, [&counter](size_t)
					 { return multi::details::Task([&counter]()
												   { counter++; }); });

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
		pool.submitBatch(batchSize, [&counter, totalTasks, allDone](size_t)
						 { return multi::details::Task([&counter, totalTasks, allDone]()
													   {
				if (++counter == totalTasks)
					allDone->set_value(); }); });
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
	CHECK_FALSE(pool.start(4, [&](size_t)
						   {
		if (created++ >= 1)
			throw std::runtime_error("test-injected thread creation failure"); }));
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
		pool.submit([&counter]()
					{ counter++; });
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

	pool.submitBatch(numTasks, [&counter](size_t)
					 { return multi::details::Task([&counter]()
												   { counter.fetch_add(1, std::memory_order_relaxed); }); });

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
				pool.submitBatch(8, [&totalRun](size_t)
								 { return multi::details::Task([&totalRun]()
															   { totalRun.fetch_add(1, std::memory_order_relaxed); }); });
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

		pool.submitBatch(numTasks, [&totalRun](size_t)
						 { return multi::details::Task([&totalRun]()
													   { totalRun.fetch_add(1, std::memory_order_relaxed); }); });

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
// Wakeups: a task whose round-robin target will not pick it up promptly must
// wake a parked worker to steal it. Both cases pin tasks to slots through the
// round-robin counter, which restarts at 0 on start(). Every wait is bounded
// and every blocker is released, so a regression fails an assertion instead of
// hanging the suite.
// ---------------------------------------------------------------------------

TEST_CASE("WorkerPool: parked worker runs a task queued behind a busy worker")
{
	const bool batch = GENERATE(false, true);
	CAPTURE(batch);

	multi::details::WorkerPool pool;
	pool.start(4);
	std::this_thread::sleep_for(k_parkDelay); // all four reach cv.wait

	std::promise<void> release;
	std::shared_future<void> released = release.get_future().share();
	std::atomic<bool> blockerRunning(false);
	std::atomic<int> fillersDone(0);
	std::atomic<bool> probeRan(false);

	// Slot 0: blocks its worker until released. Wait until it is running
	// before using slots 1..3, so no sibling can steal it off slot 0 first.
	pool.submit([&blockerRunning, released]()
				{
		blockerRunning = true;
		released.wait(); });
	CHECK(support::waitFor([&blockerRunning]()
						   { return blockerRunning.load(); },
						   k_wakeTimeout));

	// Slots 1..3: finish at once, so their workers park again.
	for (int i = 0; i < 3; ++i)
		pool.submit([&fillersDone]()
					{ fillersDone.fetch_add(1); });
	CHECK(support::waitFor([&fillersDone]()
						   { return fillersDone.load() == 3; },
						   k_wakeTimeout));
	std::this_thread::sleep_for(k_parkDelay); // workers 1..3 park

	// Slot 0 again: queued behind the blocker. One of the three parked
	// workers must steal it while the blocker still holds worker 0.
	submitOne(pool, multi::details::Task([&probeRan]()
										 { probeRan = true; }),
			  batch);
	CHECK(support::waitFor([&]()
						   { return probeRan.load(); },
						   k_wakeTimeout));

	release.set_value();
	pool.stop();
	CHECK(probeRan.load());
}

TEST_CASE("WorkerPool: nested task pushed to its own blocked worker is stolen")
{
	const bool batch = GENERATE(false, true);
	CAPTURE(batch);

	multi::details::WorkerPool pool;
	pool.start(4);
	std::this_thread::sleep_for(k_parkDelay); // all four reach cv.wait

	std::atomic<bool> outerRunning(false);
	std::atomic<int> fillersDone(0);
	std::atomic<bool> nestedRanInTime(false);
	auto outerDone = std::make_shared<std::promise<void>>();
	auto outerDoneFuture = outerDone->get_future();

	// Slot 0. Waits for its siblings to run their fillers and park, then
	// submits a nested task that lands on slot 4 % 4 == 0 — its own deque,
	// through the owner's local push — and blocks on it.
	pool.submit([&pool, &outerRunning, &fillersDone, &nestedRanInTime, outerDone, batch]()
				{
		outerRunning = true;
		support::waitFor([&fillersDone]()
				{ return fillersDone.load() == 3; },
				k_wakeTimeout);
		std::this_thread::sleep_for(k_parkDelay); // workers 1..3 park

		auto nested = std::make_shared<std::promise<void>>();
		auto nestedFuture = nested->get_future();
		submitOne(pool, multi::details::Task([nested]()
											 { nested->set_value(); }),
				  batch);
		nestedRanInTime = nestedFuture.wait_for(k_wakeTimeout) == std::future_status::ready;
		outerDone->set_value(); });

	// Fill slots 1..3 only once the outer task is running, so no sibling can
	// steal it off slot 0 first.
	CHECK(support::waitFor([&outerRunning]()
						   { return outerRunning.load(); },
						   k_wakeTimeout));
	for (int i = 0; i < 3; ++i)
		pool.submit([&fillersDone]()
					{ fillersDone.fetch_add(1); });

	// The outer task's own waits are bounded, so it always finishes.
	CHECK(outerDoneFuture.wait_for(4 * k_wakeTimeout) == std::future_status::ready);
	pool.stop();
	CHECK(nestedRanInTime.load());
}

// ---------------------------------------------------------------------------
// Worker thread naming — best-effort cross-platform. We can verify the name
// landed on the platforms where pthread_getname_np exists (Linux + Android +
// macOS). Windows has GetThreadDescription but verifying it from a worker
// task requires CoTaskMemFree dance; the build-coverage CI catches the
// Windows path compiling, so skip the runtime verification there.
// ---------------------------------------------------------------------------
#if defined(__APPLE__) || defined(__linux__) || defined(__ANDROID__)
#	include <pthread.h>
#	include <string>

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
		pool.submit(multi::details::Task([&, i, done]()
										 {
			char buf[32] = {0};
			pthread_getname_np(pthread_self(), buf, sizeof(buf));
			names[i] = buf;
			done->fetch_add(1, std::memory_order_release); }));
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
