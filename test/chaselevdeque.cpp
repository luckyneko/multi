/*
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <atomic>
#include <catch2/catch_all.hpp>
#include <multi/details/chaselevdeque.h>
#include <thread>
#include <vector>

TEST_CASE("multi::ChaseLevDeque single-threaded")
{
	SECTION("empty")
	{
		multi::ChaseLevDeque<int, 8> d;
		CHECK(d.sizeHint() == 0);
		int v = -1;
		CHECK_FALSE(d.tryPopBottom(&v));
		CHECK_FALSE(d.trySteal(&v));
	}

	SECTION("LIFO pop")
	{
		multi::ChaseLevDeque<int, 8> d;
		for (int i = 0; i < 5; ++i)
			REQUIRE(d.tryPushBottom(int(i)));
		CHECK(d.sizeHint() == 5);

		int v = 0;
		for (int i = 4; i >= 0; --i)
		{
			REQUIRE(d.tryPopBottom(&v));
			CHECK(v == i);
		}
		CHECK(d.sizeHint() == 0);
		CHECK_FALSE(d.tryPopBottom(&v));
	}

	SECTION("FIFO steal")
	{
		multi::ChaseLevDeque<int, 8> d;
		for (int i = 0; i < 5; ++i)
			REQUIRE(d.tryPushBottom(int(i)));

		int v = 0;
		for (int i = 0; i < 5; ++i)
		{
			REQUIRE(d.trySteal(&v));
			CHECK(v == i);
		}
		CHECK_FALSE(d.trySteal(&v));
	}

	SECTION("full at capacity")
	{
		multi::ChaseLevDeque<int, 4> d;
		for (int i = 0; i < 4; ++i)
			REQUIRE(d.tryPushBottom(int(i)));
		// Capacity reached.
		CHECK_FALSE(d.tryPushBottom(99));

		int v = 0;
		REQUIRE(d.tryPopBottom(&v)); // frees one slot
		REQUIRE(d.tryPushBottom(99));
		CHECK_FALSE(d.tryPushBottom(100));
	}

	SECTION("wraparound past capacity")
	{
		multi::ChaseLevDeque<int, 4> d;
		int v = 0;
		// Push/pop > capacity times to force the bottom index past Capacity.
		for (int round = 0; round < 20; ++round)
		{
			REQUIRE(d.tryPushBottom(int(round)));
			REQUIRE(d.tryPopBottom(&v));
			CHECK(v == round);
		}
	}

	SECTION("pop drains down to last element via CAS")
	{
		multi::ChaseLevDeque<int, 8> d;
		REQUIRE(d.tryPushBottom(42));
		int v = 0;
		REQUIRE(d.tryPopBottom(&v));
		CHECK(v == 42);
		CHECK(d.sizeHint() == 0);
	}
}

TEST_CASE("multi::ChaseLevDeque concurrent steal", "[stress]")
{
	// Owner produces N items and pops some; M stealers grab the rest.
	// Every produced value must be observed exactly once across owner+stealers.
	constexpr std::size_t CAP = 1024;
	multi::ChaseLevDeque<int, CAP> d;

	const int totalItems = 50000;
	const int stealerCount = 4;

	std::vector<std::atomic<int>> seenCounts(totalItems);
	for (auto& c : seenCounts)
		c.store(0, std::memory_order_relaxed);

	std::atomic<bool> producerDone{false};
	std::atomic<int> consumed{0};

	std::vector<std::thread> stealers;
	for (int s = 0; s < stealerCount; ++s)
	{
		stealers.emplace_back([&]()
							  {
			int v;
			while (!producerDone.load(std::memory_order_acquire) || consumed.load() < totalItems)
			{
				if (d.trySteal(&v))
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

	std::thread owner([&]()
					  {
		int next = 0;
		int v;
		while (next < totalItems)
		{
			// Push up to a chunk, then pop some, to keep occupancy bounded.
			int batch = 0;
			while (batch < 64 && next < totalItems)
			{
				if (d.tryPushBottom(int(next)))
				{
					++next;
					++batch;
				}
				else
				{
					// Full: yield and let stealers drain.
					std::this_thread::yield();
				}
			}
			// Owner also pops some of its own work (LIFO).
			for (int i = 0; i < 8; ++i)
			{
				if (d.tryPopBottom(&v))
				{
					seenCounts[v].fetch_add(1, std::memory_order_relaxed);
					consumed.fetch_add(1, std::memory_order_relaxed);
				}
				else
				{
					break;
				}
			}
		}
		// Drain anything left.
		while (d.tryPopBottom(&v))
		{
			seenCounts[v].fetch_add(1, std::memory_order_relaxed);
			consumed.fetch_add(1, std::memory_order_relaxed);
		}
		producerDone.store(true, std::memory_order_release); });

	owner.join();
	for (auto& t : stealers)
		t.join();

	CHECK(consumed.load() == totalItems);
	for (int i = 0; i < totalItems; ++i)
		REQUIRE(seenCounts[i].load() == 1);
}
