/*
 *  Created by LuckyNeko on 15/03/2020.
 *  Copyright 2020 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
#include <multi/multi.h>

TEST_CASE("multi::context()")
{
	CHECK(multi::context() != nullptr);
	auto defaultContext = multi::context();

	multi::Context localContext;
	multi::context() = &localContext;
	CHECK(multi::context() == &localContext);

	multi::context() = defaultContext;
}

TEST_CASE("multi::start()")
{
	REQUIRE(multi::threadCount() == 0);
	multi::start(4);
	REQUIRE(multi::threadCount() == 4);
	multi::stop();
	REQUIRE(multi::threadCount() == 0);
}

TEST_CASE("multi::async()")
{
	REQUIRE(multi::threadCount() == 0);
	multi::start(4);
	REQUIRE(multi::threadCount() == 4);

	std::atomic<int> a(0);
	auto hdl = multi::async([&](multi::JobContext& jb) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		++a;
	});
	CHECK(a == 0);
	hdl.wait();
	CHECK(a == 1);

	multi::async([&](multi::JobContext& jb) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		++a;
	});
	CHECK(a == 2);

	multi::stop();
	REQUIRE(multi::threadCount() == 0);
}