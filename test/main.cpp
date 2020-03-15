#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>

#include "details/queue.h"

TEST_CASE( "Test Queue", "[queue]" )
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