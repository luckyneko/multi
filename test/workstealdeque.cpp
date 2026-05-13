/*
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <atomic>
#include <catch2/catch_all.hpp>
#include <multi/details/workstealdeque.h>
#include <thread>
#include <vector>

TEST_CASE("WorkStealDeque: empty")
{
	multi::WorkStealDeque deque;
	CHECK(deque.empty());
	CHECK(deque.sizeHint() == 0);

	multi::Task task;
	CHECK(deque.pop(&task) == false);
	CHECK(deque.steal(&task) == false);
}

TEST_CASE("WorkStealDeque: push and pop (LIFO)")
{
	multi::WorkStealDeque deque;
	int order = 0;

	deque.tryPushRemote([&order]() { order = 1; });
	deque.tryPushRemote([&order]() { order = 2; });
	deque.tryPushRemote([&order]() { order = 3; });
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

TEST_CASE("WorkStealDeque: push and steal (FIFO)")
{
	multi::WorkStealDeque deque;
	int order = 0;

	deque.tryPushRemote([&order]() { order = 1; });
	deque.tryPushRemote([&order]() { order = 2; });
	deque.tryPushRemote([&order]() { order = 3; });

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

TEST_CASE("WorkStealDeque: mixed pop and steal")
{
	multi::WorkStealDeque deque;

	deque.tryPushRemote([&]() {});
	deque.tryPushRemote([&]() {});
	deque.tryPushRemote([&]() {});

	multi::Task task;

	// Steal takes from front, pop takes from back
	REQUIRE(deque.steal(&task)); // removes front
	REQUIRE(deque.pop(&task));   // removes back
	CHECK(deque.sizeHint() == 1);

	REQUIRE(deque.pop(&task));
	CHECK(deque.empty());
}

TEST_CASE("WorkStealDeque: concurrent push and steal stress", "[stress]")
{
	multi::WorkStealDeque deque;
	const int numTasks = 1000;
	std::atomic<int> counter(0);

	// Producer pushes tasks
	std::thread producer([&]()
						 {
		for (int i = 0; i < numTasks; ++i)
			deque.tryPushRemote([&counter]() { counter++; }); });

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

TEST_CASE("WorkStealDeque: concurrent pop and steal stress", "[stress]")
{
	multi::WorkStealDeque deque;
	const int numTasks = 1000;
	std::atomic<int> counter(0);

	for (int i = 0; i < numTasks; ++i)
		deque.tryPushRemote([&counter]() { counter++; });

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

// ---------------------------------------------------------------------------
// Local-full cascade — pushing more than LOCAL_CAP items via tryPushLocal
// must NOT drop tasks. Regression test for the by-value parameter signature
// on ChaseLevDeque::tryPushBottom / MpmcQueue::tryPush, which moved the
// caller's Task into the parameter *unconditionally* and left an empty
// moved-from Task for the overflow fallback to push. Symptom under the
// bug: tasks 257..N surface as empty Tasks on pop (m_vtable == nullptr),
// silently dropped by the worker loop's `while (task)` predicate, and
// `Job::m_remaining` never reaches zero.
// ---------------------------------------------------------------------------
TEST_CASE("WorkStealDeque: tryPushLocal cascade preserves Task contents")
{
	multi::WorkStealDeque deque;

	constexpr std::size_t N = 1000;  // > LOCAL_CAP=256, forces cascade to overflow
	std::atomic<int> counter(0);

	for (std::size_t i = 0; i < N; ++i)
	{
		multi::Task t([&counter]() { counter.fetch_add(1, std::memory_order_relaxed); });
		REQUIRE(deque.tryPushLocal(std::move(t)));
	}

	// Drain everything. Each popped Task must be invocable (m_vtable != nullptr).
	multi::Task popped;
	std::size_t popCount = 0;
	while (deque.pop(&popped))
	{
		REQUIRE(static_cast<bool>(popped));
		popped();
		popped = {};
		++popCount;
	}

	CHECK(popCount == N);
	CHECK(counter.load() == static_cast<int>(N));
}
