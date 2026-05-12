/*
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <atomic>
#include <catch2/catch_all.hpp>
#include <multi/details/mpmcqueue.h>
#include <thread>
#include <vector>

TEST_CASE("MpmcQueue: empty")
{
	multi::MpmcQueue<int, 8> q;
	CHECK(q.sizeHint() == 0);
	int v = -1;
	CHECK_FALSE(q.tryPop(&v));
}

TEST_CASE("MpmcQueue: FIFO order")
{
	multi::MpmcQueue<int, 8> q;
	for (int i = 0; i < 5; ++i)
		REQUIRE(q.tryPush(int(i)));
	CHECK(q.sizeHint() == 5);

	int v = 0;
	for (int i = 0; i < 5; ++i)
	{
		REQUIRE(q.tryPop(&v));
		CHECK(v == i);
	}
	CHECK_FALSE(q.tryPop(&v));
}

TEST_CASE("MpmcQueue: full at capacity")
{
	multi::MpmcQueue<int, 4> q;
	for (int i = 0; i < 4; ++i)
		REQUIRE(q.tryPush(int(i)));
	CHECK_FALSE(q.tryPush(99));

	int v = 0;
	REQUIRE(q.tryPop(&v));
	CHECK(v == 0);
	REQUIRE(q.tryPush(99));
	CHECK_FALSE(q.tryPush(100));
}

TEST_CASE("MpmcQueue: wraparound past capacity")
{
	multi::MpmcQueue<int, 4> q;
	int v = 0;
	for (int round = 0; round < 20; ++round)
	{
		REQUIRE(q.tryPush(int(round)));
		REQUIRE(q.tryPop(&v));
		CHECK(v == round);
	}
}

TEST_CASE("MpmcQueue: concurrent producers and consumers stress", "[stress]")
{
	constexpr std::size_t CAP = 1024;
	multi::MpmcQueue<int, CAP> q;

	const int producerCount = 4;
	const int consumerCount = 4;
	const int itemsPerProducer = 25000;
	const int totalItems = producerCount * itemsPerProducer;

	std::vector<std::atomic<int>> seenCounts(totalItems);
	for (auto& c : seenCounts)
		c.store(0, std::memory_order_relaxed);

	std::atomic<int> produced{0};
	std::atomic<int> consumed{0};

	std::vector<std::thread> producers;
	for (int p = 0; p < producerCount; ++p)
	{
		producers.emplace_back([&, p]()
							   {
			const int start = p * itemsPerProducer;
			for (int i = 0; i < itemsPerProducer; ++i)
			{
				int v = start + i;
				while (!q.tryPush(int(v)))
					std::this_thread::yield();
				produced.fetch_add(1, std::memory_order_relaxed);
			} });
	}

	std::vector<std::thread> consumers;
	for (int c = 0; c < consumerCount; ++c)
	{
		consumers.emplace_back([&]()
							   {
			int v;
			while (consumed.load(std::memory_order_acquire) < totalItems)
			{
				if (q.tryPop(&v))
				{
					seenCounts[v].fetch_add(1, std::memory_order_relaxed);
					consumed.fetch_add(1, std::memory_order_relaxed);
				}
				else
				{
					std::this_thread::yield();
				}
			} });
	}

	for (auto& t : producers)
		t.join();
	for (auto& t : consumers)
		t.join();

	CHECK(produced.load() == totalItems);
	CHECK(consumed.load() == totalItems);
	for (int i = 0; i < totalItems; ++i)
		REQUIRE(seenCounts[i].load() == 1);
}
