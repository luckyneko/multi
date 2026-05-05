# multi review report

A code review of the `multi` C++17 threading library covering cleanliness, safety, and performance, with bench-multi numbers from a fast-tagged run.

## Bench numbers (MSVC Release, 20 samples)

| Workload | items (µs) | chunks (µs) | serial (µs) | items vs chunks | chunks speedup |
|---|---:|---:|---:|---:|---:|
| empty 1k | 191 | 93 | — | 2.0× slower | — |
| empty 5k | 460 | 70 | — | 6.6× slower | — |
| tiny 1k | 202 | 104 | 389 | 1.9× slower | 3.7× |
| tiny 5k | 568 | 137 | 1958 | 4.1× slower | 14.3× |
| tiny 50k | 5523 | 760 | 19469 | 7.3× slower | 25.6× |
| nested | — | 443 | 10291 | — | 23.2× |
| async_latency | 1.97 µs/round | | | | |

The big asymmetry is **items vs chunks** — and it grows with task count. The dominant cost in items-mode is per-task `std::function` construction + `std::deque<Task>` enqueue + per-task wrapper + steal-loop wakeup. The chunked path collapses this to `(threadCount+1)*8` tasks regardless of n.

---

## High-priority findings

### 1. `each(taskCount, …)` and `range(taskCount, …)` copy `func` per chunk

