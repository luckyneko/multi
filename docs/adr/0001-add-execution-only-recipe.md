# Add an execution-only recipe

`multi` will add a generic Recipe API for DAG-shaped task execution: callers add void steps, connect them with execution-only `before` edges, and run the recipe through a `Context`. A recipe is reusable across sequential runs but not concurrently runnable, returns explicit setup/link errors instead of throwing `multi` exceptions, returns an invalid Step when creation cannot happen, propagates user task exceptions from `run`, and leaves data movement to the caller's domain model rather than becoming a dataflow runtime.
