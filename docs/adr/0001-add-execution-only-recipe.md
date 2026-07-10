# Add an execution-only recipe

`multi` will add a generic single-use Recipe API for DAG-shaped task execution: callers add void steps, connect them with execution-only `before` edges, and launch the recipe through `Context::async(std::move(recipe))`. Launch moves the recipe into an owned job, so move-only task captures are supported and the returned `RecipeHandle` observes completion and progress. Recipe setup/link errors are explicit results instead of `multi` exceptions, and data movement stays in the caller's domain model rather than becoming a dataflow runtime.
