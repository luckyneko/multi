---
name: Bug report
about: Report a defect in multi
labels: bug
---

## What happened?

A clear description of the bug — what you observed, and what you expected instead.

## Reproducer

If possible, a minimal piece of code that triggers it:

```cpp
#include <multi/multi.h>
int main() {
    multi::start(4);
    // ...
    multi::stop();
}
```

If it only reproduces under TSan/ASan, paste the relevant part of the sanitizer report below — that's more useful than a workload description.

## Environment

- multi version / commit: <!-- e.g. v0.1.0 or commit hash -->
- Compiler: <!-- e.g. clang-18, gcc-14, MSVC 2022 -->
- Build type: <!-- Debug / Release / RelWithDebInfo -->
- Build flags: <!-- e.g. -fsanitize=thread -->
- OS: <!-- e.g. Ubuntu 24.04, macOS 15, Windows 11 -->
- Worker count: <!-- multi::start(N) -->
- Workload shape: <!-- async / parallel / each / range / reduce / mixed -->

## Additional context

Anything else — frequency (always vs flaky), whether it reproduces with `threadCount=0`/`1`/many, prior version that worked, etc.
