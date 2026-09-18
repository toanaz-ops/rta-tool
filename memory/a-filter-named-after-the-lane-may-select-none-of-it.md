# A filter named after the lane may select none of the lane's tests

2026-09-18, lane L-API closeout, writing rule 12's "what a human can run".

The obvious command for a lane called L-API is `ctest -R "api"`. Written into a
handoff it would have looked helpful for months. Counted, it selects **exactly
one test, and it is a guard**: `no_server_library_outside_api`, whose *name*
happens to end in the substring. Not one of the lane's 76 test cases runs.

Three facts compose into that, and each is individually unremarkable:

- `ctest -R` is a **case-sensitive** regex over **ctest test names**.
- `catch_discover_tests` is called with no `TEST_PREFIX`, so a ctest name is the
  raw `TEST_CASE` string — and **0 of the repository's 837 `TEST_CASE` names
  contain a lowercase `api`**. The lane spelled it `API`, `Api`, or not at all.
- The lane's tags are no rescue either: 49 of its 76 cases carry `[api]`, and
  the **26 socket-level cases — the ones the whole `API-R15` argument exists to
  make runnable on CI — carry no tag whatsoever.** Catch2's `-f <specfile>` is
  not a way round it: 9 of those 26 names contain a comma, which is Catch2's
  spec separator.

So the tests most expensive to arrange, and most valuable to re-run, were the
ones no filter could reach.

The same shape as `a-fixture-can-be-too-well-behaved-to-fail.md`: a command
that returns cleanly is not a command that did the work. There, a test passed
because it could not fail; here, a filter passes because it selects nothing. A
zero-test `ctest` run still prints `100% tests passed`.

## The rule

Before a test-selection command goes into a handoff, a report or a PR body,
**count what it selects and paste the count**. `ctest -R … -N` prints
`Total Tests: N`; a Catch2 tag run prints the case count. If the count is not
the number you meant, the command is wrong — fix the command, or fix the tags
it needs.

A count is also the only way to state the filter honestly when it cannot be
made to work: the handoff that came out of this carries a 26-branch anchored
alternation over the case names, verified to match exactly those 26 of all 837,
plus the one-line fix that would retire it — give the 26 cases the lane's tag.
