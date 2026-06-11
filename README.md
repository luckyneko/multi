# multi [![CI](https://github.com/luckyneko/multi/actions/workflows/ci.yml/badge.svg?branch=master)](https://github.com/luckyneko/multi/actions/workflows/ci.yml)&nbsp; [![Docs](https://github.com/luckyneko/multi/actions/workflows/docs.yml/badge.svg?branch=master)](https://luckyneko.github.io/multi/)&nbsp; ![Release](https://img.shields.io/github/v/release/luckyneko/multi?include_prereleases)
C++17 real-time threading framework

API reference (Doxygen, regenerated from `master` on push): **<https://luckyneko.github.io/multi/>**

## Build / Integrate

multi is a static library only. It is **recommended** to submodule or copy into your repo.

### To build stand-alone:
``` sh
git clone https://github.com/luckyneko/multi.git
cd multi
cmake -B build
cmake --build build
```

### To add to your CMakeLists.txt
``` cmake
add_subdirectory("path/to/multi")
target_link_libraries(${PROJECT_NAME} multi)
```

## Tested Platforms

Continuously built and tested on:
- Linux (GCC 13 & 14, Clang 17 & 18; Debug + Release)
- macOS 15 (AppleClang; Debug + Release)
- Windows (MSVC 2022 & 2025; Debug + Release)
- Linux ThreadSanitizer (Clang 18, `-fsanitize=thread`)

Continuously cross-compiled on (build only — runtime not exercised in CI):
- Android NDK (arm64-v8a, API 21+)
- iOS (arm64, SDK 15+)

## Features
- Simple in-line API
- Will run on local-thread as well as workers
- Single threaded mode
- Only write functions once for single & multi threaded

## Examples

Self-contained example programs live in [examples/](examples/). Each one is
self-validating (prints what it does and exits non-zero on failure), so they
double as a quick smoke test. Build with `-DMULTI_BUILD_EXAMPLES=ON` (default
ON when standalone, OFF when consumed via `add_subdirectory`):

```sh
cmake -B build
cmake --build build
./build/example-hello          # basic start/stop + async
./build/example-parallel_for   # each + range, with and without taskCount
./build/example-fanout         # parallelAsync + waitAll/waitAny
./build/example-nested         # recursive divide-and-conquer with waitAll
```

For deeper API coverage see [test/context.cpp](test/context.cpp).

### Basic Async
``` C++
#include <multi/multi.h>
#include <atomic>
#include <thread>

int main()
{
    // Start with hardware thread count
    multi::start(std::thread::hardware_concurrency());

    // Run job
    std::atomic<int> i(0);
    multi::Handle jobHdl = multi::async([&]()
    {
        ++i;
    });

    // Wait for job to complete
    jobHdl.wait();

    // async will automatically wait if handle not captured
    multi::async([&]()
    {
        ++i;
    });

    // Tasks that return a value: the Handle's template parameter is
    // deduced from the functor's return type. get() blocks (rethrowing
    // on failure) and returns the value.
    multi::Handle<int> answer = multi::async([]() { return 42; });
    int result = answer.get();

    // Bounded wait — useful for "check, then keep doing something else"
    // loops. Returns std::future_status::ready or ::timeout.
    auto slow = multi::async([]() { /* …long… */ });
    if (slow.wait_for(std::chrono::milliseconds(5)) == std::future_status::timeout)
    {
        // come back later
    }

    // Handle::wait()/get()/wait_for blocks the calling thread plainly.
    // If you want the caller to help drain the pool while waiting on a
    // specific Handle — useful e.g. when the caller is a worker thread
    // that submitted nested work — use waitAll:
    auto child = multi::async([&]() { /* … */ });
    multi::waitAll(child);   // participates in work-stealing until child completes
    child.get();                // observe value/exception

    // Wait on multiple handles at once. waitAll returns after every
    // handle completes; waitAny returns the index of the first one
    // to complete. Both participate in stealing internally.
    auto h1 = multi::async([]() { return 1; });
    auto h2 = multi::async([]() { return 2.0; });
    multi::waitAll(h1, h2);
    // ... or fanned out from parallelAsync:
    auto group = multi::parallelAsync([]{ return 'a'; }, []{ return 7; });
    multi::waitAll(group);
    std::size_t firstDone = multi::waitAny(group);

    // Generalised primitive for waiting on arbitrary conditions with
    // caller participation in the pool — `waitAll`/`waitAny` are all
    // thin wrappers over it.
    std::atomic<int> remaining{N};
    multi::waitUntil([&]{ return remaining.load() == 0; });

    // Wait for all jobs, and close threads.
    multi::stop();
}
```

### Parallel tasks
``` C++
void function()
{
    // Run 2 tasks side by side
    multi::parallel(
        // Task 1
        []()
        {
        },
        // Task 2
        []()
        {
        }
    );
}
```

### Parallel fan-out with per-task results
``` C++
void function()
{
    // Fan out heterogeneous siblings — each functor gets its own
    // Handle<R>, packaged in a tuple. Use when you need per-task
    // results, different return types, or per-handle wait_for.
    // Contrast with `parallel(a, b, ...)` which is fire-and-block.
    auto handles = multi::parallelAsync(
        []() { return 42; },
        []() { return std::string("hello"); },
        []() { return 3.14; });

    auto& [hInt, hStr, hDbl] = handles;
    int i = hInt.get();          // blocks, rethrows on failure
    std::string s = hStr.get();
    double d = hDbl.get();

    // Sibling exceptions are isolated per-handle (unlike `parallel`
    // which captures only the first across all siblings).
}
```

