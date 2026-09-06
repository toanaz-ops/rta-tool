---
name: a-cap-checked-on-the-drain-path-is-unchecked-on-the-publish-path
description: The drain guarded route positions at kMaxTransferFunctions; the publish-membership sync did not, so it admitted routes 9+ as members and indexed an 8-slot analysers[] out of bounds — reachable on any interface with more than 8 measurement channels against one reference
metadata:
  type: project
---

**What happened (lane L6b closeout, 2026-09-06).** `AnalysisThread` builds
exactly `kMaxTransferFunctions` (8) `Analyser`s. Two code paths index that
vector by route POSITION. The drain path guarded the bound —
`drainPaired` has `if (routeIndex >= kMaxTransferFunctions) continue;` — so a
9th+ route got its samples discarded but no analysis. The publish-membership
path (`syncAverageGroupMembership`) had **no such guard**: it offered every
route in the plan to `AverageGroup::addMember`, so routes at positions 8, 9, …
sharing the group's reference became members, and `publishAverageGroup` then
did `analysers[index]` for `index >= 8` on a vector of exactly 8 — an
out-of-bounds read. With `kMaxChannels = 64`, up to ~63 measurement channels
can name one reference, so this is reachable on a real Dante/MADI interface —
exactly this tool's target hardware — not a theoretical bound. The 2026-09-06
verifier flagged it as an F3 NOTE ("route 9+ can still read `Member`"); the
truth underneath the note was a heap over-read, worse than the note's wording.
Fixed by capping both the prediction and the rebuild loop in
`syncAverageGroupMembership` at `i < kMaxTransferFunctions`, and giving an
over-cap route a distinct `Membership::ExcludedOverCapacity` summary.

**Why.** A cap is a property of a shared array, not of one function. Enforcing
it on the path that *writes/drains* the array says nothing about the path that
*reads/publishes* it: they index the same storage but ran through different
authors on different days, and the second one restated the membership rule
without the bound. A guard proves only the branch it sits in. The constant also
lived in `AnalysisThread.h`, which the publish layer cannot include (the
dependency points the other way), so the two sides could not even name the same
number — the fix moved `kMaxTransferFunctions` down to `RoutingPlan.h` so both
sides share one definition.

**How to apply.** When a fixed-size buffer is indexed from more than one place,
find every indexer and confirm each clamps to the same cap; a bound on the
write path is not a bound on the read path. Put the cap constant in the lowest
header both indexers include, so "the cap" is one symbol, not two literals that
can drift. And when a verifier files a NOTE about a value being "still
readable", check whether the real defect is an out-of-bounds access before
accepting the milder framing. Related: [[positions-are-not-replicates]],
[[a-placeholder-for-an-absent-result-erases-its-state]].
