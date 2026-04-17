/*
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <catch2/catch.hpp>
#include <atomic>
#include <future>
#include <vector>
#include <multi/details/workerpool.h>

TEST_CASE("multi::WorkerPool")
{
	multi::WorkerPool pool;
	REQUIRE(pool.threadCount() == 0);

	SECTION("single-threaded mode executes inline")
	{
		int value = 0;
		pool.submit([&value]() { value = 42; });
		CHECK(value == 42);

		std::vector<multi::Task> batch;
		batch.emplace_back([&value]() { value += 1; });
		batch.emplace_back([&value]() { value += 2; });
		pool.submitBatch(std::move(batch));
		CHECK(value == 45);
	}

	for (auto numThreads : {1, 2, 4, 8, 16})
	{
		SECTION("thread count " + std::to_string(numThreads))
		{
			pool.start(numThreads);
			REQUIRE(pool.threadCount() == static_cast<size_t>(numThreads));
			REQUIRE(pool.isActive());

			SECTION("submit single tasks")
			{
				std::atomic<int> value(0);

				auto triggerA = std::make_shared<std::promise<void>>();
				auto handleA = triggerA->get_future();
				pool.submit([triggerA, &value]()
							{
					value += 1;
					triggerA->set_value();
				});

				auto triggerB = std::make_shared<std::promise<void>>();
				auto handleB = triggerB->get_future();
				pool.submit([triggerB, &value]()
							{
					value += 2;
					triggerB->set_value();
				});

				handleA.wait();
				handleB.wait();
				CHECK(value == 3);
			}

			SECTION("submitBatch distributes work")
			{
				const int numTasks = 100;
				std::atomic<int> counter(0);
				auto done = std::make_shared<std::promise<void>>();
				auto doneHandle = done->get_future();

				std::vector<multi::Task> tasks;
				for (int i = 0; i < numTasks; ++i)
				{
					tasks.emplace_back([&counter, numTasks, done]()
									   {
						if (++counter == numTasks)
							done->set_value();
					});
				}
				pool.submitBatch(std::move(tasks));

				doneHandle.wait();
				CHECK(counter == numTasks);
			}

			SECTION("nested submit")
			{
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
					triggerOuter->set_value();
				});

				handleOuter.wait();
				handleInner.wait();
				CHECK(value == 3);
			}

			SECTION("tryStealAny")
			{
				// Submit tasks and try to steal from the main thread
				const int numTasks = 50;
				std::atomic<int> counter(0);

				std::vector<multi::Task> tasks;
				for (int i = 0; i < numTasks; ++i)
					tasks.emplace_back([&counter]() { counter++; });
				pool.submitBatch(std::move(tasks));

				// Main thread steals some work too
				multi::Task stolen;
				while (pool.tryStealAny(&stolen))
				{
					stolen();
					stolen = nullptr;
				}

				// Wait for workers to finish remaining
				while (counter.load() < numTasks)
					std::this_thread::yield();

				CHECK(counter == numTasks);
			}

			SECTION("high contention batch")
			{
				const int numBatches = 20;
				const int batchSize = 100;
				std::atomic<int> counter(0);
				auto allDone = std::make_shared<std::promise<void>>();
				auto allDoneHandle = allDone->get_future();
				const int totalTasks = numBatches * batchSize;

				for (int b = 0; b < numBatches; ++b)
				{
					std::vector<multi::Task> tasks;
					for (int i = 0; i < batchSize; ++i)
					{
						tasks.emplace_back([&counter, totalTasks, allDone]()
										   {
							if (++counter == totalTasks)
								allDone->set_value();
						});
					}
					pool.submitBatch(std::move(tasks));
				}

				allDoneHandle.wait();
				CHECK(counter == totalTasks);
			}

			pool.stop();
			REQUIRE(pool.threadCount() == 0);
			REQUIRE(!pool.isActive());
		}
	}
}

TEST_CASE("multi::WorkerPool empty batch")
{
	multi::WorkerPool pool;
	pool.start(2);

	std::vector<multi::Task> empty;
	pool.submitBatch(std::move(empty));

	pool.stop();
}

TEST_CASE("multi::WorkerPool stop drains remaining tasks")
{
	multi::WorkerPool pool;
	pool.start(2);

	std::atomic<int> counter(0);
	const int numTasks = 100;

	std::vector<multi::Task> tasks;
	for (int i = 0; i < numTasks; ++i)
		tasks.emplace_back([&counter]() { counter++; });
	pool.submitBatch(std::move(tasks));

	pool.stop();

	// All tasks should have completed during or after stop()
	CHECK(counter == numTasks);
}
