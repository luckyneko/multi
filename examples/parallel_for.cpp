// parallel_for.cpp — multi::each and multi::range over containers + indices.
//
// Covers:
//   - multi::each(begin, end, fn)            (per-item dispatch)
//   - multi::each(chunkPolicy, begin, end, fn) (chunked)
//   - multi::range(begin, end, fn)           (index range)
//   - multi::range(chunkPolicy, b, e, step, fn) (chunked index range)

#include <multi/multi.h>

#include <atomic>
#include <cstdio>
#include <vector>

int main()
{
	// Default pool: hardware_concurrency()-1 workers.
	multi::start();

	// each — mutate every element in place
	std::vector<int> v(100, 1);
	multi::each(v.begin(), v.end(), [](int& x) { x *= 3; });
	const bool eachOk = (v.front() == 3) && (v.back() == 3);
	std::printf("each: every element scaled by 3 (front=%d, back=%d) -> %s\n",
	            v.front(), v.back(), eachOk ? "ok" : "FAIL");

	// each with a ChunkPolicy — explicit chunk count for granularity control.
	// For 10k items + 16 chunks, each chunk processes ~625 items.
	std::vector<int> w(10000, 0);
	multi::each(16, w.begin(), w.end(), [](int& x) { x = 7; });
	const bool eachChunkOk = w.front() == 7 && w[5000] == 7 && w.back() == 7;
	std::printf("each(16 chunks, 10k items): all set to 7 -> %s\n",
	            eachChunkOk ? "ok" : "FAIL");

	// range — per-index dispatch, atomic accumulator
	std::atomic<int> sum(0);
	multi::range(0, 100, [&](int i) { sum += i; });
	const bool rangeOk = (sum.load() == 4950);
	std::printf("range(0,100) sum = %d (expected 4950) -> %s\n",
	            sum.load(), rangeOk ? "ok" : "FAIL");

	// range with a ChunkPolicy + step
	std::atomic<int> even(0);
	multi::range(8, 0, 100, 2, [&](int i) { even += i; });  // 0, 2, 4, …, 98
	const bool rangeStepOk = (even.load() == 2450);
	std::printf("range(8 chunks, 0..100, step 2) sum = %d (expected 2450) -> %s\n",
	            even.load(), rangeStepOk ? "ok" : "FAIL");

	multi::stop();
	return (eachOk && eachChunkOk && rangeOk && rangeStepOk) ? 0 : 1;
}
