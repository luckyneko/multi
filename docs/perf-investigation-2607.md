# Recipe Performance Investigation, July 2026

This note summarizes the Recipe performance pass done while shaping Recipe as
Multi's execution-only DAG API for dynamic node graphs.

## Context

Recipe is a single-use DAG builder and executor. It owns void tasks, accepts
explicit dependency edges, then `multi::async(std::move(recipe))` executes the
graph. The primary motivating caller is Lain's flow graph, where a node graph is
lowered into one task per node and one dependency edge per graph edge.

The investigation split performance into two parts:

- construction cost: adding steps and dependency edges
- runtime cost: scheduling and completing Recipe steps

The benchmark report now uses `async(baseline)` for equivalent explicit async
orchestration, and includes build-only Recipe rows so graph construction does
not distort runtime speedups.

## Changes Kept

### Inline successor storage

Each Recipe step now stores up to four successors inline before falling back to
vector overflow. This removes the common per-step successor allocation for
linear, fan-in, and small fixed-outdegree node graphs.

Measured against the trimmed construction benchmark:

```text
dense width 4 1000:      70.9 us -> 20.1 us
fan-in 1000:             47.0 us -> 35.4 us
fan-out 1000:            33.5 us -> 32.5 us
linear 1000:             23.8 us -> 10.2 us
reverse linear 1000:     23.7 us -> 10.4 us
independent 1000:         4.9 us ->  7.0 us
```

The independent/no-edge case regresses because every `RecipeStep` is larger, but
edge-bearing DAGs benefit strongly. Inline capacity 4 was the best tested
default for node-graph-style workloads. Inline capacity 1 improved independent
builds but made dense width 4 about 3.7x slower.

### Lain-shaped and wide-root benchmark coverage

Two benchmark shapes were added because the original recipe set did not clearly
separate startup roots from convergence patterns.

`wide-root`:

```text
root[0..N] -> join
```

50-sample result:

```text
run wide-root 32 roots:
  async(baseline):  5.962 ms
  multi(recipe):    5.325 ms   1.12x

build wide-root 1000 roots:
  recipe(build):   10.7 us
```

This showed that many roots ready at startup are not a Recipe bottleneck.

`lain reduce`:

```text
64 source/boundary nodes
  -> 32 two-input compute nodes
  -> 16 two-input compute nodes
  -> ...
  -> 1 output
```

50-sample result:

```text
run lain reduce 64 leaves:
  async(baseline): 23.595 ms
  multi(recipe):   12.303 ms   1.92x

build lain reduce 1024 leaves:
  recipe(build):   21.5 us     // 2047 steps
```

This is the best current proxy for Lain's developing node graph. It suggests
Recipe handles layered two-input convergence well. The weaker synthetic fan-in
benchmark is a single convergence point, not representative of the Lain shape.

### Existing construction optimizations remain important

Earlier Recipe work already kept:

- reusable reachability scratch buffers
- generation-marked reachability visits
- a topological-order shortcut that skips cycle DFS while accepted edges follow
  `before.index < after.index`
- a Recipe-level side table for duplicate detection after a producer reaches 128
  successors
- a split between mutable `Recipe` builder state and baked `RecipeGraph` runtime
  state

The topological-order shortcut is especially important for generated DAGs that
create steps in topological order but emit edges in arbitrary order:

```text
reverse chain 1000:      1371.74 us ->    256.82 us
reverse chain 5000:     31420.29 us ->    949.15 us
dense width 4 1000:      5395.06 us ->    232.75 us
dense width 4 5000:    155472.45 us ->   1221.53 us
```

## Rejected Probes

### Bulk edge API

A temporary `orderBulk(const StepLink*, size_t)` pre-counted successors, reserved
storage, then called normal `order()` per edge. It was slower:

```text
linear 1000:             immediate 10.8 us, bulk 12.5 us
dense width 4 1000:      immediate 22.4 us, bulk 31.7 us
fan-out 1000:            immediate 32.0 us, bulk 34.3 us
fan-in 1000:             immediate 35.5 us, bulk 41.8 us
```

Bulk only becomes interesting if it changes semantics to cheap edge append plus
one final validation/bake pass. The tested wrapper around `order()` is a loss.