[include/multi/details/context.inl:105](include/multi/details/context.inl#L105) and [:182](include/multi/details/context.inl#L182):

```cpp
auto task = [innerBegin, innerEnd, func]()      // each
auto task = [innerBegin, innerEnd, step, func]()// range
```

`func` is captured **by value**. Since `FUNC&& func` binds to the user's lvalue lambda, this copies the user functor `taskCount` times (and again into each `std::function`). The non-chunked overloads at [:62](include/multi/details/context.inl#L62) and [:139](include/multi/details/context.inl#L139) capture `&func` and document why: `runQueueJob` blocks until every task finishes, so the parameter outlives the tasks. The chunked paths should follow the same pattern. Fix:

```cpp
auto task = [innerBegin, innerEnd, &func]()
auto task = [innerBegin, innerEnd, step, &func]()
```

This eliminates one copy per chunk and keeps the lambda small enough for `std::function` SBO on every STL. The implicit contract — that `func` is callable concurrently — is the same one already documented for the non-chunked path.

### 2. `tryStealAny` always scans from index 0

[src/workerpool.cpp:127-138](src/workerpool.cpp#L127-L138):

```cpp
bool WorkerPool::tryStealAny(Task* task)
{
    for (size_t i = 0; i < m_workers.size(); ++i)
    {
        if (m_workers[i]->deque.steal(task))
            return true;
    }
    return false;
}
```

Every `Handle::wait` and every `runQueueJob` caller participating in stealing scans from worker 0. With many participants this concentrates contention on worker 0's deque mutex. The commented-out `m_nextVictim`-based rotation is exactly the right idea — the scan in [`tryGetTask`](src/workerpool.cpp#L171) already does it correctly per worker. Wire `m_nextVictim` in:

```cpp
size_t workerCount = m_workers.size();
size_t base = m_nextVictim.fetch_add(1, std::memory_order_relaxed) % workerCount;
for (size_t i = 0; i < workerCount; ++i)
{
    if (m_workers[(base + i) % workerCount]->deque.steal(task))
        return true;
}
return false;
```

This matters most under the `async_fanout` profile where many threads concurrently `Handle::wait` on a shared pool.

### 3. `m_nextVictim` is dead code

[include/multi/details/workerpool.h:77](include/multi/details/workerpool.h#L77) declares it; [src/workerpool.cpp:18,53](src/workerpool.cpp#L18) initialize/reset it; nothing else touches it. It also burns a cache line. Either wire it up (see #2) or remove it.

---

## Medium-priority findings

### 4. `submit` racing against `stop` is silently broken

`stop()` at [src/workerpool.cpp:71](src/workerpool.cpp#L71) sets `m_active=false`, notifies, then joins. If another thread is inside `submit()` between its `isActive()` check at [:87](src/workerpool.cpp#L87) and the `deque.push` at [:95](src/workerpool.cpp#L95), it will push into a worker that's about to exit. The task is then left in a deque that's about to be destroyed in the workers/threads cleanup. No assert, no exception — just a dropped task. This isn't a hypothetical: the original `WorkerPool stop drains remaining tasks` failure on Windows was a similar shape.

Either:
- Document that `stop` must not be called while another thread is submitting (current implicit contract); or
- Add a writer/reader pattern: take a shared lock around submit/submitBatch, take an exclusive lock in stop. That's heavy.

A lightweight fix: in `stop()`, after setting `m_active=false`, drain every worker's deque on the calling thread before joining — same pattern as the do-while drain in `workerMain`. That at least guarantees no pushed task is silently lost; concurrent submitters might still race past the active check, but the window narrows to the actual push call.

### 5. Inconsistent `std::ref` use

[include/multi/details/context.inl:75,108](include/multi/details/context.inl#L75) wrap with `std::ref(*it)`:

```cpp
func(std::ref(*it));
```

`*it` is already an lvalue; `std::ref` adds nothing here (and the resulting `reference_wrapper<T>` only works because of its implicit `operator T&`). It's inconsistent with the non-chunked path at [:63](include/multi/details/context.inl#L63) which writes `func(*item)`. Drop the `std::ref` for clarity.

### 6. `each(begin, end, …)` produces one task per element with no warning

The non-chunked overload at [include/multi/details/context.inl:52](include/multi/details/context.inl#L52) creates one `std::function` per item. With 1M items that's ~64 MB of `std::function` storage plus the deque pages plus per-task dispatch cost. The bench numbers confirm items-mode at 50k tasks is 7× slower than chunks. The README example uses `each(begin, end, …)` as the primary form, which is the wrong default for any non-trivial workload. Either:
- Make the no-`taskCount` form auto-chunk (e.g., default to `threadCount*K` chunks like the bench helper), or
- Document the trade-off and recommend `each(taskCount, …)` for >threadCount items.

### 7. ~~No step-less `range(taskCount, begin, end, …)` overload~~ — withdrawn (would clash)

Originally I suggested adding `range(N, b, e, fn)` for symmetry with `each(taskCount, …)`. **Don't.** The proposed overload would have the same arity (4) as the existing `range(b, e, s, fn)`, and overload resolution silently prefers the existing one whenever the user passes integer literals: `range(4, 0, 100, fn)` would resolve to `begin=4, end=0, step=100`, which fails the `end <= begin` guard at [include/multi/details/context.inl:132](include/multi/details/context.inl#L132) and runs nothing without any diagnostic.

If the symmetry is genuinely wanted, the safe options are:
- A tagged first arg (`range(multi::tasks(4), 0, 100, fn)`) — unambiguous, verbose.
- A distinct name (`range_n(4, 0, 100, fn)`) — clear, but loses the `range`/`each` parallel.
- Leave it alone. Users spell `range(N, 0, n, 1, fn)` explicitly — current behavior.

`each(taskCount, begin, end, fn)` is fine as-is: its 4-arg form has iterator types that can't collide with `size_t`, so no ambiguity.

### 8. `submitBatch` partial failure leaves `Job::remaining` stuck

[src/workerpool.cpp:99-125](src/workerpool.cpp#L99-L125): if `deque.push` throws partway through (e.g., OOM), some tasks are queued, others aren't, but `Job::remaining` was sized to `tasks.size()`. The caller in `runQueueJob` then spins forever because remaining never reaches 0. Extreme edge case (OOM on push), but the failure mode is "hang" rather than "throw". Two options: catch in submitBatch and decrement `remaining` for unsubmitted tasks (requires submitBatch to know about Job, which breaks its abstraction), or — simpler — have `runQueueJob` allow tasks to be batched-or-run-inline so we never partially submit. The current OOM behavior is bad enough that documenting "do not catch — process is dying" is reasonable, since `std::bad_alloc` here is essentially terminal.

---

## Low-priority / cleanups

### 9. Header guards use reserved-identifier style

`#ifndef _MULTI_*_H_` — leading underscore + uppercase is reserved to the implementation by the standard. Real-world rare to hit, but technically UB. Either drop the leading underscore or switch to `#pragma once` (already supported by every compiler the project targets).

### 10. `Handle::reset` is public

[include/multi/handle.h:40](include/multi/handle.h#L40) — `reset()` clears the future and context with no wait. `detach()` already exists with the same semantics and a better name. Either remove `reset` or document the difference.

### 11. `task = nullptr` instead of `task = {}`

[src/workerpool.cpp:165](src/workerpool.cpp#L165) — `std::function::operator=(nullptr)` works but `task = {}` is the idiomatic reset. Tiny stylistic point.

### 12. Public `multi::context() = X` mutator is racy

[src/multi.cpp:14-18](src/multi.cpp#L14-L18) returns `Context*&`. Tests rely on this for swapping pools. The public function lets users set the global context with no synchronization. Document as "set once before any concurrent use" — currently no doc says this.

### 13. `WorkStealDeque::sizeHint`/`empty` lock-then-peek

[src/workstealdeque.cpp:38-48](src/workstealdeque.cpp#L38-L48): both take the mutex. They're advertised as racy/heuristic but actually synchronize. They're only used by tests. Either drop them, or actually make them lock-free (read `m_deque.size()` without the lock — UB technically with `std::deque`, but practical). Marginal.

---

## Performance opportunities (deeper / opinion)

### 14. `Task = std::function<void()>` is heavier than necessary

`std::function` requires `CopyConstructible`. The deque only ever moves Tasks. A move-only callable with a 24-byte SBO would let lambdas with iterator+iterator+pointer state stay inline on every STL (today libc++ may heap-allocate where MSVC and libstdc++ would inline). C++23's `std::move_only_function` is the canonical answer; not available pre-C++23, but a small in-house move-only function (~80 LOC) is straightforward. The marginal cost on items-mode is real — see bench numbers. Probably the single largest perf lever short of changing the deque.

### 15. `std::deque<Task>` under a mutex is the perf floor

Every `submit`, every local pop, and every steal takes the deque mutex. For pipelines where many threads steal at once, this serializes everything to (workers × 1) lock-acquire/sec. A Chase–Lev work-stealing deque is the canonical replacement: lock-free for owner pop and steal, with single-CAS contention only on shrinking. Substantial implementation effort but the right next step if perf matters.

### 16. `m_nextWorker.fetch_add` is a hot atomic on submit

[src/workerpool.cpp:93,113](src/workerpool.cpp#L93): every `submit` and every `submitBatch` hits this. For high-throughput producers it's a cache-line-bounce. A TLS-keyed submitter index (each calling thread keeps its own counter, written non-atomically) would eliminate the bounce while still spreading work. Not worth doing until the bigger items above land.

### 17. Caller `tryRunSteal` busy-loop with `yield`

[src/context.cpp:104-108](src/context.cpp#L104-L108) and [src/handle.cpp:38-43](src/handle.cpp#L38-L43): both spin on `tryRunSteal`/`yield`. For long-running tasks where the caller has nothing to steal, this burns a core. A bounded spin-then-block-on-cv would cooperate better with the OS scheduler. Trade-off: signaling cost on every task completion. For compute-bound workloads where caller participation is the design goal, the current behavior is correct. For mixed I/O + compute, it's wasteful.

### 18. Chunk count heuristic is hardcoded in the bench

[bench/workloads.cpp:66-72](bench/workloads.cpp#L66): `(threadCount+1)*8`. This isn't library code, but if you ever expose a "chunk-by-default" variant of `each`/`range` in the lib, the heuristic should live in the library and be testable. The constant 8 is an oversubscription factor for steal balance — fine, just worth naming.

---

## Safety summary

The exception story is well thought through:
- `parallel/each/range` capture only the first exception via `std::call_once` and rethrow after drain. Siblings are not cancelled — documented at [README.md:174](README.md#L174).
- `async`'s `Handle::wait` is idempotent on a `shared_future` and rethrows.
- `Handle`'s destructor and move-assign swallow exceptions to preserve RAII (with explicit `wait()` for observability).
- Workers swallow exceptions from raw `submit`d tasks at [src/workerpool.cpp:158-164](src/workerpool.cpp#L158-L164) so a throw can't kill the worker thread.
- `tryRunSteal` swallows for the same reason at [src/context.cpp:73-79](src/context.cpp#L73-L79).

The wakeup story is correct — `fencedNotify`'s lock/unlock-before-notify closes the predicate-vs-wait race, and the recent do-while fix in `workerMain` closes the start/stop race. Concurrent **submit vs stop** (#4 above) is the remaining gap.

---

## Suggested action order

1. Fix capture-by-value in chunked overloads (#1) — one-line edit, tested by existing tests, real perf win.
2. Remove or wire up `m_nextVictim` (#2 + #3) — small change, removes contention in fanout.
3. Document or fix submit-vs-stop race (#4) — safety.
4. Drop `std::ref` (#5), reset idiom (#11), `Handle::reset` cleanup (#10) — janitorial.
5. Consider move-only `Task` (#14) before going after the deque (#15).
