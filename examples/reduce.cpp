// reduce.cpp — multi::reduce and multi::transformReduce.
//
// Covers:
//   - multi::reduce(begin, end, init, op)        (parallel fold)
//   - multi::transformReduce(begin, end, init, reduceOp, transformOp)
//
// reduceOp must be associative AND commutative — the library combines
// partials in unspecified order. T must be default-constructible (vector
// of partials).

#include <multi/multi.h>

#include <cstdio>
#include <functional>
#include <numeric>
#include <thread>
#include <vector>

int main()
{
	multi::start(std::thread::hardware_concurrency());

	// 1..10000 — fits comfortably in a long long sum.
	std::vector<long long> v(10000);
	std::iota(v.begin(), v.end(), 1LL);

	const long long sum = multi::reduce(v.begin(), v.end(), 0LL, std::plus<>{});
	const long long expectedSum = 10000LL * 10001LL / 2LL;  // = 50,005,000
	const bool sumOk = (sum == expectedSum);
	std::printf("reduce sum 1..10000 = %lld (expected %lld) -> %s\n",
	            sum, expectedSum, sumOk ? "ok" : "FAIL");

	// Sum of squares — same shape with a per-element transform.
	const long long sumSq = multi::transformReduce(
		v.begin(), v.end(),
		0LL,
		std::plus<>{},
		[](long long x) { return x * x; });
	// sum_{i=1..N} i² = N(N+1)(2N+1)/6
	const long long N = 10000LL;
	const long long expectedSq = N * (N + 1LL) * (2LL * N + 1LL) / 6LL;
	const bool sqOk = (sumSq == expectedSq);
	std::printf("transformReduce sum of squares = %lld (expected %lld) -> %s\n",
	            sumSq, expectedSq, sqOk ? "ok" : "FAIL");

	multi::stop();
	return (sumOk && sqOk) ? 0 : 1;
}
