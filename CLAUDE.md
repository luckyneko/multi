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

# Run a single test by Catch2 name. ctest sees them with the "(multi) " prefix
# from catch_discover_tests; the binary itself takes the raw TEST_CASE name.
./build/test-multi "Context: reduce sums an integer vector across thread counts"
./build/test-multi -# "[some-tag]"

# Benchmarks (off by default — opt in)
cmake -B build -DMULTI_BUILD_BENCHMARK=ON
cmake --build build
./build/bench-multi "[fast]"       # ~10s subset
./build/bench-multi "[slow]"       # mandelbrot, imbalanced
./build/bench-multi --benchmark-samples=20
./build/bench-multi --reporter xml --out before.xml
```

CMake options (all default ON when `multi` is top-level, OFF when consumed via `add_subdirectory`):
- `MULTI_BUILD_TESTING` — builds [test-multi](test/)
- `MULTI_BUILD_BENCHMARK` — builds [bench-multi](bench/)
- `MULTI_INSTALL` — emits `install()` / `export()` rules so downstream `find_package(multi REQUIRED CONFIG)` works against a `cmake --install` tree. Provides the namespaced `multi::multi` alias either way.

Generated header: `multi/version.h` is produced from [include/multi/version.h.in](include/multi/version.h.in) by `configure_file(@ONLY)` into `<build>/include/multi/version.h`. Exposes `MULTI_VERSION_MAJOR/MINOR/PATCH/STRING` macros, an encoded `MULTI_VERSION` for `#if` checks, and matching `inline constexpr` accessors in namespace `multi`. The build tree's `include/` is on the `BUILD_INTERFACE` include path; the generated copy is installed alongside the static headers.

Catch2 v3.14.0 is auto-downloaded into `thirdparty/` by [cmake/addcatch2.cmake](cmake/addcatch2.cmake) when `find_package(Catch2)` fails. `thirdparty/` and `build/` are gitignored.

Compile flags are strict: `/WX /W4` on MSVC, `-Werror -Wall -Wextra` elsewhere — warnings break the build. The repo ships a custom [.clang-format](.clang-format) (Allman braces, tabs, no column limit); VS Code is configured to format on save.

## Architecture

Three layers, public → private:

1. **Free functions** in [include/multi/multi.h](include/multi/multi.h) — `multi::start/stop/async/parallel/parallel_async/each/range/reduce/transform_reduce/stealWhile` — all forward to a globally-installed `Context*` returned by `multi::context()` (settable; default points to a static instance in [src/multi.cpp](src/multi.cpp)). Tests swap the context pointer to exercise multiple `Context` instances; preserve that capability. The setter is unsynchronised — its implicit contract is "set once before any concurrent use".

2. **`Context`** ([include/multi/context.h](include/multi/context.h), [include/multi/details/context.inl](include/multi/details/context.inl), [src/context.cpp](src/context.cpp)) owns one `WorkerPool` and implements the dispatch primitives. Templates live in the `.inl`.

3. **`WorkerPool`** ([include/multi/details/workerpool.h](include/multi/details/workerpool.h), [src/workerpool.cpp](src/workerpool.cpp)) owns N `Worker`s, each with its own [`WorkStealDeque`](include/multi/details/workstealdeque.h) + mutex + condvar, all cache-line aligned.

### WorkStealDeque composition

`WorkStealDeque` is two lock-free primitives stitched together, no mutex:
- A fixed-capacity Chase-Lev SPMC deque ([`ChaseLevDeque<Task, 256>`](include/multi/details/chaselevdeque.h)) — owner pushes/pops at the bottom, any thread steals from the top. Per-slot sequence atomics gate slot reuse: the owner's `tryPushBottom` waits for the previous occupant to release the slot before overwriting, which is what makes claim-first stealing safe for move-only `Task`. Released to position `b` after a pop, position `t + Capacity` after a steal — pop reuses the same slot, steals don't.
- A bounded MPMC ring ([`MpmcQueue<Task, 4096>`](include/multi/details/mpmcqueue.h)) — Vyukov sequence-numbered ring used for spillover and external submits. Visible to stealers as a fallback so externally-submitted work isn't trapped behind a busy owner.

`pop()` refills local from overflow when local size drops below `REFILL_LOW=16` (up to `REFILL_BATCH=32` items), then pops local LIFO; falls through to draining one from overflow when local is empty. `steal()` tries local Chase-Lev first, then overflow.

