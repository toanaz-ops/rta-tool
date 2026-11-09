---
name: a-test-name-starting-with-a-dash-is-a-filter
description: catch_discover_tests passes each TEST_CASE name to the exe as a filter; a name starting with "-" is read as an exclusion and the case fails on CI with "Unrecognised token: -"
metadata:
  type: project
---

PR #105 (2026-11-09) named a case `"- PANE destroys only the last pane..."`
after the button it tests. Locally, running the exe by TAG, it passed. On CI,
ctest runs every discovered case as `exe "<name>"`, and Catch2 parses a
leading `-` as an exclusion token: `Error(s) in input: Unrecognised token: -`,
the case reported Failed in 0.02 s without running. A leading `+ PANE` passed,
so it is the dash specifically.

**Why:** a tag run never passes the name, so the local narrow run cannot see it;
only the per-case ctest invocation (CI) does.

**How to apply:** never start a TEST_CASE name with `-`, `~`, `[` or `"`; put a
word first ("removing the last pane with - PANE ..."). When a CI-only failure
takes ~0 s, read the case's own output before suspecting the code. Related:
[[a-filter-named-after-the-lane-may-select-none-of-it]],
[[a-hidden-catch2-tag-is-not-a-gate]].
