# 006 — Multichannel workflows (lane L6b)

*2026-09-06. All five stations of lane L6b in one orchestrator session (Fable
5.1 orchestrating; Explore-type agents at station 1; Fable at station 2; Opus
at station 3; Sonnet at station 4; Fable-class verifiers at station 5, five
of them — one on the record before any code, one on the L3 small debts, one on
the core lane, one interim, one final). Research
`docs/research/2026-09-06-l6b-station1-research.md`, record
`docs/dsp/2026-09-06-multichannel-l6b.md`, plan
`docs/plans/2026-09-06-L6b-impl-plan.md`.*

## The numbers, from a clean rebuild

Independent verifier on `e048443` (the lane's last commit), scratch `git worktree`,
`--clean-first`, Visual Studio generator (never Ninja), MSVC 14.51:

```
cmake -S . -B build-off -G "Visual Studio 18 2026" -A x64
cmake --build build-off --config Release --parallel --clean-first  -> 0 "warning C"
ctest --test-dir build-off -C Release                              -> 100% tests passed, 0 tests failed out of 447

cmake -S . -B build-on  -G "Visual Studio 18 2026" -A x64 -DRTA_BUILD_APP=ON -DRTA_JUCE_PATH=...
cmake --build build-on  --config Release --parallel --clean-first  -> 0 "warning C"
ctest --test-dir build-on  -C Release                              -> 100% tests passed, 0 tests failed out of 508
```

The same verifier measured `10dd94f` (before F3) at 446 / 505, also clean.

Baselines measured in this session, not copied: **390 (OFF) / 436 (ON)** on
`60ba99c` at the start; the two L3 small debts took ON to 442; L6b-a took OFF
to 406 (from a measured 393 — the plan had predicted 390, stale by the three
JUCE-free app tests `a777887` added); L6b-b and the fix pass took OFF to 446
and ON to 505; F3 to 447 / 508. The one place the verified figures live is `docs/HANDOFF.md`'s
baseline block.

Guards after the lane, each proven RED then GREEN by inserting the forbidden
thing and watching it fail (builder at A5, reproduced by the L6b-a verifier):

```
core_has_no_framework_deps             OK, 107 files  (before: 96)
coherence_gate_is_not_bypassed         OK, 68 files   (before: 62)
filter_design_has_no_polynomial_form   OK, 125 files  (before: 113)
measure_has_no_framework_deps          OK, 42 files   (before: 30)
platform_types_has_no_framework_deps   OK, 5 files
```

## What was built

**`core/`** — `rta::dsp::spatialAverage` (`SpatialAverage.{h,cpp}`): the
weighted mean of dB magnitudes (default) or of power (option) over N
`TransferSnapshot`s with `W_i = u_i·γ²_i`; phase as the weighted circular mean
with its agreement `R = |z|`, absent under `R ≤ contributors·DBL_EPSILON`;
`weightedCoherence` and `phaseAgreement` beside magnitude and phase; per-bin
contributor count and two absence reasons, `NoContributor` and `NoWeight`; a
muted member (`u = 0`) is not a contributor; `nullopt` only when every bin is
absent; a throw on any grid mismatch. `spatialAverageBins` is the always-
returning form the MTW variant uses. `spatialAverageMtw` (`SpatialMtw.{h,cpp}`)
runs the combine once per band over `MtwResult::bandSnapshots` and reuses the
L3 stitch. `hasOverload` (`OverloadDetector.{h,cpp}`): three or more
consecutive samples at `|x| ≥ 1 − 2⁻¹⁵`. Golden `core/tests/golden/spatial.txt`
from `tools/gen_spatial.py` (argparse; `--help` writes nothing; `--check`
proves byte-identical regeneration).

**`platform/`** — `ChannelConfig` carries a per-channel transfer-function
index (a grouping tag, relaxed atomics) and an all-channels-of-role lookup.
The audio callback is untouched.

**`app/`** — three files split at their seams first (`Analyser.cpp`,
`SessionCodec.cpp`, `TransferView.cpp`); a `RoutingPlan` and N `Analyser`s
behind one drain that reads each distinct reference once per hop; the
`AverageGroup` wired into the live publish path so the spatial average is the
published trace plus one soloed position, the rest as `PositionSummary`;
level alignment over the displayed span; a `CaptureSequencer` that refuses on
`Overload` and `GateNotCleared` and drives no output; session schema 3 with
`[tf]` and `[average]` sections, device by name **and** channel count,
unbound-on-mismatch; `RoutingMatrix` mounted in `MainComponent`; readouts
`"k of N   R 0.32"`; `ui/az_ui` gained one generic `GridPanel` with no
measurement vocabulary.

`DualFftEngine.{h,cpp}`, `test_dualfft.cpp` and `platform/src/AudioIo.cpp` were
not edited (diff against `f048dda` empty at every station).

## What the research changed about the plan

Four premises the lane opened with were wrong, and the lane was re-scoped
before anything was built (research Part A §"corrections", memory
`the-parity-table-is-a-hypothesis`):

1. **SysTune "SSA" is not spatial averaging.** It is *Spectrally Selective
   Accumulation*, a temporal bad-data filter — a G20 precedent (and the only
   documented *soft* rejection anywhere), not a G15 one.
2. **REW has no coherence weighting.** Smaart is the only product that ships
   it, and Smaart does not publish the weight. The record chose `W = u·γ²` over
   the inverse-variance optimum on a derived argument (memory
   `positions-are-not-replicates`): positions are not replicates of one
   quantity, and the optimum lets a 0.99 position outvote a 0.9 one eleven
   to one.
3. **No source defines a bad capture at whole-capture granularity**, and no
   reachable M1 document defines any discard criterion. The record refuses on
   two criteria that need no invented number — Smaart's published overload
   rule and the existing coherence gate — and reports the coherence-trusted
   fraction without enforcing it (question filed for the owner).
4. **Generator solo/mute needs an output path that does not exist**: the
   callback clears every output, by design of Phase 1. Building it changes
   the audio callback's contract, so it left the lane with its own record
   pending (`HUMAN-QA-QUEUE`); the sequencer ships a step hook.

Two smaller findings shaped the design: the dB-vs-power question has a
standards answer (SMPTE ST 202 A.3.5 allows arithmetic averaging within a
4 dB spread, where the two differ by 0.45 dB) and the record keeps dB as the
default for a transfer function with the power option; and OSM's `Union`
reports `Σ|H|γ/Σ|H|` as the average's coherence, which is blind to two
coherent positions cancelling — so the record reports a *trust*
(`weightedCoherence`) and an *agreement* (`R`) separately and names neither
`coherence`.

## What the verifiers refuted

**On the record, before any code** (six findings, all folded in before
station 3 read it): §6 said one reference is handed to every `Analyser` while
§9 persisted a per-TF reference channel — reconciled as "per-TF reference
allowed; an average group requires a shared one"; `Σ W = 0` with contributors
present was an undefined `0/0` — the second absence reason `NoWeight` was
born there; the publish-churn sum had dropped a term (2.09 → 2.21 MB per
position, 335 → 354 MB/s at N = 8); the peak-bias claim now states its noise
model; the `1/n_d` coherence bias is marked UNVERIFIED; the delay-sign
precondition on shared `Sxx` is stated.

**On the plan** (six ambiguities, all resolved in the record before build):
guard counts for the real file set (96 → 106, 62 → 68, 113 → 124, all later
measured exactly); the golden tolerance shape; a derived floor for "phase
absent" (`contributors·DBL_EPSILON`); a muted member is not a contributor;
T12's bound became `bytes(8) − bytes(4) ≤ 4·sizeof(PositionSummary) + 4096`
instead of an unfalsifiable "within 2×"; T11's shared-reference precondition.

**On L6b-a (core).** One MUST-FIX: `spatialAverageMtw` substituted a
placeholder for a `nullopt` band and rewrote `NoWeight/2` as `NoContributor/0`;
no fixture had reached the branch. Fixed in `1b7d9a0` (memory
`a-placeholder-for-an-absent-result-erases-its-state`). The A2 tolerance the
builder "discovered by measurement" was judged a derived bound (boundary
leakage of a linear one-sample shift in a finite Hann window), not a grid
floor. All five mutations the verifier ran were caught.

**On L6b-b (app), interim.** The builder disclosed that `AverageGroup` and
`RoutingMatrix` were built and unit-tested but not wired into the live publish
path or `MainComponent` — record §6 was not yet true. The fix pass wired them
(`10dd94f`), and T12 now runs on the real path: bytes(4) = 1575,
bytes(8) = 1863, delta 288 ≤ 4352 for `AverageGroup::publish`; the full
gather including per-member snapshot copies reads 9511 / 17735 and is reported
without a bound. The builder also found that the per-channel index is a
grouping tag, not a unique key, and indexed the `Analyser` array by route
ordinal; the record adopted the amendment.

**On the L3 small debts** (`a777887`, `959a197`): confirmed in a scratch
worktree, 442/442, mutation of the toggle's pane wiring failed 15 of 28
assertions. `specimen.png` is the design swatch, not a `Snapshot` — the MTW
evidence lives in `transfer.png`.

**Final verifier, on `10dd94f`** (both configurations, `--clean-first`,
scratch worktree): no MUST-FIX; four mutations caught (a member on another
reference admitted; every position published in full; an overload refusal
turned into a stored capture; the channel-count check dropped from preset
binding). Five notes, one of which went back to station 4:

1. **A route naming a second reference vanished silently.**
   `syncAverageGroupMembership` pre-filtered the plan to the first route's
   reference before calling `addMember`, so the named refusal
   `DifferentReference` was unreachable in production and the route produced
   no `PositionSummary`, no contribution and nothing on screen — the record's
   "refuses visibly" was true of the class and false of the product. Fixed in
   F3 (see below).
2. The builder's guard counts 106 / 124 were 107 / 125 on a clean scan: F1's
   new test file enters both globs. Reporting slip, not a regression.
3. `RoutingMatrix` is capped at eight rows (the eight-`Analyser` memory
   policy), unlike `ChannelRoleTable`; channels 9+ cannot be assigned a
   transfer-function index through this view.
4. T12 proves O(1)-in-N for `AverageGroup::publish()` in isolation
   (delta 288 B ≤ 4352); the full production gather is measured
   (3739 / 11095 / 20903 B at N = 1 / 4 / 8 in the verifier's rebuild) and only
   monotonicity is asserted — the plan's own R5 resolution, stated plainly.
5. Mutation "publish every position in full" was caught by the structural
   test, not by T12's byte bound, because the fixture's `fftSize` is 64.

**Fix F3 (`e048443`)**: the pre-filter is gone; every route is offered to
`AverageGroup::addMember` and the real refusal is written into that route's
`PositionSummary::membership` (`Member`, `ExcludedDifferentReference`, …);
`RoutingMatrix` gained an AVG column. The builder found, from the offscreen
render, that a refresh driven only by a `juce::Timer` never runs under
`tools/snapshot.cpp` (no message loop is pumped) and moved it into
`resized()`, which the snapshot tool already calls. Re-verified independently on
`e048443`: 447 / 508, 0 warnings; reverting the file to `10dd94f` reproduced
the historical failure (`2 == 3` summaries), and forcing every membership to
`Member` failed the enum assertion. Four notes, none MUST-FIX, kept below as
gaps: the three-route test's comment says the average is built from two
members but all three analysers see identical audio, so that half is not
asserted; the vanish is closed by `mergeRoutePositions` (one summary per plan
route), not by the `addMember` call the comments credit; a route past the
eight-`Analyser` cap is never fed audio yet can read `Member` with stale data;
`RoutingMatrix` truncates to `min(routes, positions)` without a diagnostic.

## Known gaps, stated rather than hidden

- **Generator solo/mute is not built** (record §8); the sequencer's step hook
  is where an output path attaches. Own record pending.
- **Remote API is not built** (record §10); localhost bind and read-only
  proposed; owner's word pending in `HUMAN-QA-QUEUE`.
- **One live average group.** Membership is filtered to routes sharing the
  first route's reference; a second reference is visible state, not a second
  group. A second live group is a follow-up.
- **`RoutingMatrix` shows eight rows** (the eight-`Analyser` policy); channels
  9+ take no transfer-function index through this view, and a ninth route
  that shares the group's reference reads `Member` without ever being fed
  audio (`AnalysisThread.cpp:204`) — a distinct membership state is owed.
- **The three-route publish test does not prove the average excludes the
  refused route numerically** (identical audio on all three); a fixture with
  a distinguishable third position is owed.
- **Coherence-trusted fraction is reported, not enforced** — a number for it
  needs a real system.
- **Spatial-average traces are live-only**, inheriting MTW's L5 amendment.
- **B3 and B7 were not strict compile-RED TDD** (builder disclosure); the
  mutations were run and failed before revert, which is the evidence that
  counts, and the final verifier judged whether the tests are tautologies.

## How this landed

Branch `claude_desk/l6b-multichannel-research-4b3876`, `git rev-list --count
main..HEAD` — measure, do not copy. Not merged; merge is the owner's word.