### Run function on Each item
``` C++
void function()
{
    std::vector<Item> items;

    // Run task per item
    multi::each(items.begin(), items.end(), [](Item& item)
    {
        item.setValue();
    });

    // Run over all items, using 32 tasks
    multi::each(32, items.begin(), items.end(), [](Item& item)
    {
        item.setValue();
    });
}
```

### Run function on range of numbers with step
``` C++
void function()
{
    std::atomic<int> out(0);

    // Run task per index (step defaults to 1)
    multi::range(0, 100, [&](int idx)
    {
        out += idx;
    });

    // Run task per step
    multi::range(0, 100, 2, [&](int idx)
    {
        out += idx;
    });

    // Run task per step, using 32 tasks
    multi::range(32, 0, 100, 2, [&](int idx)
    {
        out += idx;
    });
}
```

## Benchmarks

Headline numbers — AMD Ryzen 9 5950X (16 cores / 32 threads), Windows 11, MSVC Release, 20 samples. The pool runs `hardware_concurrency()-1 = 31` workers plus the calling thread; `chunks`-mode uses `(threadCount+1)*CHUNK_FACTOR = 128` chunks (`CHUNK_FACTOR = 4`). Times are mean per iteration.

| Workload                      | serial   | items    | chunks   | speedup   |
|-------------------------------|---------:|---------:|---------:|----------:|
| `tiny_tasks` / 1k             |   387 µs |   114 µs |    74 µs | **5.3×**  |
| `tiny_tasks` / 50k            |  19.4 ms |  2.78 ms |   771 µs | **25×**   |
| `nested` (fork-join tree)     |  10.3 ms |    —     |   491 µs | **21×**   |
| `heavy_capture` / 50k         |    —     |  2.09 ms |   173 µs | 12× †     |
| `each_iter` / 50k vec (RA)    |    —     |  2.07 ms |   753 µs | 2.8× †    |
| `each_iter` / 50k map (bidi)  |    —     |  3.14 ms |  2.05 ms | 1.5× †    |
| `empty_tasks` / 1k            |    —     |   118 µs |    15 µs | 7.9× *    |
| `empty_tasks` / 50k           |    —     |  2.68 ms |    65 µs | 41× *     |
| `imbalanced` / 200 (slow)     |   128 ms |  4.79 ms |  4.96 ms | 26×       |
| `mandelbrot` (slow)           |   597 ms |  24.3 ms |  27.2 ms | 22×       |

Per-dispatch latencies (no serial comparison): `async_latency` ≈ **5.5 µs** per serial `async` + `Handle::wait` round-trip; `parallel_pair` ≈ **1.5 µs** per `parallel(a, b)` call.

\* `empty_tasks` measures raw dispatch overhead — its speedup is overhead-vs-overhead, not work-throughput.

† No serial baseline (these benches contrast the two parallel paths); the speedup is chunked dispatch vs one-task-per-item.

`tiny_tasks` is the most representative CPU-bound microbench: items-mode pays per-task wrapper + push/steal cost on every element, while chunks-mode amortises that to ~`(N_workers+1)·CHUNK_FACTOR` dispatches and approaches the serial-time / N_cores limit. For the irregular workloads (`imbalanced`, `mandelbrot`) items-mode actually edges out chunks — one task per item gives the finest steal granularity. Numbers shift across hardware (especially core count and memory subsystem) — re-run locally before drawing conclusions for your target.

Build and run:
```sh
cmake -B build -DMULTI_BUILD_BENCHMARK=ON
cmake --build build
./build/bench-multi          # all workloads
./build/bench-multi "[fast]" # quick subset: everything except mandelbrot / imbalanced
./build/bench-multi "[slow]" # longer subset: mandelbrot, imbalanced
```

Common options:
```sh
# Fewer samples for a quick pass (default is 100)
./build/bench-multi --benchmark-samples=20

# Save results as XML or JSON for diffing between runs
./build/bench-multi --reporter xml --out before.xml
./build/bench-multi --reporter json --out before.json

# Adjust warmup window (default 200ms)
./build/bench-multi --benchmark-warmup-time=500

# List available test cases
./build/bench-multi --list-tests
```

Workloads:
| Test case | Tag | Measures |
|---|---|---|
| `empty_tasks` | fast | Raw dispatch overhead (≈no work per task); 1k vs 50k tasks |
| `tiny_tasks` | fast | Overhead relative to task duration; 1k vs 50k tasks |
| `heavy_capture` | fast | Chunked-dispatch cost when the user functor's capture exceeds `std::function` SBO |
| `each_iter` | fast | `each` over vector (random-access) vs map (bidirectional); 1k vs 50k items |
| `parallel_pair` | fast | `parallel(a, b)` two-task dispatch on the hot path |
| `nested` | fast | Fork-join tree via `parallel`; includes serial baseline |
| `async_latency` | fast | Serial `async` + `Handle::wait` round-trip cost |
| `async_fanout` | fast | N concurrent `async` tasks, collected then waited |
| `steal_contention` | fast | Multiple external driver threads issuing parallel work at once |
| `mandelbrot` | slow | Uniform CPU-bound; the "well-behaved" case |
| `imbalanced` | slow | O(i) work per task; measures steal quality |

### Exception handling
Tasks that throw propagate the exception to the caller:
- `multi::async`: the first exception is stored on the returned `Handle`; calling `Handle::wait()` rethrows it.
- `multi::parallel` / `each` / `range`: the first exception thrown by any task is rethrown once all tasks have finished. Siblings are not cancelled.

`Handle`'s destructor swallows exceptions so dropping a Handle cannot call `std::terminate`; call `wait()` explicitly if you need to observe failures.
