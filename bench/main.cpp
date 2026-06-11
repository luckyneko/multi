/*
 *  Created by LuckyNeko on 20/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <catch2/catch_all.hpp>

#include <multi/multi.h>

int main(int argc, char* argv[])
{
	// multi uses (hw-1) workers; the calling thread participates in stealing,
	// giving hw effective parallel threads total.
	multi::start(std::max(int(std::thread::hardware_concurrency()), 4) - 1);
	int result = Catch::Session().run(argc, argv);
	multi::stop();
	return result;
}