### Scheduling model

- **Submit routing**: a `thread_local` worker index tells `submit`/`submitBatch` whether the caller is a worker. If round-robin lands on the calling worker's own index, the task goes via `WorkStealDeque::tryPushLocal` (Chase-Lev SPSC fast path, no MPMC contention, no notify). Otherwise it goes via `tryPushRemote` (overflow ring) and the target worker is notified. Round-robin via `m_nextWorker.fetch_add` is unchanged; only the path through the deque differs.
- **Push-region claim**: `submit` increments `m_inFlight` after the first `isActive()` check and before touching `m_workers[idx]`. `stop()` flips `m_active` then spins on `m_inFlight` reaching zero before clearing `m_workers`. The `m_inFlight` slot is what keeps the `Worker` storage alive against concurrent shutdown — the lock-free deque internals don't substitute for this.
- **Overflow-full spin**: `pushWithRetry` spin-yields if `tryPushLocal`/`tryPushRemote` returns false (the bounded ring is full). The spin also checks `isActive()` and bails to inline execution on shutdown — without this, `stop()` would deadlock waiting on `m_inFlight`.
- **Local pop**: LIFO from the back of the owner's local Chase-Lev (temporal locality preserved).
- **Steal**: FIFO from the front of another worker's local Chase-Lev (then its overflow ring as a fallback), scanning `(self+1) % N … (self-1) % N` to spread contention.
- **Caller participation**: `Context::runQueueJob` (used by `parallel`/`each`/`range`/`reduce`/`transform_reduce`) calls `Context::tryRunSteal` while spinning on `remaining()`, so the calling thread is an active participant — `bench/main.cpp` deliberately starts `hw-1` workers to leave room for the caller. **`Handle::wait` does NOT participate in stealing** — `Handle` is Context-agnostic and blocks plainly. Callers that want to help drain the pool while waiting on a specific async result call `Context::stealWhile(handle)` explicitly. This split exists so timed waits (`wait_for`/`wait_until`) honour their deadline (a steal-participate wait can pull the very task it's waiting on and be obliged to run it past the deadline), and so the cross-context handle-passing story works. Trade-off: anonymous nested `async` inside a worker task on a 1-worker context deadlocks unless the worker calls `stealWhile` (was self-healing before the post-typed-Handle refactor; intentional change).
- **Single-threaded mode**: `WorkerPool::submit` / `submitBatch` run tasks inline on the caller when `!isActive()` — code written for the multi-threaded API works unchanged with no pool started. `each(taskCount, …)` and `range(taskCount, …)` also short-circuit to a serial loop when `taskCount <= 1`.

`WorkerPool::dequeOf(idx)` returns a `const WorkStealDeque&` for tests/benchmarks; `WorkStealDeque::localSizeHint()` and `overflowSizeHint()` expose the two halves separately for routing diagnostics.

### Wakeup correctness

`WorkerPool::fencedNotify` does `lock(); unlock(); notify_one()` before any wake. The lock/unlock is a deliberate barrier that closes the race between a worker's predicate check and entering `cv.wait()` — without it, a notify issued in that window would be lost. Preserve this pattern when adding any new wake site (`stop`, `submit`, `submitBatch`).

`m_active` is the shutdown flag; the wait predicate checks both "got a task" and `!m_active`, so `stop()` flipping `m_active` and notifying every worker reliably drains them. Workers swallow exceptions from raw `submit`d tasks so a throw can't kill the worker thread.

### Job dispatch

`parallel`/`each`/`range`/`reduce`/`transform_reduce`/`async` each construct a typed `Job` subclass that owns the dispatch state for one call. Subclasses live in [include/multi/details/job.h](include/multi/details/job.h):
- `RangeJob<IDX,FUNC>` / `ChunkedRangeJob<IDX,FUNC>` — index-only dispatch, no per-task storage. Chunk slice bounds are computed inside `run(i)` from `(begin, step, base, extra)`.
- `EachJob<ITER,FUNC>` / `ChunkedEachJob<ITER,FUNC>` — iterator-category split via `if constexpr`: random-access iterators (vector, array, raw pointers) store the begin iterator directly with no allocation; other categories (std::map, std::list, etc.) materialise a `std::vector<T*>` pointer table at construction. Indexed via `m_storage[i]`.
- `ParallelJob<Fs...>` — `std::tuple<Fs...>` plus an index-pack fold-expression dispatcher.
- `TransformReduceJob<ITER,T,BinaryOp,UnaryOp>` — backs both `Context::reduce` (UnaryOp = `details::Identity`) and `Context::transform_reduce`. Same iterator-category split as EachJob. Per-chunk partial T's live in a `std::vector<T>` sized to taskCount (hence the `static_assert` that T is default-constructible). After `runQueueJob` returns, `finalize()` combines the partials with `init` serially, applying `init` exactly once. Reduction model: each chunk seeds from its first transformed element. The `BinaryOp` contract documented to users is "associative AND commutative"; the library combines partials in unspecified order.
- `AsyncJob<F,R>` — heap-allocated via `shared_ptr` (lifetime extends past the calling stack frame). Templated on the user functor's return type `R = invoke_result_t<F>` (defaults via second template arg); holds `std::promise<R>` + the user functor; doesn't use `runOne`, exception path goes through the promise. The void / non-void split is an `if constexpr` inside `run()`. Backs the typed `Handle<R>` returned by `Context::async`.

The base [`Job`](include/multi/details/job.h) is non-polymorphic — no virtual `run`, protected non-virtual destructor. It only holds the shared state: `m_taskCount` (immutable), `m_remaining` (atomic countdown), `m_excOnce`/`m_firstException` (exception capture), plus the `runOne(F&&)` template helper that wraps user code with try/catch + release-`fetch_sub`.

`Context::runQueueJob<JobT>(JobT&)` is templated on the concrete subclass so `job.run(i)` is a direct call resolved at compile time, not a vtable hop. Three paths:
- `taskCount() == 0`: no-op.
- `taskCount() == 1`: run inline on caller (covers historical `taskCount<=1` serial fallback for chunked jobs, and any single-task dispatch).
- `taskCount() >  1`: submit a batch via `WorkerPool::submitBatch(count, gen)` (the generator-based overload — no intermediate `vector<Task>`), spin on `remaining()` while participating via `tryRunSteal`, then `rethrowIfFailed()`.

Wrapper Tasks pushed into worker deques are `[&job, i]() { job.run(i); }` (16 B SBO fit) for sync jobs; AsyncJob's wrapper is `[job]() { job->run(0); }` capturing the `shared_ptr` by value (also SBO). Sibling tasks are **not** cancelled on exception — the surviving `runOne` calls still decrement `m_remaining`.

`async` ([context.inl](include/multi/details/context.inl)) creates the `shared_ptr<AsyncJob<F,R>>` (with `R = invoke_result_t<F>`), grabs a `std::future<R>` from its promise, submits the wrapper, and returns `Handle<R>`. `Handle` is a class template — `template<class T = void> class Handle` — so CTAD makes `auto h = multi::async([]{ return 42; });` deduce `Handle<int>`; the default-init form is `multi::Handle<> h;` (void specialisation). Handle wraps a `shared_future<T>` so `wait()`/`get()` are idempotent and rethrow. **`Handle` holds no Context pointer**; `wait()` / `get()` / `wait_for` / `wait_until` all block plainly with no steal participation. Use `Context::stealWhile(handle)` (or the `multi::stealWhile(h)` free-function) to help while waiting. Handle's templated method bodies live in [details/handle.inl](include/multi/details/handle.inl); there is no `handle.cpp`. **`Handle`'s destructor auto-waits and swallows** — to observe failures, call `wait()`/`get()` explicitly. `Handle::operator=` also waits on the LHS before overwriting (preserves RAII even when reassigning); `detach()` releases without waiting.

**Inlining trade-off, documented:** templating `runQueueJob` lets the compiler see through every layer, which is great for `tiny_tasks/items`-shape workloads (~5–15% improvement vs the original mutex deque) but produces large per-instantiation wrapper functions for chunked jobs whose user lambdas have heavy captures. `chunk_factor_scan/heavy_cap` at K=16+ shows ~14% regression vs the mutex baseline; the wrapper Task `invoke()` ends up at 4–11 KB after the user's body inlines through `ChunkedRangeJob::run` → `runOne` → chunk loop. `[[gnu::noinline]]` doesn't recover it (the user lambda still inlines into `run` itself). Real CPU-bound workloads (mandelbrot, imbalanced) sit at noise. The trade is intentional — don't try to "fix" it by re-virtualising `Job::run` without re-checking the items rows.

### Type-system invariants

- `RangeJob`/`ChunkedRangeJob` `static_assert` that `IDX` is signed — unsigned subtraction in the chunking math silently underflows.
- `RangeJob`/`ChunkedRangeJob` count is computed via the `is_integral_v<IDX>` branch: integer types use `(end - begin + step - 1) / step` (O(1)); floating-point uses an additive loop so the count matches what an equivalent serial loop would produce. The dispatched values use the multiplied form `begin + i*step` regardless — for floats the last value may differ by fp rounding, but the iteration count matches.
- `each(taskCount, …)` and `range(taskCount, …)` use balanced distribution: first `extra = total % taskCount` chunks get `base+1`, the rest get `base`. Don't replace with simple ceiling-division — the case `total=15, N=14` where ceiling collapses to fewer chunks than requested is what motivated the formula.
- `ChunkedRangeJob`/`ChunkedEachJob` normalise `taskCount=0` to `1` (single chunk over the whole range); together with the `count==1` inline path in `runQueueJob`, that's the historical "serial fallback" semantics.
- `task.h` lives at [include/multi/details/task.h](include/multi/details/task.h) — internal-only. Users go through `multi::async`/`parallel`/`each`/`range`; they should never include `Task` directly.

### When extending

- New synchronous primitives belong as new `Job` subclasses in [details/job.h](include/multi/details/job.h) plus a thin 2-line dispatcher in [context.inl](include/multi/details/context.inl) (`construct Job, call runQueueJob(job)`) and a forwarder in `multi.h`. Don't add new dispatch shapes by building task vectors in `Context` — the architecture is "Context is thin, Jobs own their setup".
- New `Job` subclasses must override `run(std::size_t i) noexcept` non-virtually, and use the inherited `runOne(...)` for the try/catch + decrement pattern. Don't reinvent it. The `m_remaining` release in `runOne` is what publishes `m_firstException` to waiters via `remaining()` — preserve that pairing.
- AsyncJob is the exception that proves the rule: it doesn't use `runOne` (its exception path is the promise) and doesn't go through `runQueueJob` (lifetime extends past the caller's stack frame, so it's heap-allocated via `shared_ptr`).
- New wake sites must use `fencedNotify`.
- Don't drop the `bool isActive()` inline-fallback path on `WorkerPool::submit*` — it's the single-threaded story. Don't drop the `count==1` inline-fallback path in `runQueueJob` either — it's the chunked `taskCount<=1` serial story.
- `WorkerPool::~WorkerPool` asserts `stop()` was called explicitly; release builds clean up defensively but the assert is the contract.
- New owner-side push sites must use `tryPushLocal`; cross-thread pushes use `tryPushRemote`. Don't bypass `pushWithRetry` — its shutdown-aware spin is what prevents `stop()` deadlock when the overflow ring is saturated.
- New `T` types stored in `ChaseLevDeque` / `MpmcQueue` must be nothrow-move-assignable, nothrow-move-constructible, and default-constructible (`static_assert`s in the headers will catch this). The Chase-Lev claim-first steal relies on the per-slot sequence release ordering — don't switch back to a read-then-CAS algorithm without first switching `T` to a copy-supporting representation.
- **Don't add a Context back-pointer to `Handle`.** The Context-agnostic design is intentional and was the conclusion of a deliberate refactor: it lets Handles cross context boundaries, removes the include cycle between handle.h and context.h, and makes the steal-participate behaviour an explicit, visible call (`stealWhile`) instead of implicit. If you find yourself wanting Handle to "just know" the right context to steal from, write a `Context::stealUntil(pred)` predicate primitive instead.
- New threads created outside `WorkerPool::start` (if any) should call the existing `setCurrentThreadName(name)` helper inside their thread function with a short ASCII name — workers self-name as `multi-N` from inside `workerMain` so they show up identifiably in `top -H` / Activity Monitor / Visual Studio. The helper is platform-conditional (`SetThreadDescription` on Win10+, `pthread_setname_np` everywhere else) and best-effort; failures never propagate.
- The free-function layer in `multi.h` is a thin forwarder over `multi::context()->method(...)`. Every new public Context method needs a matching free-function wrapper there; otherwise users on the global context can't call it.
