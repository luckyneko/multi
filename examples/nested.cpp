// nested.cpp — recursive parallel divide-and-conquer.
//
// Covers:
//   - Submitting tasks from inside a worker task (nested dispatch)
//   - Using parallelAsync to spawn sibling tasks that return values
//   - waitAll to block (with steal participation) until siblings done
//   - Recursion-depth control with a serial fallback below a threshold

#include <multi/multi.h>

#include <cstdio>
#include <numeric>
#include <vector>

namespace
{
	// Parallel sum via divide-and-conquer. Below the cutoff we fall back
	// to serial std::accumulate; otherwise split the range in half and
	// spawn two sibling tasks via parallelAsync, then waitAll on the
	// tuple. waitAll lets the calling worker drain the pool while
	// waiting on its children — important because the caller IS a
	// worker thread here.
	long long parallelSum(const std::vector<long long>& v, std::size_t lo, std::size_t hi)
	{
		constexpr std::size_t cutoff = 4096;
		if (hi - lo <= cutoff)
			return std::accumulate(v.begin() + lo, v.begin() + hi, 0LL);

		const std::size_t mid = lo + (hi - lo) / 2;
		auto children = multi::parallelAsync(
			[&]() { return parallelSum(v, lo, mid); },
			[&]() { return parallelSum(v, mid, hi); });
		multi::waitAll(children);

		auto& [hL, hR] = children;
		long long l = 0, r = 0;
		hL.get(&l);
		hR.get(&r);
		return l + r;
	}
} // namespace

int main()
{
	// Default pool: hardware_concurrency()-1 workers.
	multi::start();

	std::vector<long long> v(100000);
	std::iota(v.begin(), v.end(), 1LL);  // 1..100000

	const long long sum = parallelSum(v, 0, v.size());
	const long long expected = 100000LL * 100001LL / 2LL;  // 5,000,050,000
	const bool ok = (sum == expected);
	std::printf("nested divide-and-conquer sum 1..100000 = %lld (expected %lld) -> %s\n",
	            sum, expected, ok ? "ok" : "FAIL");

	multi::stop();
	return ok ? 0 : 1;
}
