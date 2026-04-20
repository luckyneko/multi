/*
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 *
 *  Custom main so multi::start/stop wrap the entire benchmark session.
 *  Benchmarking is always enabled in Catch2 v3 — no define required.
 *  Link against Catch2::Catch2 (not Catch2WithMain) to avoid duplicate main.
 */

#include <catch2/catch_all.hpp>

#include <multi/multi.h>

#include <thread>

int main(int argc, char* argv[])
{
	size_t hw = std::thread::hardware_concurrency();
	if (hw == 0)
		hw = 4;

	// multi uses (hw-1) workers; the calling thread participates in stealing,
	// giving hw effective parallel threads total.
	multi::start(hw > 0 ? hw - 1 : 1);

	int result = Catch::Session().run(argc, argv);

	multi::stop();
	return result;
}
