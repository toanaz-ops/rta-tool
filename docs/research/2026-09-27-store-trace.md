# STORE lane — station-1 research

*2026-09-27. Scope: `docs/plans/MASTER-EXECUTION-PLAN.md`'s "Plan review —
2026-09-27" named STORE as the next lane and the one thing every other
stalled consumer (Save/Open, XOVER, L7-EQ-UI, L7-ALIGN-UI, any RT60 UI)
waits on. Read at `origin/main` `4fc6b6e` (PR #45, XOVER pane, merged) — one
commit newer than the plan review's own verification point `e2c67a1`; see
"Corrections to the brief" below for what moved in that gap. Research and
plan only, per the owner's instruction — no production code, no sub-agents.*

---

## Part A — What exists

### A1. What a `Trace` is

`app/src/trace/Trace.h:51-103`. A `Trace` is immutable once built:
`CaptureMeta meta_` plus three parallel `std::vector<float>` — magnitude
(required), phase, coherence (both optional, empty means absent, never a
zero-filled stand-in — `Trace.h:75` `has(Field)` tests emptiness). The
frequency axis is **derived, never stored**: `binHz() = sampleRate/fftSize`
(`Trace.h:91`), one bin per index, DC through Nyquist inclusive
(`pointCountFor`, `Trace.h:44`). `Trace::make` (`Trace.h:55`) refuses a
magnitude whose length disagrees with `pointCountFor(meta.fftSize)`.

`CaptureMeta` (`Trace.h:25-41`) is the immutable capture record: id, capture
time, device name, channel roles, sample rate, fftSize, window, averaging
type/depth, effective averages, applied delay samples, calibration offset
and unit (dBFS vs dBSPL — `Trace.h:20` warns explicitly that mixing the two
"stays plausible... and nothing on screen says so").

Phase convention: `Trace`'s phase field is **radians**, per
`app/src/trace/VirtualTrace.h:7` ("A stored Trace is float dB plus wrapped
radians"). The live path carries **degrees** (`TransferBlock::phaseDeg`,
`Snapshot.h:47`) — see A4.

### A2. What the live `Snapshot` holds, RTA vs TRANSFER

One `Snapshot` (`app/src/measure/Snapshot.h:208-270`) carries both at once;
"RTA mode" / "TRANSFER mode" are which pane is *showing*, not two different
snapshot shapes:

- **Always present**: `spectrumDb` (`Snapshot.h:220-224`) — raw per-bin
  magnitude, `fftSize/2+1` long, magnitude only, no phase, no coherence.
  This is what `RtaView` reads for the bar/spectrum overlay.
- **Present only when a reference is fed** (`hasReference`,
  `Snapshot.h:236`): `transfer` (`std::optional<TransferBlock>`,
  `Snapshot.h:228`) — the **fixed-FFT** magnitude+phase(deg)+coherence
  (optional)+effectiveAverages+appliedDelaySamples block
  (`Snapshot.h:45-63`), and `mtw` (`std::optional<MtwBlock>`,
  `Snapshot.h:234`) — the multi-time-window stitched curve, **with its own
  explicit, non-uniform frequency vector** (`Snapshot.h:96-101`: "LIVE-ONLY
  until an L5 amendment gives storage a frequency vector of its own").
  `TransferView`'s three panes default to MTW as the on-screen curve
  (`TransferView.h:36,132`; record §6), but MTW is what cannot be frozen
  into a `Trace` today (A3).

### A3. What conversion from Snapshot to Trace is missing

Nothing converts a `Snapshot` into a `Trace` anywhere in `app/src` today.
Concretely, five gaps:

1. **Unit crossing.** `transfer->phaseDeg` is degrees; `Trace`'s phase field
   is radians (A1). The one existing deg↔rad crossing is
   `AnalyserPublish.cpp:20-22` (rad→deg, building the Snapshot in the first
   place) — a STORE conversion needs the *inverse* crossing, and should
   reuse `180.0/std::numbers::pi` the same way rather than re-deriving it
   (dual-FFT-conventions memory: a second spelling of one constant is how
   two places drift).
2. **MTW cannot fit `Trace`'s shape.** `Trace::binHz()` derives an implicit,
   uniform axis from `fftSize`; `MtwBlock::frequencyHz` is explicit and
   non-uniform (A2). Freezing the MTW curve would need either a lossy
   resample onto the fixed grid (no closed-form/standard to test it
   against — fails CLAUDE.md's verification standard) or the L5 storage
   amendment `MtwBlock`'s own comment names as not-yet-written. CLAUDE.md
   rule 6 ("a record amendment is written against shipped code, not ahead
   of it") rules out writing that amendment now. See candidate design C1.
3. **No caller assembles a `CaptureMeta`.** No file in `app/src` or
   `app/tests` constructs one with more than `.id` set
   (`grep -rn "CaptureMeta" app/tests/test_trace_library.cpp:10-11`, the
   only non-trivial construction found). `sampleRate`/`fftSize` are on the
   `Snapshot` directly (`Snapshot.h:214-215`); `effectiveAverages`/
   `appliedDelaySamples` are on `TransferBlock` directly
   (`Snapshot.h:56-62`) — those four are a straight copy. The rest are not:
   - `window`/`averagingType`/`averagingDepth` live on
     `Analyser::Config` (`app/src/measure/Analyser.h:40-79`:
     `window`, `averaging`, `timeConstantSeconds`), which `AnalysisThread`
     stores as a **private** `baseConfig_` (`AnalysisThread.h:285`) with
     **no public getter**. `MainComponent` does not keep its own copy
     either — it passes a throwaway temporary at construction
     (`MainComponent.cpp:24`: `rta::measure::Analyser::Config{}`). Today
     these settings never change at runtime (no UI touches them), so a v1
     STORE could read `Analyser::Config{}`'s compile-time defaults
     directly, but that silently goes stale the day a runtime control is
     added. Recommend a small `AnalysisThread::config() const` getter
     instead (station-3 task, below).
   - `deviceName`/`channelRoles` live on `AudioIoBus`/`rail_` state, not on
     `Snapshot`.
   - `calibrationOffsetDb`/`calibrationUnit`: the only calibration offset in
     the tree is `SplConfig::referenceOffsetDb` (`SplConfig.h:125`), scoped
     to an active SPL-logging session, not a general "current dBFS→dBSPL
     offset" any pane can read. See candidate design C3.
4. **Id generation.** `TraceLibrary::add` refuses a duplicate id, returning
   `""` and changing nothing (`TraceLibrary.cpp:62`, `TraceLibrary.h:56-62`).
   `Snapshot::sequence` or a wall-clock millisecond timestamp are both the
   "obvious" id source and both collide under two STORE presses that land
   between two publishes (the analysis thread publishes at up to 20 Hz —
   `RtaView.h:23` — so two manual clicks are unlikely to collide in
   practice, but "unlikely" is not a proof, and a scripted/automated STORE
   or a synthetic-mode capture with no live publish loop makes it
   reachable). Needs a dedicated monotonic counter, the same shape
   `TraceLibrary::generation_` already uses (`TraceLibrary.cpp:13-16`).
5. **No production caller for `TraceLibrary::add` that captures anything
   new.** `library_.add` has exactly one production caller today,
   `MainComponentSession.cpp:187`, inside session **Open** — it replays
   `Trace`s that a previous session already saved to disk
   (`SessionDecode`). It never freezes a *live* measurement. This is a
   narrower and more precise statement than the plan review's "zero
   production callers" (verified at `e2c67a1`, before PR #43 merged) — see
   "Corrections to the brief."

### A4. Which thread may read the Snapshot

`Snapshot`s are published by `AnalysisThread` (the analysis thread) through
an atomic pointer swap and read via `SnapshotSource::latest()`
(`SnapshotSource.h:16-20`), which is explicitly safe from any thread
(`AnalysisThread.h:107-112`, "same atomic-pointer-swap shape as `latest()`").
The established **production** pattern reads it from the **message
thread**, on a UI action, never the audio callback:
`MainComponent::refreshMembershipFromSnapshot` (`MainComponent.cpp:283`, a
timer poll) and `MainComponent::exportReportClicked`
(`MainComponentSpl.cpp:142`, a button click) both call
`analysisThread_.latest()` directly from `MainComponent`. STORE follows the
exact same shape: a button's `onClick` on the message thread calls
`analysisThread_.latest()`, builds a `Trace`, and calls `library_.add(...)`
— no allocation-sensitive code runs on the audio callback, satisfying
CLAUDE.md's real-time-safety rule because nothing here touches it.

---

## Part B — How others do it

### B1. Open Sound Meter (real code, `github.com/psmokotnin/osm`,
`src/source/stored.{h,cpp}`, read 2026-09-27)

OSM's `Stored` (`Abstract::Source` subclass) is built by
`Stored::build(source)` → `source.copyTo(*this)` (`stored.cpp:60-63`): a deep
copy of the live source's per-bin array (`frequency, module, magnitude,
phase, coherence, peakSquared, meanSquared`, `stored.cpp:77-84`) plus the
full impulse-response time series (`stored.cpp:96-106`). This is the
**unsmoothed averaged estimate**, not the displayed smoothed curve — OSM
applies octave smoothing at render time, uniformly, to live and stored
sources alike, so freezing the raw array is what keeps a stored trace
comparable to a live one under the same smoothing later.

Distinctively, `Stored` carries **adjustable** `gain`, `delay`, `polarity`,
`inverse`, `ignoreCoherence` properties that are applied live, every read,
*after* the freeze (`stored.cpp:222-247`: `phase(i)` computes
`alpha = (polarity?PI:0) - 2*PI*delay*frequency/1000` and rotates the frozen
phase by it). A stored trace is not fully frozen — a further delay/gain/
polarity trim can still be dialed in against it after the fact, independent
of whatever the live engine's own delay compensation did at capture time.

Naming: `Stored::autoName(prefix)` (`stored.h:37`, `stored.cpp:65-71`) —
`prefix.mid(0,7) + " @ HH:mm"`, i.e. a 7-character source-name prefix plus a
minute-resolution timestamp. Store is **per-source** (one measurement
channel/pair, not a whole multi-channel group at once — a group is stored
by storing each member). Live delay compensation is untouched by storing —
the live source keeps measuring; the stored copy is an independent object.

### B2. Friture (real code, `github.com/tlecomte/friture`, read 2026-09-27)

`friture/store.py` exists but is a Qt/QML **UI dock-state** store (which
panels are open, `_dock_states`), unrelated to measurement data. Friture has
no reference-channel transfer-function measurement, no coherence, and no
"freeze this curve" feature at all — it is a single-channel real-time
visualiser (spectrum, spectrogram, level meter). **No analogue exists to
compare**; excluded from the candidate table below for that reason, not
overlooked.

### B3. REW (docs only, closed source)

REW has no continuously-averaging live curve that gets frozen on demand the
way OSM/Smaart/this project do. Each "measure" (a sweep, or a timed RTA/SPL
capture) **creates a new, already-independent Measurement** in the
measurement list and selects it — capture-as-store, not live-then-freeze.
Its Overlays window plots any subset of the stored measurement list
together (trace-options dialog for line style, not for re-deriving the
curve). This is a materially different interaction model from ours: there
is no "STORE" verb because there is no live rolling estimate to distinguish
from a captured one.

### B4. Smaart (docs only, closed source)

Smaart's Live tab tracks a per-trace **Measurement Time Reference (MTR)**
— formerly "Measurement Delay" — alongside each transfer-function trace, and
an averaged/static trace's own MTR is computed as the average of its
contributors' MTRs (Rational Acoustics support articles, "What's New in
Smaart", "What is Average Time Reference in Smaart?", read 2026-09-27).
This directly supports storing delay-compensation metadata *with* the
trace rather than assuming one global engine setting applies to everything
downstream — exactly what `CaptureMeta::appliedDelaySamples` already exists
to do (A1).

### B5. SysTune

No public source and no documentation of its store semantics was found
within this pass's scope; not included in the comparison rather than
guessed at (a specific, undated claim about a closed product's internal
behaviour is exactly the kind of thing a later reader cannot verify —
`memory/a-public-issue-has-a-date-too.md`'s own lesson, applied here to "no
evidence" instead of "stale evidence").

### Where they disagree

| | freezes | smoothing | naming | scope | live delay comp |
|---|---|---|---|---|---|
| OSM | raw averaged array + IR | applied at render, live+stored alike | `name(0:7) @ HH:mm`, editable | per source/channel | untouched; stored copy independently adjustable |
| Friture | n/a — no TF measurement | n/a | n/a | n/a | n/a |
| REW | n/a — capture IS the store | per-measurement setting | user-typed or auto | per measurement | n/a (no live/frozen split) |
| Smaart | (docs don't say array vs curve) | (not found) | (not found) | per trace, own MTR | own MTR carried with the trace |

The one point of real agreement across every system with a transfer-function
concept (OSM, Smaart) is that **delay/timing information travels with the
trace**, not as a single mutable global the trace silently inherits later.
`CaptureMeta` already has the field (`appliedDelaySamples`); the design
question is only whether STORE also needs OSM's *further, adjustable*
trim on top (Candidate discussion, Part C).

---

## Part C — Candidate designs

### C1. What gets frozen: fixed-FFT `transfer`, or the MTW curve

**Recommended: the fixed-FFT `TransferBlock`.** It is the only shape that
fits `Trace`'s existing storage format with no schema change (A3.2). The
MTW curve is the pane's own *default displayed* source (record §6), which
makes "store the fixed-FFT block" the non-obvious choice worth arguing for:
an operator storing what they are looking at (MTW) would get a trace that
silently doesn't match the screen. Rejecting MTW-for-now costs exactly
that — a stored trace can read slightly different (more noise-floor detail,
less of MTW's low-frequency time-averaging gain) than the live curve the
operator was staring at when they pressed the button. The alternative
(resample MTW onto a uniform grid to fit `Trace` today) has no
closed-form/standard acceptance and would be graded against nothing but its
own output — refused by the verification standard, not by taste. Flag this
trade-off for the operator: **the pane should say, next to STORE, which
engine it just froze**, so "why doesn't this match what I was looking at"
never has to be debugged live. MTW storage is a named, later lane (needs
the L5 frequency-vector amendment `MtwBlock`'s comment already anticipates)
— not blocked by anything in this lane.

### C2. Button scope: one global STORE, or one per pane

**Recommended: one global, context-sensitive button**, enabled on RTA and
TRANSFER, disabled on SPL (a scalar/metric pane with no per-bin curve to
freeze) and XOVER (a *consumer* of stored traces, not a producer). On click,
branch on `currentPaneView()` (`MainComponent.h:96`): RTA builds a
magnitude-only `Trace` from `spectrumDb`; TRANSFER builds one from
`transfer` (refusing with a readout message if `!hasReference`, the same
"one-off action fails at a label, never a dialog" convention
`exportReportClicked` already uses — `MainComponentSpl.cpp:117-121`).

Argued against: **per-pane buttons** (a `storeRtaButton_` and a
`storeTransferButton_`) were the first instinct, mirroring how RTA/TRANSFER
already have separate view classes. Rejected because an operator is looking
at exactly one pane at a time regardless — a second button never adds a
capability, only a second onClick handler, a second test file, and a second
place a future reader has to check the refusal message stayed in sync. One
button with a `currentPaneView()` branch is the same amount of *logic*,
fewer moving parts, and it exactly matches the human-try row's own framing
("press STORE... in TRANSFER mode" — one verb, one button, a mode).

### C3. Does the stored trace carry delay compensation and calibration
metadata, and does it get OSM's post-hoc adjustable knobs

**Recommended: freeze `appliedDelaySamples`/`effectiveAverages` into
`CaptureMeta` (both already copied straight off `TransferBlock`, A3), and
do *not* add OSM-style adjustable delay/gain/polarity/coherence-ignore
knobs to `Trace` or `TraceLibrary`.** This project already has the layer
OSM folds into `Stored` itself: `VirtualTrace` + `VirtualOp` (`DelayOp`,
`PolarityOp`, `GainOp`, `BiquadOp`, `VirtualTrace.h:42-68`) is exactly
"a stored trace plus an adjustable processing chain, applied at render,
never mutating the source" — built for the crossover/EQ preview lane and
already wired to read a `Trace` (`VirtualTrace::fromTrace`, `VirtualTrace.h:112`).
Adding a second, parallel adjustable-knobs mechanism onto `Trace` itself
would duplicate that architecture for no new capability. For calibration:
`CaptureMeta::calibrationOffsetDb`/`calibrationUnit` default to
`0.0f`/`LevelUnit::DbFs` (never invented) for a STORE press outside an
active SPL calibration flow, since RTA/TRANSFER are dBFS-native and no
general "current calibration offset" exists for those panes today (A3.3) —
inventing one would be exactly the silent-unit-mismatch `Trace.h:17-19`'s
own comment warns against.

### C4. Naming, grouping, cap/eviction, keyboard shortcut

- **Naming**: `"<PANE> @ HH:MM:SS"` (e.g. `TRANSFER @ 14:32:07`), the same
  shape OSM's `autoName` uses (source-context prefix + clock time),
  renameable afterward through the already-shipped
  `TraceLibrary::rename` (`TraceLibrary.h:70`). One-second, not
  minute, resolution: OSM's UI has one measurement in flight per source at a
  time; this app's human-try row explicitly presses STORE **twice**, and two
  presses inside the same minute must not look identical in the library
  list.
- **Grouping**: default group = the same pane-name prefix, so consecutive
  STOREs in one pane land in one group and pick up distinct shades
  automatically (`TraceLibrary::nextFreeShadeIndex`, `TraceLibrary.cpp:43-58`).
  Regrouping is already a shipped `setGroup` call.
- **Cap/eviction**: **none for v1.** `TraceLibrary` already has no cap
  (`entries_`/`traces_` are plain `std::vector`s), and `StoredTraceLayer`'s
  own header comment already treats "dozens after an afternoon of tuning"
  as the expected, cheap case (`StoredTraceLayer.h:27`). Adding an eviction
  policy now solves a problem nobody has hit, and the wrong one (evicting a
  trace an operator is relying on mid-show) is worse than an unbounded
  vector of small structs.
- **Keyboard shortcut: none for v1.** Grepping `app/src` for keyboard
  handling finds exactly one binding in the whole app, F1
  (`app/src/Main.cpp:45-47`, presumed help/about) — every other action
  button (LOCATE, APPLY, CAL START/END, EXPORT REPORT, SAVE, OPEN) is
  mouse-only. Giving STORE alone a shortcut would be a new UI precedent this
  research did not find a reason to set.

**These four are lower-stakes and owner-overridable** — see the report for
the explicit decision list.

---

## Corrections to the brief / plan review

- **The plan review's "`TraceLibrary::add` has zero production callers" no
  longer holds literally at current `origin/main`.** PR #43 (Save/Open,
  merged after the review's `e2c67a1` verification point) added exactly one
  production caller: session **Open**, replaying previously-saved traces
  (`MainComponentSession.cpp:187`). It is still true in the sense that
  matters — nothing freezes a *live* measurement — but the plan review's own
  sentence, read literally today, is stale. This research states the
  narrower, still-true claim in A3.5.
- **The XOVER pane (PR #45) is further along than the plan review's context
  implied.** It is not `TraceLibrary::entries()[0]/[1]` any more — three
  fix rounds (PR #45 rounds 1–3, `app/src/view/CrossoverPaneView.h:10-28`)
  replaced that with a real picker: two trace combo boxes restricted to
  `visible && has(Field::Phase)` entries, plus explicit topology and
  inversion combos, refusing an incomplete or duplicate pick
  (`CrossoverPaneView.h:90-136`). This makes STORE's job *simpler*, not
  harder: XOVER already handles "more than two eligible traces" as a real
  menu, so STORE does not need to reserve any special "first two" ordering
  or coordinate with XOVER at all — it only needs to produce traces with a
  non-empty phase field, which C1's fixed-FFT choice already guarantees
  whenever `hasReference` is true.
- Every other claim in the plan review's evidence table (Save/Open,
  L7-EQ-UI, L7-ALIGN-UI, any RT60 UI, the "two producer lanes are missing"
  conclusion) was re-checked against `4fc6b6e` and still holds.
