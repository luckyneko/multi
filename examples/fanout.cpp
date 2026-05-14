// fanout.cpp — multi::parallelAsync + multi::waitAll / waitAny.
//
// Covers:
//   - parallelAsync for heterogeneous typed tasks → tuple<Handle<R>...>
//   - waitAll on a tuple (participates in stealing while waiting)
//   - waitAny returning the index of the first complete handle
//   - Homogeneous fanout via std::vector<Handle<T>>

#include <multi/multi.h>

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

int main()
{
	multi::start(std::thread::hardware_concurrency());

	// Heterogeneous fan-out: each lambda has its own return type. The
	// tuple's element types are deduced from invoke_result of each.
	auto group = multi::parallelAsync(
		[]() { return 7; },
		[]() { return 3.14; },
		[]() { return std::string("multi"); });

	// waitAll blocks until every handle completes, participating in
	// work-stealing while we wait. Returns void; observe results with .get().
	multi::waitAll(group);

	auto& [hInt, hDbl, hStr] = group;
	std::printf("parallelAsync results: %d, %f, %s\n",
	            hInt.get(), hDbl.get(), hStr.get().c_str());
	const bool tupleOk = (hInt.get() == 7) && (hStr.get() == "multi");

	// Homogeneous fanout via vector of Handle<int>. Iteration uses plain
	// Handle::get() per element; for participating-while-waiting, call
	// multi::waitAll on each (or batch them into a tuple).
	std::vector<multi::Handle<int>> squares;
	for (int i = 0; i < 8; ++i)
		squares.push_back(multi::async([i]() { return i * i; }));

	long long total = 0;
	for (auto& h : squares)
		total += h.get();
	const bool sumOk = (total == 0 + 1 + 4 + 9 + 16 + 25 + 36 + 49);  // = 140
	std::printf("sum of squares 0..7 = %lld (expected 140) -> %s\n",
	            total, sumOk ? "ok" : "FAIL");

	// waitAny: spawn three handles, two slow + one fast. Pre-complete
	// the fast one via plain wait() so we get a deterministic index
	// (otherwise the caller-side stealing could pull the slow ones onto
	// this thread and run them first).
	auto slowA = multi::async([]() { std::this_thread::sleep_for(std::chrono::milliseconds(50)); return 'a'; });
	auto slowB = multi::async([]() { std::this_thread::sleep_for(std::chrono::milliseconds(80)); return 'b'; });
	auto fast  = multi::async([]() { return 'F'; });
	fast.wait();
	const std::size_t firstDone = multi::waitAny(slowA, slowB, fast);
	std::printf("waitAny first-complete index = %zu (expected 2)\n", firstDone);
	const bool waitAnyOk = (firstDone == 2);

	slowA.wait();  // tidy up slow siblings
	slowB.wait();

	multi::stop();
	return (tupleOk && sumOk && waitAnyOk) ? 0 : 1;
}
