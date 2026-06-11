// Downstream consumer smoke test — built against an installed `multi`
// package via `find_package(multi REQUIRED CONFIG)`. Exercises the
// install graph end-to-end (header dirs, alias target, version macros)
// without re-testing library behaviour itself.

#include <multi/multi.h>

#include <array>
#include <atomic>
#include <cstdio>

// Compile-time probe of the generated version header.
static_assert(MULTI_VERSION_MAJOR >= 0, "version major macro");
static_assert(MULTI_VERSION >= 0, "encoded version macro");

int main()
{
	std::printf("multi version: %s (encoded %d)\n",
	            MULTI_VERSION_STRING, MULTI_VERSION);

	multi::start(); // default pool: hardware_concurrency()-1 workers

	// Touch each major dispatch shape to make sure the installed headers
	// declare everything users will reach for.
	std::atomic<int> sum(0);
	multi::range(0, 1000, [&](int i) { sum += i; });

	std::array<int, 4> values = {1, 2, 3, 4};
	std::atomic<int> squareSum(0);
	multi::each(values.begin(), values.end(), [&](int x) { squareSum += x * x; });

	auto h = multi::async([]() { return 42; });
	multi::waitAll(h);
	int value = 0;
	h.get(&value);

	multi::stop();

	const bool ok = (sum.load() == 499500) && (squareSum.load() == 30) && (value == 42);
	std::printf("dispatch smoke: %s\n", ok ? "OK" : "FAIL");
	return ok ? 0 : 1;
}
