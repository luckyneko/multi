## Summary

<!-- One or two sentences on what this PR changes and why. -->

## Test plan

<!-- What you ran locally before pushing. Tick whichever apply. -->

- [ ] `ctest --test-dir build --output-on-failure` passes
- [ ] TSan run passes for concurrency-touching changes (`-fsanitize=thread -g -O1`)
- [ ] New behaviour has a Catch2 test
- [ ] Public-API changes update [README.md](../README.md) examples
- [ ] Dispatch/scheduling changes update [CLAUDE.md](../CLAUDE.md) architecture notes

## Notes for the reviewer

<!-- Anything that's worth flagging up front: tricky invariants, trade-offs taken, alternatives you tried, bench numbers if performance-relevant. Delete if there's nothing. -->
