/*
 *  Created by LuckyNeko on 15/03/2020.
 *  Copyright 2020 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <array>
#include <catch2/catch.hpp>
#include <multi/details/job.h>

TEST_CASE("multi::Job")
{
	// Invalid Job
	multi::Job jb;
	CHECK(jb.valid() == false);
	CHECK(jb.hasWork() == false);
	CHECK(jb.complete() == true);

	// Create valid Job
	std::atomic<int> a(0);
	std::vector<multi::Task> tasks;
	tasks.push_back([&]()
					{ a++; });
	tasks.push_back([&]()
					{ a++; });
	jb = multi::Job(std::move(tasks));
	CHECK(jb.valid() == true);
	CHECK(jb.hasWork() == true);
	CHECK(jb.complete() == false);

	// task 1
	CHECK(jb.tryRun() == true);
	CHECK(jb.hasWork() == true);
	CHECK(jb.complete() == false);

	// task 2
	jb.waitRun();
	CHECK(jb.hasWork() == false);
	CHECK(jb.complete() == true);

	// Try post run
	CHECK(jb.tryRun() == false);
}