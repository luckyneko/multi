/*
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <atomic>
#include <catch2/catch.hpp>
#include <multi/details/workstealdeque.h>
#include <thread>
#include <vector>

TEST_CASE("multi::WorkStealDeque")
{
	SECTION("empty deque")
	{
		multi::WorkStealDeque deque;
		CHECK(deque.empty());
		CHECK(deque.sizeHint() == 0);

		multi::Task task;
		CHECK(deque.pop(&task) == false);
		CHECK(deque.steal(&task) == false);
	}

	SECTION("push and pop (LIFO)")
	{
		multi::WorkStealDeque deque;
		int order = 0;

		deque.push([&order]()
				   { order = 1; });
		deque.push([&order]()
				   { order = 2; });
		deque.push([&order]()
				   { order = 3; });
		CHECK(deque.sizeHint() == 3);

		multi::Task task;

		// Pop returns from back (LIFO)
		REQUIRE(deque.pop(&task));
		task();
		CHECK(order == 3);

		REQUIRE(deque.pop(&task));
		task();
		CHECK(order == 2);

		REQUIRE(deque.pop(&task));
		task();
		CHECK(order == 1);

		CHECK(deque.empty());
		CHECK(deque.pop(&task) == false);
	}

	SECTION("push and steal (FIFO)")
	{
		multi::WorkStealDeque deque;
		int order = 0;

		deque.push([&order]()
				   { order = 1; });
		deque.push([&order]()
				   { order = 2; });
		deque.push([&order]()
				   { order = 3; });

		multi::Task task;

		// Steal returns from front (FIFO)
		REQUIRE(deque.steal(&task));
		task();
		CHECK(order == 1);

		REQUIRE(deque.steal(&task));
		task();
		CHECK(order == 2);

		REQUIRE(deque.steal(&task));
		task();
		CHECK(order == 3);

		CHECK(deque.empty());
		CHECK(deque.steal(&task) == false);
	}

	SECTION("mixed pop and steal")
	{
		multi::WorkStealDeque deque;

		deque.push([&]() {});
		deque.push([&]() {});
		deque.push([&]() {});

		multi::Task task;

		// Steal takes from front, pop takes from back
		REQUIRE(deque.steal(&task)); // removes front
		REQUIRE(deque.pop(&task));	 // removes back
		CHECK(deque.sizeHint() == 1);

		REQUIRE(deque.pop(&task));
		CHECK(deque.empty());
	}

	SECTION("concurrent push and steal")
	{
		multi::WorkStealDeque deque;
		const int numTasks = 1000;
		std::atomic<int> counter(0);

		// Producer pushes tasks
		std::thread producer([&]()
							 {
			for (int i = 0; i < numTasks; ++i)
				deque.push([&counter]() { counter++; }); });

		// Thieves steal tasks
		std::atomic<int> totalStolen(0);
		std::vector<std::thread> thieves;
		for (int t = 0; t < 4; ++t)
		{
			thieves.emplace_back([&]()
								 {
				multi::Task task;
				while (totalStolen.load() < numTasks)
				{
					if (deque.steal(&task))
					{
						task();
						totalStolen++;
					}
					else
					{
						std::this_thread::yield();
					}
				} });
		}

		producer.join();
		for (auto& t : thieves)
			t.join();

		// Some tasks may still be in the deque if thieves stopped early
		multi::Task task;
		while (deque.pop(&task))
		{
			task();
			totalStolen++;
		}

		CHECK(totalStolen == numTasks);
		CHECK(counter == numTasks);
	}

	SECTION("concurrent pop and steal")
	{
		multi::WorkStealDeque deque;
		const int numTasks = 1000;
		std::atomic<int> counter(0);

		for (int i = 0; i < numTasks; ++i)
			deque.push([&counter]()
					   { counter++; });

		// Owner pops from back
		std::atomic<int> totalDone(0);
		std::thread owner([&]()
						  {
			multi::Task task;
			while (deque.pop(&task))
			{
				task();
				totalDone++;
			} });

		// Thieves steal from front
		std::vector<std::thread> thieves;
		for (int t = 0; t < 4; ++t)
		{
			thieves.emplace_back([&]()
								 {
				multi::Task task;
				while (deque.steal(&task))
				{
					task();
					totalDone++;
				} });
		}

		owner.join();
		for (auto& t : thieves)
			t.join();

		CHECK(counter == numTasks);
	}
}
