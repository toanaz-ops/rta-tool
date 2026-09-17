---
name: a-hidden-catch2-tag-is-not-a-gate
description: Catch2's `[.]` tag hides a test case from the DEFAULT run only, not from a filtered one. A destructive case tagged `[.][api]` was reached by `exe "[api]"`, rewrote the golden regression lock mid-suite, and the next identical run passed. A gate on a destructive action must be something a tag expression cannot supply, and it must be asserted by a test that runs under the filter that used to trip it.
metadata:
  type: project
---

**What happened (2026-09-17, lane L-API station 4 wave 1, PR #16).** Task D
needed a way to regenerate `app/tests/golden/api-v1-snapshot.json`, the byte-
compare regression lock. A Python script cannot call the C++ serialiser, so the
regenerator was written as a `TEST_CASE` tagged `[.][api][golden-write]`, and
the commit, the test's own comment, `docs/HANDOFF.md` and the PR body all
claimed the same thing:

> It is tagged `[.]` so Catch2 hides it, ctest never discovers it and **no
> wildcard run can reach it**.

Half of that is true. `[.]` keeps a case out of the **default** run and out of
`--list-tests`, so `catch_discover_tests` never registers it and no `ctest`
invocation reaches it. **It does not keep the case out of a run that names its
tags.** The verifier measured it on the committed binary:

| invocation, golden first truncated to an 8-byte sentinel | result |
|---|---|
| `rtatool_analysis_tests.exe "[api]"` — run 1 | 49 cases, 48 passed, **1 failed** — and the golden is now **198045 bytes** |
| `rtatool_analysis_tests.exe "[api]"` — run 2, identical command | **All tests passed (2320 assertions)** |

`[api]` was the tag every test in the lane shares, so the lane's own natural
filter was the one that reached the destructive case.

## Why this is worse than having no lock

A real format regression reports itself **once** and then the regenerator
repairs the evidence. The second run is green, and green-on-rerun is exactly
what a person reads as "transient, now fine". The lock silently unlocks itself
and then vouches for the thing it was supposed to catch. A lock that can do
that is a liability, because it converts a caught defect into a confirmed
pass.

## Why a tag cannot be the fix

The instinct is to pick a safer tag — drop `[api]`, add more `[.]`, invent
`[!mayfail]`. All of that is still the tag matcher, and **the tag matcher is
the thing being exploited**. Any gate expressible as a tag is a gate a tag
expression can open. Dropping `[api]` was worth doing (a case that rewrites a
lock has no business answering to the tag its whole lane shares) but it is
hardening, not a gate.

**The gate has to be something no filter expression can supply.** An
environment variable, or a compile definition. Here:
`RTA_API_GOLDEN_WRITE=1`, checked at the top of the case, `SKIP`-ping with a
message when unset. Proven both directions, sentinel re-truncated before each:

| invocation | golden after |
|---|---|
| `exe "[api]"` | **8 bytes** — 3 failures reported, not repaired |
| `exe "[*]"` | **8 bytes** |
| `exe "[golden-write]"` | **8 bytes**, 1 skipped |
| `exe "regenerate the API golden"` | **8 bytes**, 1 skipped |
| `RTA_API_GOLDEN_WRITE=1 exe "regenerate the API golden"` | **198045 bytes**, and `git diff` on it **empty** |

Note the last row twice over: naming the case exactly is *still not enough*,
and the regenerated file was byte-identical to the committed one, which
re-confirms the fixture is deterministic.

## The half that stops this coming back

**A test must assert the gate, and it must run under the filter that used to
trip the regenerator.** `D9 a filtered run cannot rewrite the golden` carries
the tag `[api]` on purpose and asserts the gate is shut while it runs. So the
next person who adds a tag to the destructive case, or who reintroduces a
tag-only gate, learns it from ctest rather than from a golden that quietly
healed.

A comment cannot do this job. The claim "no wildcard filter can reach it" sat
in four places for a whole PR and was wrong in all four; what it needed was
one two-process experiment nobody had run.

## The general shape

**"Hidden" is a default-run property, not a permission.** Before relying on any
"this is excluded" mechanism to protect a destructive action, ask what
*selects* it, and then try that selector. If the mechanism and the selector are
the same subsystem — a tag and a tag filter, a name and a name filter, a
default and an override — the exclusion is a convenience, not a gate.

Also: this is the second attempt at honouring
`memory/a-gen-script-runs-the-moment-you-invoke-it.md`. The first attempt
picked a mechanism that *looked* like "regeneration takes an explicit act" and
was never tested against the thing that would bypass it. A lesson can be
correctly cited and still incorrectly implemented.

## See also

- `memory/a-gen-script-runs-the-moment-you-invoke-it.md` — the original
  lesson: a generator must not run as a side effect of being invoked. This file
  is what it takes to actually satisfy it.
- `memory/a-prescribed-mutation-is-not-proof-the-check-catches-it.md` — the
  same family: a protection nobody ran the bypass against.
- `memory/a-fixed-defect-returns-through-the-silent-fallback.md` — the branch
  where the fix does not run quietly restores the bug.
