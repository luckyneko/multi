# multi [![CI](https://github.com/luckyneko/multi/actions/workflows/ci.yml/badge.svg?branch=master)](https://github.com/luckyneko/multi/actions/workflows/ci.yml)&nbsp; ![Release](https://img.shields.io/github/v/release/luckyneko/multi?include_prereleases)
C++17 real-time threading framework

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
For extra examples please see `tests/context.cpp`

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

Headline numbers — Apple M4 Pro (14 cores), macOS, Release, 20 samples. Times are mean per iteration; `chunks` uses `(threadCount+1)*8` chunks unless noted.

| Workload                  | serial   | items     | chunks   | chunks speedup |
|---------------------------|---------:|----------:|---------:|---------------:|
| `tiny_tasks` / 1k         |   403 µs |    144 µs |    79 µs |  **5.1×**      |
| `tiny_tasks` / 5k         |  2016 µs |    588 µs |   266 µs |  **7.6×**      |
| `tiny_tasks` / 50k        | 20157 µs |   5163 µs |  2084 µs |  **9.7×**      |
| `nested` (fork-join tree) | 11472 µs |     —     |  1111 µs |  **10.3×**     |
| `empty_tasks` / 1k        |    —     |    113 µs |    32 µs |    3.5× *      |
| `empty_tasks` / 5k        |    —     |    402 µs |    39 µs |   10.3× *      |
| `each` / 10k random-access|    —     |   1009 µs |   445 µs |    2.3×        |
| `each` / 10k map (bidi)   |    —     |   1020 µs |   518 µs |    2.0×        |
| `async_latency`           |    —     |     ~2 µs/round (single-task round-trip) |       —     |

\* `empty_tasks` measures raw dispatch overhead — the "chunks speedup" is overhead-vs-overhead, not work-throughput.

`tiny_tasks` is the most representative CPU-bound microbench: items-mode hits per-task wrapper + push/steal cost; chunks-mode amortises that to ~`(N_workers+1)·K` dispatches and approaches the serial limit / N_cores. Numbers shift across hardware (especially core count and memory subsystem) — re-run locally before drawing conclusions for your target.

Build and run:
```sh
cmake -B build -DMULTI_BUILD_BENCHMARK=ON
cmake --build build
./build/bench-multi          # all workloads
./build/bench-multi "[fast]" # quick subset (~10s): empty_tasks, tiny_tasks, nested, async_*
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
| `empty_tasks` | fast | Raw dispatch overhead (no work per task) |
| `tiny_tasks` | fast | Overhead relative to task duration; scales over 1k–50k tasks |
| `nested` | fast | Fork-join tree via `parallel_invoke`; includes serial baseline |
| `async_latency` | fast | `async` + `Handle::wait` round-trip cost |
| `async_fanout` | fast | N concurrent `async` tasks vs `range`-based dispatch |
| `mandelbrot` | slow | Uniform CPU-bound; the "well-behaved" case |
| `imbalanced` | slow | O(i) work per task; measures steal quality |

### Exception handling
Tasks that throw propagate the exception to the caller:
- `multi::async`: the first exception is stored on the returned `Handle`; calling `Handle::wait()` rethrows it.
- `multi::parallel` / `each` / `range`: the first exception thrown by any task is rethrown once all tasks have finished. Siblings are not cancelled.

`Handle`'s destructor swallows exceptions so dropping a Handle cannot call `std::terminate`; call `wait()` explicitly if you need to observe failures.
