/*
 *  Created by LuckyNeko on 17/05/2020.
 *  Copyright 2020 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <catch2/catch.hpp>
#include <multi/details/jobqueue.h>

TEST_CASE("multi::JobQueue")
{
	multi::JobQueue testQueue;
	multi::Job out;
	CHECK(testQueue.pop(&out) == false);
	CHECK(out.valid() == false);

	multi::Job in({[]() {}});
	CHECK(in.valid() == true);
	testQueue.push(in);

	// Pop
	CHECK(testQueue.pop(&out) == true);
	CHECK(out.valid() == true);
	CHECK(out == in);

	// Still valid until jobs run
	CHECK(testQueue.pop(&out) == true);
	CHECK(out.tryRun() == true);
	CHECK(testQueue.pop(&out) == false);
}