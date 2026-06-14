/*
 *  Created by LuckyNeko on 07/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <atomic>
#include <catch2/catch_all.hpp>
#include <memory>
#include <multi/details/mpmcqueue.h>
#include <thread>
#include <vector>

TEST_CASE("MpmcQueue: empty")
{
	multi::details::MpmcQueue<int, 8> q;
	CHECK(q.sizeHint() == 0);
	int v = -1;
	CHECK_FALSE(q.tryPop(&v));
}

TEST_CASE("MpmcQueue: FIFO order")
{
	multi::details::MpmcQueue<int, 8> q;
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
	multi::details::MpmcQueue<int, 4> q;
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
	multi::details::MpmcQueue<int, 4> q;
	int v = 0;
	for (int round = 0; round < 20; ++round)
	{
		REQUIRE(q.tryPush(int(round)));
		REQUIRE(q.tryPop(&v));
		CHECK(v == round);
	}
}

TEST_CASE("MpmcQueue: move-only type preserves FIFO and ownership")
{
	multi::details::MpmcQueue<std::unique_ptr<int>, 8> q;
	for (int i = 0; i < 5; ++i)
		REQUIRE(q.tryPush(std::make_unique<int>(i)));

	std::unique_ptr<int> out;
	for (int i = 0; i < 5; ++i)
	{
		REQUIRE(q.tryPop(&out));
		REQUIRE(out != nullptr);
		CHECK(*out == i);
	}
	CHECK_FALSE(q.tryPop(&out));
}

TEST_CASE("MpmcQueue: failed push leaves the value intact for cascading")
{
	multi::details::MpmcQueue<std::unique_ptr<int>, 4> q;
	for (int i = 0; i < 4; ++i)
		REQUIRE(q.tryPush(std::make_unique<int>(i)));

	// Ring full: the push must fail without consuming the lvalue, so the caller
	// can cascade it elsewhere (WorkStealDeque::tryPushLocal relies on this).
	auto item = std::make_unique<int>(99);
	REQUIRE_FALSE(q.tryPush(item));
	REQUIRE(item != nullptr);
	CHECK(*item == 99);

	// Make room; the retried push now succeeds and moves the value out.
	std::unique_ptr<int> out;
	REQUIRE(q.tryPop(&out));
	REQUIRE(q.tryPush(item));
	CHECK(item == nullptr);
}

TEST_CASE("MpmcQueue: concurrent producers and consumers stress", "[stress]")
{
	constexpr std::size_t CAP = 1024;
	multi::details::MpmcQueue<int, CAP> q;

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
