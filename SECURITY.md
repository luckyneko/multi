# Security Policy

## Supported versions

`multi` is maintained from the `master` branch. There are no LTS or backport branches; fixes land on `master` and are tagged as the next release.

| Version    | Supported |
|------------|-----------|
| Latest tag | Yes       |
| Older tags | No        |

## Reporting a vulnerability

Please report security issues **privately** — do not open a public GitHub issue.

Email: **github@luckyneko.com**

When you report, please include:

- A description of the issue (UB, data race, lifetime bug, denial of service, etc.).
- The build configuration that triggers it (compiler, version, build type, sanitizer flags if any).
- A minimal reproducer if possible. For concurrency bugs, the full ThreadSanitizer or AddressSanitizer stack trace is usually enough.
- Whether you'd like to be credited in the fix's commit/release notes.

## Response

This is a single-maintainer project, so response time is best-effort:

- Initial acknowledgement: within 7 days.
- Triage and assessment: within 30 days.
- Fix timeline: depends on severity and complexity; you'll get a status update if it stretches beyond that.

If you don't hear back, please ping the same email — it's not deliberate.

## Scope

In scope:

- Undefined behaviour, data races, lifetime bugs, deadlocks, lost wakeups, or other concurrency hazards in the library code.
- Issues affecting the build system or installed CMake package that could compromise downstream consumers (path traversal in install rules, etc.).

Out of scope:

- Performance regressions (file a normal issue or PR).
- Misuse of the API in caller code (we'll happily document footguns, but they're not vulnerabilities).
- Issues in vendored dependencies (Catch2 is fetched via `addcatch2.cmake`; report to upstream).

## Disclosure

Once a fix is in place we'll coordinate a disclosure date with you. The default is "disclose at release" — the patch lands on `master` in a tagged release, with a brief acknowledgement in the release notes.
