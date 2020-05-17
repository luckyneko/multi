/*
 *  Created by LuckyNeko on 17/05/2020.
 *  Copyright 2020 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <catch2/catch.hpp>
#include <multi/details/jobnode.h>

TEST_CASE("multi::JobNode")
{
	int value = 0;
	auto func = [&value](multi::JobContext& jb) { ++value; };
	multi::JobNode* parent = new multi::JobNode(nullptr,
												func,
												new multi::JobNode(nullptr,
																   func,
																   new multi::JobNode(nullptr, func)));
	multi::JobNode* childA = new multi::JobNode(parent, func);
	multi::JobNode* childB = new multi::JobNode(parent, func);

	auto next = parent->run();
	CHECK(value == 1);
	REQUIRE(next == nullptr);

	next = childA->run();
	CHECK(value == 2);
	REQUIRE(next == parent);

	next = next->run();
	CHECK(value == 2);
	REQUIRE(next == nullptr);

	next = childB->run();
	CHECK(value == 3);
	REQUIRE(next == parent);

	next = next->run();
	CHECK(value == 3);
	REQUIRE(next != nullptr);

	next = next->run();
	CHECK(value == 4);
	REQUIRE(next != nullptr);

	next = next->run();
	CHECK(value == 5);
	CHECK(next == nullptr);
}