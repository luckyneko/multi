/*
 *  Created by LuckyNeko on 17/05/2020.
 *  Copyright 2020 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <catch2/catch.hpp>
#include <multi/details/queue.h>

TEST_CASE("multi::Queue")
{
	multi::Queue<int> testQueue;
	int out = 0;
	CHECK(testQueue.pop(&out) == false);

	testQueue.push(7);
	CHECK(testQueue.empty() == false);
	CHECK(testQueue.pop(&out) == true);
	CHECK(out == 7);
	CHECK(testQueue.empty() == true);

	testQueue.emplace(9);
	CHECK(testQueue.empty() == false);
	CHECK(testQueue.pop(&out) == true);
	CHECK(out == 9);
	CHECK(testQueue.empty() == true);

	CHECK(testQueue.pop(&out) == false);
}