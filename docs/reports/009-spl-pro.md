# 009 — SPL-pro (lane L6a)

*2026-09-26, lane closeout. L6a ran across nine pull requests over three
sessions: stations 1+2 (research + record) on 2026-09-16, station 3 (the
implementation plan) on 2026-09-17, station 4 Wave 0 (the publish path) and
Wave 1 (core pure math) on 2026-09-18, and this session's Waves 2-4a plus
Task G and the W2-E wiring sub-lane, 2026-09-25/26. Research
`docs/research/2026-09-16-l6a-spl-pro-station1-research.md`, record
[`docs/dsp/2026-09-16-spl-pro-l6a.md`](../dsp/2026-09-16-spl-pro-l6a.md)
including its §15 amendments, plan
[`docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md`](../plans/2026-09-17-L6a-spl-pro-impl-plan.md).*

**Lane state: BUILT and CLOSED OUT.** Waves 0-3, 4a, Task G and the W2-E
wiring sub-lane are on `origin/main`. **Wave 4b (the served web viewer) is
CUT** by the owner's decision, 2026-09-25, in the orchestrator's chat — not by
a failed test. Full numbers, what a verifier found wrong, and what is honestly
still unverified follow below.

---

## 1. How it landed

Wave 0 (PR #17, `b1e14a9`) and Wave 1 (PR #20, merged up to `d071269`) landed
in an earlier session and are covered by `docs/HANDOFF.md`'s earlier entries
and the master plan's L6a row. This report covers what landed after that,
in the order it actually merged to `origin/main`:

| PR | branch | merge commit | what |
|---|---|---|---|
| #27 | `l6a/wave3-calibration` | `8f8df27` | Wave 3 tasks W3-A/B: `CalibrationSession`, CAL START/END wiring. Merged first; #26 absorbed it via a merge-up (`6d6c8eb`) before merging itself |
| #26 | `l6a/wave2-app` | `8c5d407` | Wave 2 tasks W2-A..D: SPL history ring, alarms, `#key=value` log pipeline, the SPL pane |
| #28 | `l6a/wave4a-report` | `66c5f32` | Wave 4a (the report) + W3-C (calibration in the report) |
| #30 | `l6a/task-g-guards` | `e3253d7` | Task G: `report_makes_no_class_1_claim`, and three fixes to the guard script itself |
| #29 | `l6a/w2e1-spl-state` | `f992ee7` | W2-E1: `SplChannelState` — the analysis thread owns per-channel SPL state and publishes it correctly per weighting chain |
| #31 | `l6a/w2e2a-log-wiring` | `10da3dc` | W2-E2a: the log pipeline's SPSC-to-writer-thread wiring, composition-root enable/disable, epoch restart |
| #32 | `l6a/w2e2b-calibration-export` | `65d8efc` | W2-E2b: calibration applied to the live session, calibration record at read time, report export from a live session |
| #33 | `l6a/low-followups` | `04b42a6` | 17 deferred LOW findings, including three file splits to stay under the 400-line cap |

Every SHA above is confirmed present in `git log --oneline origin/main`.

### W2-E — the wiring nobody was assigned

The station-3 plan built and unit-tested every SPL component (block
accumulator, histogram, alarms, dose, log) and assigned no task to wire them
into the running app. `enableSplLogging` had no caller; the publish fold left
alarms, dose and Ln empty through Wave 0. PR #27's verifier found this and it
cost three PRs (#29, #31, #32) and eleven verifier rounds across them to close
— see `memory/a-component-with-no-production-caller-is-not-shipped.md` for the
lesson and how to catch this class of gap earlier. The plan now carries a
"### W2-E — the wiring nobody was assigned" section documenting the three
sub-tasks this created.

---

## 2. What shipped

**Wave 2 (app).** `SplHistory` — a bounded ring of `Block`s plus a fixed-32-byte
marker type (`kMaxMarkers = 4096`, overflow counted rather than silently
dropped) — `SplAlarms` (`SplAlarmState::{Filling,Clear,Fired}`, closed-form
headroom, no invented hysteresis or debounce), the `#key=value` log pipeline
(`SplLogPipeline`, `SplLogWriter`) writing `ch<N>.gen<G>.seg<S>.csv` files plus
a `session.header.txt`, and the SPL pane (`app/src/view/SplView.cpp`,
`SplStrip.h`) reachable through the `PaneView::Spl` pane type
(`app/src/view/PaneRegistry.h`).

**Wave 3 (calibration).** `CalibrationSession` (`app/src/measure`, OFF,
JUCE-free): a start/end pair measured through the same Z-weighted chain a
session uses, drift between them, and a verdict against **ISO 1996-2:2017 cl.
5.2**'s 0.5 dB criterion (`CalibrationSession::kClause`,
`CalibrationSession.h:89`). `MainComponent` gained CAL START / CAL END buttons
and a readout wired the same way LOCATE/APPLY already are.

**Wave 4a (report).** One self-contained HTML document, nine sections, an
integrity hash, and a load-bearing honesty sentence that states plainly what
the tool does and does not claim conformance to. It is generated from a frozen
payload — no server, no network, and (by design) not gated on anything Wave 4b
needed.

**Task G (guards).** `report_makes_no_class_1_claim`, a second, independently
scoped instance of the "no Class 1/2 conformance claim" guard, registered from
`app/tests/CMakeLists.txt` against the `SplReport*` family (7 files). Its own
verifier round found three holes in the *guard script itself* — a claim split
across two adjacent C++ string literals the regex could not see across, `[01]`
that should have been `[012]` (the plan's acceptance gate covers Class 1 *and*
Class 2), and a self-reference exemption list that never expired (the shape
`memory/a-naming-grep-that-bans-a-word-bans-its-own-justification.md` warns
about) — all three fixed in the same PR.

**W2-E (the wiring).** `SplChannelState::onBlockClosed` now routes alarms,
dose and Ln to the *specific* weighting chain each consumer's own definition
names (PR #29), the log pipeline runs on its own writer thread fed by an SPSC
queue with the composition root owning enable/disable and epoch-triggered
restart (PR #31), and calibration is applied to the live session — a fresh log
starts, the calibration record is written, `CalibrationInvalid` is set at read
time when a channel mismatch is detected, and the report can be exported from
a live session (PR #32).

**LOW batch (PR #33).** 17 deferred LOW findings from earlier verifier rounds,
including splitting `ApiServer.cpp`, a CMake file, and `AnalysisThread.h`
(via a new `AnalysisThreadSplState.h`) to stay under the project's 400-line
hard cap.

### What was cut, and why

**Wave 4b (the served web viewer) is CUT.** Not a test failure: the owner
decided to cut it, 2026-09-25, in the orchestrator's chat. The record's own
§9 fallback is what the plan and this report both fall back to: the §12
constraint-2 rounding obligation (which viewer numbers must round the same way
the desktop app does) is now **recorded as untested for the viewer**. Because
Wave 4b never shipped, its Gate 2 — the unrun Chrome Local Network Access
test, whether a page served from `127.0.0.1` fetching `127.0.0.1` is exempt
from Chrome's Local Network Access prompt — is now **moot for this lane**,
not merely deferred. Wave 4a (the report) is untouched by the cut; it never
depended on Wave 4b's gates.

---

## 3. What review caught

Independent verifiers, across the eight PRs above, found and the builder
fixed:

1. **A C-weighted dose/alarm published under an LAeq label.** Measured wrong
   by 340x on dose — a substituted-convention error of exactly the shape
   `SplCriteria.h` exists to prevent, now caught because the fix routes each
   consumer to its own named weighting chain (PR #29).
2. **Ln fed from the max-held level, not 100 ms Fast samples.** This made
   `L90 == L1` — every percentile collapsed to the same block-max value
   instead of sampling the Fast detector every 100 ms as record §5 specifies.
3. **`SplMeter` losing samples uncounted when a hop closed more blocks than
   its ready buffer could hold.** A fixture had been narrowed to fit
   `BlockAccumulator::kReadyCapacity` and called out of scope; the verifier
   probed the shipped path instead of the shrunk fixture and found real,
   uncounted sample loss on the path a real hop actually takes. See
   `memory/a-fixture-shrunk-to-fit-a-limit-hides-the-limits-defect.md`.
4. **Allocation in the analysis-thread feed.** A per-hop feed path allocated
   where the real-time contract (CLAUDE.md's "no allocation, no locks, no file
   I/O, no FFT" in the audio callback path) forbids it; fixed with a
   pre-sized, per-thread `AllocationProbe`.
5. **A sample-rate change mid-session corrupting the log header and filters.**
   Fixed by `pollSplLogging`'s epoch check: a device or sample-rate change
   while logging is active now closes the old session and opens a fresh one,
   rather than appending mismatched data to the old header.
6. **A calibration ROUTE index used as a CHANNEL index.**
   `kCalibrationRouteIndex = 0` is a route *position*; three channel-indexed
   calls used it directly as a channel number. Default routing (route 0 =
   channel 0) hid this on every manual test run; one operator click assigning
   a different channel to the Measurement role would have silently lost the
   drift verdict. Fixed to resolve the channel from the routing plan the
   capture actually used, once, never re-derived (`50e53a8`). See
   `memory/an-index-from-one-table-used-in-another.md`.
7. **A dangling reference, red only on gcc/clang.** Found by the three-OS CI
   matrix, not by local MSVC-only testing — the same class of cross-toolchain
   finding L-API's `D7`/FP-contraction lesson documents.
8. **`Clear` published for a comparison that had never actually run.** An
   alarm whose window had not yet filled published `Clear` — the wrong state
   for "no verdict yet" — rather than the honest `Filling` state record §15's
   A6 amendment introduces.
9. **A report missing the "untested for the viewer" sentence.** Wave 4a's
   first cut shipped without the sentence the Q8 default requires when the
   viewer does not ship; added.
10. **Marker/excluded-region x-scale bugs in the report.** The time-history
    strip's markers and its calibration-excluded shaded region were computed
    against a different x-scale than the trace itself, so they drifted out of
    alignment with the data they were meant to annotate; both now share one
    x-scale function.

Guard-script findings (Task G, §2 above) are a separate category — three holes
in the guard itself, not in the code the guard polices — and are listed there
rather than repeated here.

---

## 4. The numbers

**Final tallies, measured at `04b42a6`** (builder + verifier, on PR #33's fix
head `34cbf69`; the commits after that are test-claim corrections only and do
not change the count):

```
ctest --test-dir build -C Release      (RTA_BUILD_APP=OFF)  -> 1019/1019, 0 failed
ctest --test-dir build-on -C Release   (RTA_BUILD_APP=ON)   -> 1101/1101, 0 failed
grep -E "warning( [A-Z]+[0-9]+)?:" across every Release build log -> 0
CI (ubuntu-latest / macos-latest / windows-latest)            -> 3/3 green
```

**Baseline at the start of this session**, `main` at `411f1e7` (Wave 0 + Wave 1
already merged from the prior session): **OFF 824**.

**Per-PR tallies, pasted from each PR body** (own build directories, not
independently re-measured by this closeout — see §5 for what that means):

```
PR #27 (Wave 3, after its fix round)     OFF 834/834   ON 908/908   fallback OFF 834/834
PR #26 (Wave 2, absorbed #27)            OFF 882/882   ON 958/958
PR #29 (W2-E1, round 4)                  OFF 937/937   ON 1013/1013
PR #30 (Task G)                          OFF 906/906   ON 982/982
PR #32 (W2-E2b)                          OFF 973/973   ON 1054/1054
merged tree at 04b42a6 (this closeout's figure) OFF 1019/1019   ON 1101/1101
```

The ladder above is not monotonically increasing PR-by-PR because these
branches were not all built from the same parent commit — several merged up
`origin/main` mid-flight (PR #26 absorbed PR #27; later PRs absorbed earlier
merges) — so a PR's own reported tally reflects its own branch tip, not a
running total. The only tally this report asks a later session to trust
without re-deriving is the final one at `04b42a6`.

**Guard counts** (read from each guard's own `OK (N files scanned)` line,
never predicted): `report_makes_no_class_1_claim` **7 files** scanned in
both configs (new in this lane, Task G); `core_makes_no_class_1_claim` and
`measure_has_no_framework_deps` continued rising through Waves 0-4a as SPL
files were added (see the master plan's L6a row for the wave-by-wave file
counts carried over from Wave 0/1).

---

## 5. What is NOT verified

- **No real hardware.** No session in this lane connected a real calibrator
  or a real microphone. Every number above comes from synthetic mode and
  fixtures. `docs/HUMAN-QA-QUEUE.md` "Mục mới mở khi đóng lane L6a" carries a
  new open item asking for exactly this run.
- **The SPL pane is not reachable in the running `rtatool.exe` GUI today.**
  `MainComponent` still builds exactly one default workspace (a single `"rta"`
  pane); nothing in it loads a session, and no session-loading UI exists yet.
  The `"spl"` pane type (`PaneView::Spl`) is proven by ctest and by the
  offscreen `rtatool_snapshot` render (`shots/preview-spl.png`), not by
  opening it in a running window. `docs/HANDOFF.md`'s new top section states
  this without softening it.
- **Q11 (which `BlockFlag`s exclude a block from `combineBlocks`) has a
  shipped default (`CalibrationInvalid` alone) but no standards citation.**
  Both governing clauses — IEC 61672-1 cl. 3.28 (validity) and ISO 1996-2 cl.
  10.3 (incomplete or corrupted data) — are paywalled and unread. The project
  does not claim a standards basis for this default in either direction and
  says so in code and in the queue.
- **The Chrome LNA test is moot, not passed.** Wave 4b's Gate 2 was never run
  because Wave 4b was cut before it was reached (§2 above). This is different
  from "run and failed" — nobody knows whether it would have passed.
- **Per-PR tallies in §4's ladder were not independently re-built by this
  closeout.** Only the final tree at `04b42a6` is this report's own claim;
  the intermediate figures are transcribed from each PR's own body.

---

## 6. Numbers a later session must not re-derive

- **Final tallies at `04b42a6`: OFF 1019/1019, ON 1101/1101, 0 CI-pattern
  warnings, CI 3/3.**
- **`CalibrationSession::kClause = "ISO 1996-2:2017 cl. 5.2"`**
  (`app/src/measure/CalibrationSession.h:89`) and its 0.5 dB drift bound are
  the only published calibration criterion; do not invent a second one.
- **The SPL log's file-naming convention**: `<sessionDir>/ch<N>.gen<G>.seg<S>.csv`
  (`app/src/measure/AnalysisThreadSpl.cpp:119`, `app/src/export/SplLogWriter.cpp:35`)
  plus `session.header.txt`, under
  `Documents/RTA Tool/spl/<UTC timestamp>-e<epoch>/`
  (`app/src/MainComponentSpl.cpp:65-78`).
- **Q11's shipped default is `CalibrationInvalid` alone excludes a block from
  `combineBlocks`** — `Overload`, `UnderRange`, `Dropped`/`Gap` do not. This is
  a named default with no standards citation behind it (§5 above), not an
  inferred rule; do not re-derive a different one without a citation.
- **Wave 4b is cut, and the record §12 constraint-2 rounding obligation is
  recorded as untested for the viewer.** A later session reopening the viewer
  starts from that recorded gap, not from an assumption that rounding was
  proven equivalent.

---

## 7. Related documentation

- Master plan: `docs/plans/MASTER-EXECUTION-PLAN.md`, L6a row and P6 dependency
  node — both updated by this closeout.
- Handoff: `docs/HANDOFF.md`, new top section "2026-09-26 — L6a (SPL-pro) lane
  CLOSED", with the exact commands a human can run and what they should see.
- Owner queue: `docs/HUMAN-QA-QUEUE.md`, "Từ lane L6a" (Q2 and Q8 now closed)
  and the new "Mục mới mở khi đóng lane L6a" section.
- Plan: `docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md`, Wave 4b heading now
  carries the cut note.
- Memory: three new lessons —
  `memory/a-component-with-no-production-caller-is-not-shipped.md`,
  `memory/a-fixture-shrunk-to-fit-a-limit-hides-the-limits-defect.md`,
  `memory/an-index-from-one-table-used-in-another.md`.
