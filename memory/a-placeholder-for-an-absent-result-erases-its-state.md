---
name: a-placeholder-for-an-absent-result-erases-its-state
description: When a function returns "nothing" for an all-absent input, a caller that substitutes a default placeholder silently rewrites every per-element reason inside it; the verifier found it, no fixture had reached it
metadata:
  type: project
---

**What happened (lane L6b-a, station 5, 2026-09-06).** `spatialAverage()`
returns `std::nullopt` when every bin is absent — the record's rule. The MTW
variant calls it once per band, and for a band that came back `nullopt` the
builder substituted a placeholder result whose every bin read
`{NoContributor, contributors = 0}`. But an all-absent band can be absent for
the *other* reason: two gate-passed positions with coherence exactly 0.0 at
every bin of that band carry `{NoWeight, contributors = 2}`, and the
placeholder overwrote that. Both shipped test fixtures were high-SNR and fully
gated, so the branch never ran. Verifier finding, fixed in `1b7d9a0` by
splitting the combine into an always-returning per-bin function that
`spatialAverage()` wraps with the all-absent rule, so the MTW path never has to
invent a band.

**Why.** "Return nothing when there is nothing" is a good contract at the top
level and a lossy one at the level below: the absence of a *result* and the
absence of each *element* are different facts, and a placeholder can only
carry the first. The fallback branch was written as "keep something sane",
which is the same shape as [[a-fixed-defect-returns-through-the-silent-fallback]].

**How to apply.** When a function's "nothing" return is consumed by a caller
that must still produce per-element state, give the caller the always-
returning form and apply the collapse-to-nothing rule once, at the outermost
level. And write the fixture that makes the whole input absent for the *second*
reason before calling the test set complete. Related:
[[a-fixture-can-be-too-well-behaved-to-fail]].
