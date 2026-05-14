// Downstream consumer smoke test — built against an installed `multi`
// package via `find_package(multi REQUIRED CONFIG)`. Exercises the
// install graph end-to-end (header dirs, alias target, version macros)
// without re-testing library behaviour itself.

#include <multi/multi.h>

#include <array>
#include <atomic>
#include <cstdio>
#include <functional>
#include <thread>

// Compile-time probe of the generated version header.
static_assert(MULTI_VERSION_MAJOR >= 0, "version major macro");
static_assert(MULTI_VERSION >= 0, "encoded version macro");

int main()
{
	std::printf("multi version: %s (encoded %d)\n",
	            MULTI_VERSION_STRING, MULTI_VERSION);

	multi::start(std::thread::hardware_concurrency());

	// Touch each major dispatch shape to make sure the installed headers
	// declare everything users will reach for.
	std::atomic<int> sum(0);
	multi::range(0, 1000, [&](int i) { sum += i; });

	std::array<int, 4> values = {1, 2, 3, 4};
	auto squareSum = multi::transformReduce(
		values.begin(), values.end(),
		0, std::plus<>{}, [](int x) { return x * x; });

	auto h = multi::async([]() { return 42; });
	const int value = h.get();

	multi::stop();

	const bool ok = (sum.load() == 499500) && (squareSum == 30) && (value == 42);
	std::printf("dispatch smoke: %s\n", ok ? "OK" : "FAIL");
	return ok ? 0 : 1;
}
