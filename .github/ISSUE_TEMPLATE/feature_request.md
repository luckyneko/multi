---
name: Feature request
about: Suggest a feature, API addition, or behaviour change
labels: enhancement
---

## What are you trying to do?

The use case, not just the proposed API. "I have a pipeline of N stages and need to back-pressure between them" beats "add a bounded channel" — the former lets us suggest alternatives, the latter pre-decides.

## What's the workaround today (if any)?

Pseudocode of how you'd solve it with the current API. Tells us how painful the gap is.

## Proposed API (optional)

If you have a concrete shape in mind:

```cpp
// e.g.
multi::scoped(4, [&](multi::Scope& s) {
    s.async([]{ ... });
    s.each(begin, end, [](auto&){ ... });
}); // joins all scope tasks on exit
```

## Alternatives considered

Any other shapes you thought about. Helpful when several APIs could solve the same use case.

## Additional context

Links to similar APIs in other libraries (TBB task arenas, Tokio runtimes, std::execution, etc.) — they often have lessons about edge cases.
