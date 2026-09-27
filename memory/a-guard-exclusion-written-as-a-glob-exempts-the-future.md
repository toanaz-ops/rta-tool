---
name: a-guard-exclusion-written-as-a-glob-exempts-the-future
description: PR #50's file-length guard exempted three pre-existing over-cap scripts by a `gen_*`/`probe_*` glob. The round-2 verifier showed the glob exempted 25 files and every FUTURE gen_*/probe_* script -- a fabricated 401-line `tools/gen_zz_probe.py` passed the guard clean. Fixed by listing the three exact paths instead.
metadata:
  type: feedback
---

**2026-09-27, PR #50 round 2 (V3-a).** `core/tests/check_file_length.cmake`'s
`TOOLS_DIR` scan was widened to cover `tools/*.py` (PR #50 LOW V3), and three
pre-existing scripts were over the 400-line cap: `gen_generator.py` (452),
`probe_align_order4.py` (519), `probe_l4b_truncation.py` (534). The first fix
exempted them with a name-pattern glob, reasoning that `gen_*.py`/`probe_*.py`
are one-off generator/probe scripts by convention and out of scope for a LOW
batch.

The round-2 verifier planted a fabricated `tools/gen_zz_probe.py` at 401 lines
and ran the guard: it passed, clean. The glob did not exempt three named
files, it exempted a NAMING CONVENTION -- 25 files matched it on the tree at
the time, and every script anyone writes tomorrow that happens to start with
`gen_` or `probe_` inherits the exemption for free, with no review and no
date attached.

**How to apply:**
- A guard exclusion list is a scope decision made about specific, named
  artefacts on a specific date. Write it as exact paths, each with the date
  and the reason it was excepted, never as a pattern that also matches
  everything not yet written.
- Prove the exclusion is as narrow as claimed the same way you'd prove a gate
  fires: plant a new file that satisfies the pattern but not the intended
  exemption, and confirm the guard still catches it (mutation, applied to the
  exclusion list itself, not just to the gated code).
- The fix here: `core/tests/CMakeLists.txt`'s `EXCLUDES` now lists the three
  files by exact path; the same fabricated-probe mutation goes RED (exit 1)
  after the fix, and the real baseline (627 files) stays green.

## Related

- `memory/a-tally-counts-the-prefix-it-greps.md` — a different shape of the
  same trap: a check written broader (or narrower) than the thing it claims
  to gate.
