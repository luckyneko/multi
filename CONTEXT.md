# multi

`multi` is a C++ task execution library. Its language should distinguish ordinary task dispatch from dependency-aware execution.

## Language

**Execution Context**:
The object that owns a task execution environment and exposes dispatch and wait operations.
_Avoid_: Scheduler, pool

**Real-Time Threading**:
Low-overhead task execution where worker lifetime, dispatch cost, and caller participation are explicit parts of the API contract.
_Avoid_: General-purpose async runtime, node processing

**Task Handle**:
A value that observes one asynchronous task's completion and result.
_Avoid_: Future, job

**Recipe**:
A single-use DAG of executable steps connected by ordering edges, used as another way to thread work through `multi`.
_Avoid_: Flow, schedule, node graph, TaskFlow

**Step**:
A light public handle to one task inside a Recipe. It is not the callable itself; it is the value callers use to attach ordering edges.
_Avoid_: Task, job, node

**Invalid Step**:
A Step that does not refer to a task inside a Recipe. Failed step creation returns an invalid Step, and callers can check it with `valid()` or `operator bool`.
_Avoid_: Null task, exception

**Before Edge**:
An ordering relationship between two Steps, expressed as `a.before(b)`. It controls when work may run, not what data moves between tasks.
_Avoid_: Precede, dataflow edge, port connection

**Failed Step**:
A Step whose callable threw a user exception during a run. Its dependent successors are skipped, while already-running or independent steps may still finish.
_Avoid_: Cancelled task, setup failure

**Recipe Progress**:
A future observable for a launched Recipe run, not state owned by Recipe itself.
_Avoid_: Synchronization primitive, recipe state, per-node progress

**Single-Use Recipe**:
A Recipe whose task graph is consumed by `async(std::move(recipe))`. Ownership moves into the launched job, so callers should only destroy or assign the moved-from Recipe and the returned handle owns the run lifetime.
_Avoid_: Reusable recipe, borrowed recipe run

**Recipe Run State**:
The lifecycle of a launched recipe job. This is not currently exposed on Recipe itself because Recipe launches transfer ownership into the job.
_Avoid_: Recipe mutation state, task state

**Recipe Mutation**:
Changing a Recipe's structure before launch. The initial model allows adding steps and adding before edges; removing individual steps is outside the first version.
_Avoid_: Live graph editing, task cancellation

**Recipe Result**:
The explicit outcome of a Recipe setup or mutation operation, returned so callers can log or display failures.
_Avoid_: Exception, assertion-only validation

**Node Graph**:
A caller-owned graph of domain nodes and dataflow edges that can be lowered into a Recipe for execution.
_Avoid_: Task graph, recipe
