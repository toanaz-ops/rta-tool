---
name: a-shared-test-helpers-normalisation-blinds-every-guard-built-on-it
description: PR #50 round 1 (2026-09-27) put a "space before `(`" strip into the SHARED `app/tests/CodeLines.h::codeTextOf`, claimed "safe for every caller", and it silently broke `test_spl_publish.cpp`'s C3 guard, which anchors on the literal `"valuedb > "` (trailing space). A mutation of C3's own target went undetected on the "fixed" tree.
metadata:
  type: feedback
---

**2026-09-27, PR #50 round 1 (MEDIUM V1).** A path-guard fix needed to strip a
space before `(` so `.open (x)` reads the same as `.open(x)`. The builder put
the strip into `app/tests/CodeLines.h::codeTextOf` — a helper every source-text
guard in the suite calls to normalise a line before pattern-matching it — and
wrote the fix's own claim as "safe for every caller".

That claim was never checked against the OTHER callers, only against the guard
being fixed. `test_spl_publish.cpp`'s C3 anchors on `"valuedb > "` (note the
trailing space) to catch a re-derived alarm comparison. The new strip turned
`valueDb > (limitDb)` into `valuedb >(limitdb)` — no trailing space before `(`
— which silently stopped matching C3's anchor. A probe planted in
`SplAlarms.cpp` (`return valueDb > (limitDb);`) was MISSED on the "fixed" tree
and CAUGHT on `origin/main`'s, which is what round-1 verification's mutation
pass found; the original inspection-only claim had not.

**How to apply:**
- A change to a helper multiple guards build on is not "safe for every
  caller" until it is checked against every caller, not just the one being
  fixed. Enumerate the callers; run each one's own mutation probe against the
  changed helper before shipping.
- Prefer a LOCAL normalisation inside the one file that needs it over
  widening a shared helper's behaviour. The fix here reverted `codeTextOf` to
  byte-for-byte `origin/main` behaviour and moved both normalisations
  (`arrowsAsDots`, `spaceBeforeParenStripped`) into the path-guard test file
  alone, composed at the call site.
- "Safe for every caller" is a claim a shared-helper change must prove by
  mutation on every dependent guard, not a claim the guard being fixed can
  make on its own.

## Related

- `memory/merge-when-no-high-or-medium-remains.md` — the review policy this
  finding was made under (MEDIUM needs a real instance in the repo; this one
  had one, C3, immediately).