### Deferred validation

A temporary `orderDeferred()` plus `validate()` path was slower with validation
included:

```text
linear 1000:             immediate 10.8 us, deferred 14.9 us
dense width 4 1000:      immediate 21.5 us, deferred 24.5 us
fan-out 1000:            immediate 31.6 us, deferred 31.6 us
fan-in 1000:             immediate 34.7 us, deferred 36.1 us
```

The topological-order shortcut already makes immediate validation cheap for the
generated-forward DAGs tested here.

### Root list and pending-state changes

Baking root indices into `RecipeGraph` and removing the successor scheduled-CAS
both landed in benchmark noise. Root scanning is not worth optimizing without a
larger graph shape showing otherwise, and the CAS removal weakens the simple
`Pending -> Scheduled -> Finished` state model.

### Runtime state layout

Packing `pending` and `state` into one per-step struct, including a compact
`uint32_t` version, did not produce stable runtime wins:

```text
compact state vs split vectors:
run fan-in:       6.887 ms -> 6.778 ms   noise
run fan-out:     12.616 ms -> 12.304 ms  noise
run independent:  5.576 ms -> 5.497 ms   noise
run lain reduce: 12.422 ms -> 12.039 ms  noise
```

The compact form may be a memory-footprint cleanup someday, but it is not a
performance change from current measurements.

### Submit-path refcount reduction

Replacing per-scheduled-step `shared_ptr` captures with raw `RecipeJob*` captures
was dangerous. The first version released the job self-retain as soon as the
promise was set, and the benchmark crashed because the handle could observe
completion while a worker task was still returning from `runStep()`.

A safe version held one job-level self-retain until both completion and the last
scheduled task wrapper exit. It fixed the crash but remained benchmark noise:

```text
run fan-in:       6.887 ms -> 6.929 ms   noise
run fan-out:     12.616 ms -> 12.721 ms  noise
run independent:  5.576 ms -> 5.387 ms   noise
run lain reduce: 12.422 ms -> 11.837 ms  noise
```

The current per-task `shared_ptr` capture is simpler and is not a proven
bottleneck.

### Exact outdegree hints

A temporary public hook was tested:

```cpp
RecipeResult Recipe::reserveSuccessors(Step step, std::size_t count) noexcept;
```

80-sample result:

```text
dense width 4:      normal 21.2 us, reserved 22.8 us
dense width 8:      normal 88.6 us, reserved 58.9 us
fan-out 1000:       normal 31.7 us, reserved 30.7 us
fan-out dup 1000:   normal 34.5 us, reserved 34.1 us
fan-in 1000:        normal 36.2 us, reserved 36.1 us
```

Exact hints help when many producers exceed the inline-4 capacity. They do not
matter for common outdegree <= 4, and barely help one huge fan-out. The API
surface is not worth adding for current Lain-shaped targets.

## Task Storage Finding

`details::Task` already has a 24-byte SBO. Empty lambdas and normal small
captures do not allocate per step.

Allocation probe for 1000 reserved tasks or Recipe steps:

```text
sizeof(Task)=32 sizeof(RecipeSuccessors)=64 sizeof(RecipeStep)=104

Task empty lambda:       1 alloc,    32000 bytes
Task small capture:      1 alloc,    32000 bytes
Task big capture:     1001 allocs,  96000 bytes
Task throwing move:   1001 allocs,  33000 bytes

Recipe empty lambda:     3 allocs, 116000 bytes
Recipe small capture:    3 allocs, 116000 bytes
Recipe big capture:   1003 allocs, 180000 bytes
Recipe throwing move: 1003 allocs, 117000 bytes
```

The independent/no-edge Recipe build cost is mostly `RecipeStep` footprint and
vector writes, not hidden task heap churn.

## Current Conclusion

The performance pass is exhausted for current information. The kept changes
improve the construction path that matters for dynamic DAGs and add benchmark
coverage for Lain-like graph shapes. Further low-level runtime changes did not
produce stable improvements.

The next meaningful performance input should be a captured real Lain graph once
that project has a representative node graph. Until then, avoid adding new
Recipe API surface for speculative performance.
