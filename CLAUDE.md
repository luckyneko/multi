# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

`multi` is a C++17 real-time threading framework distributed as a single static library. It's designed to be vendored (submodule / `add_subdirectory`) into a parent project; its tests and benchmarks only build when `multi` is the top-level CMake project.

## Build / test / bench

In-source builds are blocked by [CMakeLists.txt](CMakeLists.txt) — always use a separate build dir.

```sh
# Library + tests (default when standalone)
cmake -B build
cmake --build build
ctest --test-dir build --output-on-failure

# Run a single test by Catch2 name (tests are registered with prefix "(multi) ")
./build/test-multi "multi::async()"
./build/test-multi -# "[some-tag]"

# Benchmarks (off by default — opt in)
cmake -B build -DMULTI_BUILD_BENCHMARK=ON
cmake --build build
./build/bench-multi "[fast]"       # ~10s subset
./build/bench-multi "[slow]"       # mandelbrot, imbalanced
./build/bench-multi --benchmark-samples=20
./build/bench-multi --reporter xml --out before.xml
```

CMake options (both default ON when `multi` is top-level, OFF when consumed via `add_subdirectory`):
- `MULTI_BUILD_TESTING` — builds [test-multi](test/)
- `MULTI_BUILD_BENCHMARK` — builds [bench-multi](bench/)

Catch2 v3.14.0 is auto-downloaded into `thirdparty/` by [cmake/addcatch2.cmake](cmake/addcatch2.cmake) when `find_package(Catch2)` fails. `thirdparty/` and `build/` are gitignored.

Compile flags are strict: `/WX /W4` on MSVC, `-Werror -Wall -Wextra` elsewhere — warnings break the build. The repo ships a custom [.clang-format](.clang-format) (Allman braces, tabs, no column limit); VS Code is configured to format on save.

## Architecture

Three layers, public → private:

1. **Free functions** in [include/multi/multi.h](include/multi/multi.h) — `multi::start/stop/async/parallel/each/range` — all forward to a globally-installed `Context*` returned by `multi::context()` (settable; default points to a static instance in [src/multi.cpp](src/multi.cpp)). Tests swap the context pointer to exercise multiple `Context` instances; preserve that capability.

2. **`Context`** ([include/multi/context.h](include/multi/context.h), [include/multi/details/context.inl](include/multi/details/context.inl), [src/context.cpp](src/context.cpp)) owns one `WorkerPool` and implements the dispatch primitives. Templates live in the `.inl`.

3. **`WorkerPool`** ([include/multi/details/workerpool.h](include/multi/details/workerpool.h), [src/workerpool.cpp](src/workerpool.cpp)) owns N `Worker`s, each with its own [`WorkStealDeque`](include/multi/details/workstealdeque.h) + mutex + condvar, all cache-line aligned.

### Scheduling model

- **Submit**: round-robin onto worker deques via an atomic counter (`m_nextWorker.fetch_add`).
- **Local pop**: LIFO from the back of the owner's deque (temporal locality).
- **Steal**: FIFO from the front of another worker's deque, scanning `(self+1) % N … (self-1) % N` to spread contention.
- **Caller participation**: `Context::runQueueJob` (used by `parallel`/`each`/`range`) and `Handle::wait` both call `Context::tryRunSteal` while waiting, so the calling thread is an active participant — `bench/main.cpp` deliberately starts `hw-1` workers to leave room for the caller.
- **Single-threaded mode**: `WorkerPool::submit` / `submitBatch` run tasks inline on the caller when `!isActive()` — code written for the multi-threaded API works unchanged with no pool started. `each(taskCount, …)` and `range(taskCount, …)` also short-circuit to a serial loop when `taskCount <= 1`.

### Wakeup correctness

`WorkerPool::fencedNotify` does `lock(); unlock(); notify_one()` before any wake. The lock/unlock is a deliberate barrier that closes the race between a worker's predicate check and entering `cv.wait()` — without it, a notify issued in that window would be lost. Preserve this pattern when adding any new wake site (`stop`, `submit`, `submitBatch`).

`m_active` is the shutdown flag; the wait predicate checks both "got a task" and `!m_active`, so `stop()` flipping `m_active` and notifying every worker reliably drains them. Workers swallow exceptions from raw `submit`d tasks so a throw can't kill the worker thread.

### Job batching and exceptions

`parallel/each/range` build a `Job` on the caller's stack ([src/context.cpp](src/context.cpp)) holding `remaining` (atomic countdown), `firstException` (guarded by `std::call_once`), and the task vector. Wrappers capture `[&job, i]` — small enough for `std::function` SBO, no second heap allocation per task. The caller spins on `tryRunSteal` until `remaining == 0`, then rethrows the first captured exception. Sibling tasks are **not** cancelled on exception.

`async` ([context.inl](include/multi/details/context.inl)) builds a `shared_ptr<AsyncState>` holding the user functor + a `std::promise<void>`; the wrapper lambda only captures the shared_ptr (SBO-fits). `Handle` wraps a `shared_future` so `wait()` is idempotent and rethrows. **`Handle`'s destructor auto-waits and swallows** — to observe failures, call `wait()` explicitly. `Handle::operator=` also waits on the LHS before overwriting (preserves RAII even when reassigning); `detach()` releases without waiting.

### Type-system invariants

- `multi::range` `static_assert`s that `IDX` is signed — unsigned subtraction in the chunking math silently underflows.
- `each(taskCount, …)` and `range(taskCount, …)` use balanced distribution: first `extra = total % taskCount` chunks get `base+1`, the rest get `base`. Don't replace with simple ceiling-division — the comment in `range` documents the case (`total=15, N=14`) where ceiling collapses to fewer chunks than requested.

### When extending

- New synchronous primitives belong on `Context` (templated in `.inl`) with a thin forwarder in `multi.h`.
- New wake sites must use `fencedNotify`.
- Don't drop the `bool isActive()` inline-fallback path on `WorkerPool::submit*` — it's the single-threaded story.
- `WorkerPool::~WorkerPool` asserts `stop()` was called explicitly; release builds clean up defensively but the assert is the contract.
