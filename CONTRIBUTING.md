# Contributing to multi

Thanks for your interest. This document covers the practical bits: build, test, style, and what to put in a PR.

## Build, test, bench

```sh
cmake -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

CMake options (default ON when standalone, OFF when consumed via `add_subdirectory`):

- `MULTI_BUILD_TESTING` — builds `test-multi`
- `MULTI_BUILD_BENCHMARK` — builds `bench-multi` (off by default; opt in with `-DMULTI_BUILD_BENCHMARK=ON`)
- `MULTI_INSTALL` — generates install/export rules

Single Catch2 test name:

```sh
./build/test-multi "Context: reduce sums an integer vector across thread counts"
./build/test-multi -# "[some-tag]"
```

Benchmarks:

```sh
cmake -B build -DMULTI_BUILD_BENCHMARK=ON
cmake --build build
./build/bench-multi "[fast]"   # ~10s subset
./build/bench-multi "[slow]"   # mandelbrot, imbalanced
```

See [README.md](README.md) for headline benchmark numbers.

## ThreadSanitizer

`multi` is a concurrency library — any change touching the deques, worker pool, wakeup, or shutdown paths should be validated under TSan locally before opening a PR. CI runs TSan on Linux/Clang automatically.

```sh
cmake -B build-tsan \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=thread -g -O1" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread"
cmake --build build-tsan -j
TSAN_OPTIONS="halt_on_error=1:second_deadlock_stack=1" \
  ctest --test-dir build-tsan --output-on-failure
```

`-O1` is the recommended optimisation level — fast enough to actually exercise concurrency, slow enough to keep useful stack frames.

## Code style

- C++17, Allman braces, **tabs** for indentation. The repo ships a [.clang-format](.clang-format); VS Code is configured to format on save.
- Strict warnings: `-Werror -Wall -Wextra` (Clang/GCC), `/WX /W4` (MSVC). PRs that warn don't merge.
- `#pragma once` for header guards (not `_MULTI_*_H_` — those are reserved identifiers per `[lex.name]`).
- Public headers should not transitively pull in heavy STL headers if it can be avoided. The non-template implementation lives in `src/`.
- Comments explain *why*, not *what*. The architecture is documented in [CLAUDE.md](CLAUDE.md) — keep it in sync when changing dispatch or scheduling semantics.

## Tests

Add Catch2 tests for any new behaviour. The tests in `test/` are organised one TEST_CASE per behaviour and use `GENERATE` to parameterise across thread counts (including `0` for the single-threaded inline path) — please follow that pattern.

If your change touches concurrency primitives (`ChaseLevDeque`, `MpmcQueue`, `WorkStealDeque`, `WorkerPool`, `Handle`), please also add or extend a stress test in the matching `test/<primitive>.cpp` file.

## Pull requests

- Target the `develop` branch. `master` tracks released revisions.
- Keep changes focused — one logical change per PR. Sweeping refactors are easier to review when split across a small chain of PRs.
- CI must be green before merge: Linux (GCC 13/14, Clang 17/18), macOS 15, Windows (MSVC 2022/2025), Linux TSan, and Android/iOS cross-compile.
- For behaviour changes: update [README.md](README.md) examples and the architecture notes in [CLAUDE.md](CLAUDE.md) if the dispatch or scheduling story changes.
- For new public API: extend the corresponding free-function wrapper in [multi.h](include/multi/multi.h).

## Reporting bugs

Please file an issue using the bug-report template under "Issues". Concurrency bugs in particular benefit from:

- Build configuration (compiler version, build type, sanitizer flags)
- Worker count
- A minimal reproducer if possible — but if it's a flaky TSan report from your real workload, include the TSan stack trace anyway; we'd rather see it than not.

## Security

For security-sensitive issues, see [SECURITY.md](SECURITY.md). Please don't open public issues for those.

## License

By contributing, you agree that your contributions are licensed under the MIT License — see [LICENSE.md](LICENSE.md).
