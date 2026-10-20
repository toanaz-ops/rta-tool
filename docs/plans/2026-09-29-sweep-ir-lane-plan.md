# SWEEP lane (Sweep→IR producer) — station-3 plan

*2026-09-29. Follows `docs/research/2026-09-29-sweep-ir-app.md`. This is hot
lane H4 of plan review #2. It builds after H1 (which brings `MainComponent.h`
to ≤ 360 lines), H2 and H3. Every task names its production caller and its
operator path (CLAUDE.md rule 5).*

*Owner defaults in this plan adopted 2026-09-29 under the owner's "execute
autonomously" instruction (plan review #2, docs/plans/MASTER-EXECUTION-PLAN.md).
Any row can still be overridden before its task is built.*

## The design, in six steps

1. The operator presses **SWEEP**, on its own rail row under LOCATE.
2. The click passes its guards (see "Sequencing" below).
3. It sets up the sweep:
   - `setSource(gen::Sweep)`;
   - `soloOutput(output, 0)`;
   - `armLocateCapture(0, len)`.
4. On the first tick at least 100 ms later: `armSource()`.
5. When the capture fills:
   - `disarmSource()`;
   - the capture goes to `SweepIrWorker`, which runs on a `std::thread`.
6. The worker:
   - deconvolves both channels;
   - sets t = 0 to the reference peak;
   - normalises to the loop;
   - windows N bins and computes the spectrum.
7. The 2 Hz timer polls for the result, then:
   - `library_.add(trace, "SWEEP @ HH:MM:SS", "SWEEP")`;
   - `irStore_.add(traceId, ir)`;
   - the readout shows `IR +D samples (x.x ms) · loop a..b dB`.

## Owner-default decisions (recommended; each can be overridden)

| # | decision | default | why / what the alternative costs |
|---|---|---|---|
| 1 | button | `SWEEP` on its own rail row directly under LOCATE, same shape as the STORE row (`MainComponentLayout.cpp:97-103`); enabled on every pane; hint "log sweep 20 Hz–20 kHz, out 1, needs REF on route 1" | a button inside a pane would hide the producer on SPL/XOVER |
| 2 | sweep | `Sweep::Config{}` (10 s, 20 Hz–20 kHz, 2-oct fade-in), except `levelDbFsPeak = −12` | the only configuration with a measured flatness row (record `:237`); level does not change flatness; −12 matches LOCATE (`MainComponentDelay.cpp:28`) and sits near REW's −12 dBFS rms; a 5 s default would ship an unmeasured configuration |
| 3 | capture length | 1.0 s pre-roll budget + `sweep.durationSec()` + 3.0 s tail (1.5 × an assumed RT60 of 2.0 s); refused after the fact if the actual tail comes up short | record §9 (`:996-1038`). Rooms with RT60 > 2 s need a longer tail; that knob is left for the RT60 UI (H6) |
| 4 | trace | N = nextPow2(⌈0.5·fs⌉ + leadIn); rectangular window from `refPeak − leadIn`; phase relative to the reference arrival; `appliedDelaySamples = 0` | research C3 |
| 5 | level | 0 dB = the loop's in-band mean | same meaning as TRANSFER's H = Y/X |
| 6 | IR storage | `IrStore` held in memory, not persisted, keyed by trace id, capped at 8 with FIFO eviction (~4.6 MB each at 48 kHz) | after Save/Open, a trace comes back without its IR; ALIGN already handles "no IR" (`AlignmentWizard.cpp:282-290` AskingCycle) |
| 7 | naming | `SWEEP @ HH:MM:SS`, group `SWEEP` | follows STORE's pattern |
| 8 | SYNTHETIC filter | ON: `ButterworthDesign::bandPass(40, 16000, fs, 1)` on the measurement copy (`ButterworthDesign.h:41`) | gives the synthetic curve checkable −3 dB edges (the delay's reasoning, `SyntheticImpairment.h:5-11`). OFF would leave T1 with no operator path. Cost: the synthetic TRANSFER picture changes at its edges; re-read every test that runs SYNTHETIC and asserts magnitude, and re-render the snapshots |
| 9 | clipping | warn in the readout, do not refuse | a clipped ESS puts its harmonics at negative time; the linear IR survives (record §3) |
| 10 | route | route 0 only | same as LOCATE and CAL |

## Sequencing and mutual exclusion

The sweep runs as a state machine: `Idle → PreRoll → Playing → Analysing → Idle`.
`busy()` is true whenever the state is not `Idle`.

**Mutual exclusion.**
- LOCATE's and CAL's guards gain `|| sweep_.busy()`
  (`MainComponentDelay.cpp:19`, `MainComponentCalibration.cpp:38,50`).
- SWEEP is given a predicate over `locateWaitingForSettle_ ||
  locateCaptureArmed_ || calibrationCaptureArmed_`.

**SWEEP refuses the click when:**
- that predicate is true;
- `!output().sourceIsQuiescent()`, because `setSource` would fail
  (`OutputEngine.h:55-56`);
- `bus().sampleRate() != output().sampleRate()`.

**Abort paths.** Each one disarms the source and says why in the readout.
- `outputEpoch()` has changed since the arm, i.e. a device or mode switch
  (`OutputEngine.h:68`).
- `captureTimedOut(armedAt, now, captureMs + 5000)` (`CaptureTimeout.h:31`).
  The readout asks "no REF on route 1?".

**Which capture is ours.** Only the SWEEP poll consumes a capture while in
`Playing`, and it tracks its own `lastHandled` pointer
(`MainComponentDelay.cpp:64-68`).

**Why pre-roll works.**
- The arm is picked up at the next drain, and drains run every 10 ms
  (`AnalysisThread.cpp:17,183-186`).
- Waiting at least 100 ms before `armSource()` therefore puts the start of
  the capture ahead of the sweep.
- The 1.0 s budget covers one 2 Hz tick (`MainComponent.cpp:119`); the tail
  check catches a stall.

## Worker thread

- **What it is.** `SweepIrWorker` (JUCE-free, `app/src/measure/`).
- **How a job runs.**
  - `start(shared_ptr<const LocateCapture>, SweepIrRequest)` launches one
    `std::thread`.
  - The thread runs an injected `analyse` function, which defaults to
    `analyseSweepCapture`.
  - It publishes through `AtomicSharedPtr<const SweepIrOutcome>` (the same
    handoff as `AnalysisThread.h:373`).
  - `latest()` is polled from the timer.
- **Shutdown.** The destructor joins, so shutdown waits at most one analysis
  (about 1 s).
- **Where it lives.** It is a member of `MainComponentSweep`, declared after
  the references that class holds (trap T-1).
- **Isolation.** The outcome lives in `SweepIrOutcome.h`, so no message-thread
  file includes `rta/ir/*`.

## SYNTHETIC loopback

**Config.** `SyntheticInput::Config` gains two fields:
- `rta::platform::OutputEngine* loopback`;
- `std::vector<rta::dsp::Biquad::Coeffs> measurementFilter`.

**Constructor.**
- It calls `loopback->prepare(sampleRate, 1)`.
- This is legal because the device is already stopped
  (`MainComponent.cpp:159`) and the synthetic thread has not started yet,
  which meets `prepare`'s precondition (`OutputEngine.h:48-52`).

**`runBody`** (`SyntheticInput.cpp:99-129`):
- It always calls `loopback->render(&out, 1, n)`.
- If `!sourceIsQuiescent()`, then `block_ = rendered`; otherwise it plays
  pink or sine as today.
- The measurement copy becomes noise + filter(delay(`block_`)).

**Wiring.** `setSyntheticMode` passes `&audioIo_.output()`.

## Device-free acceptance tolerance (derived, not inherited)

**Fixture.**
- fs = 48000.
- Reference = [P zeros, sweep at −12 dBFS, tail zeros], with P = 12345.
- Measurement = δ_D ⊛ g ⊛ reference, with D = 37 and g =
  `ButterworthDesign::bandPass(300, 3000, fs, 2)`. Its 300 Hz skirt is the
  record's case with teeth (`:900-905`).
- No noise.

**Quantities.**
- `S_m` = the pipeline's output bins for (reference, measurement).
- `S_r` = the pipeline's output bins for (reference, reference).
- G(k) = `cascadeResponse(g, ω_k)·e^{−iω_k D}` (`BiquadResponse.h:25-26`).

**Where the error comes from.** Deconvolution is linear, so `S_m − G·S_r`
comes only from the N-point window and from float rounding. Using
|DFT_k(x)| ≤ ‖x‖₁:

```
B_win = ‖w·(g_D ⊛ ((1−w)·r̃))‖₁ + ‖(1−w)·(g_D ⊛ (w·r̃))‖₁ + ‖r̃‖₁·‖g_tail‖₁
B_float(k) = ε₃₂ · Σ_s(log₂N_s + 1) · (|G·S_r(k)| + max_j|S_m(j)|)
```

What each symbol means:
- **r̃** is the normalised `deconvolve(reference, inverse)`.
- **w** is the INTENDED window, [refPeak − leadIn, +N), computed by the test
  from this spec.
  - It is never read back from the pipeline. If it were, a window that moved
    would move its own bound.
- **g_D** is truncated at M samples. `‖g_tail‖₁` is evaluated in double to
  M' ≫ M and checked against ρ^M/(1−ρ), where ρ = `maxPoleRadius()`
  (`Biquad.h:69`).
- **The sum in `B_float` runs over four float transforms:** three of 2^21
  inside `deconvolve` and one of N.
  - The shape is the two-term form from `memory/float32-fft-precision.md`.
  - Its constant, (log₂N+1)·ε, is that file's 1.3e-6 at 2^10.

**What the test asserts.**
- For every bin in [`validBandLowHz`, `validBandHighHz`] (`Sweep.h:139-148`):
  `|S_m − G·S_r| ≤ B_win + B_float(k)`.
- In dB, that bound reads 20·log10(1 + ratio); in phase, asin(min(1, ratio)).

**What the builder reports.** Print the worst residual-to-bound ratio and the
worst bound in dB, and put both in the PR body. If the bound is looser than
0.1 dB anywhere in band, say so; do not tighten it
(`memory/a-tolerance-inherited-from-a-plan-is-that-plans-fixture.md`).

## Station-3 task table

| task | files | called from | operator path | acceptance (mutant that must go RED) |
|---|---|---|---|---|
| **T0** — gate on H1; move `channelRolesSummary` out of `MainComponentStore.cpp:41`'s anonymous namespace into `CaptureConverter` | `trace/CaptureConverter.h/.cpp`, `MainComponentStore.cpp` | `storeClicked` (`MainComponentStore.cpp:115`) and T6 | STORE (existing) | a unit test fixes the summary for a two-role config, and `test_main_component_store` stays green; swapping the role labels goes RED. SWEEP and STORE must spell the string identically, or ALIGN refuses with `ReferenceMismatch` (`AlignmentWizard.cpp:218-220`) |
| **T1** — measurement-channel filter | `measure/SyntheticInput.h/.cpp` | `SyntheticInput::runBody`; default set in `setSyntheticMode` (decision 8) | SYNTHETIC toggle | an impulse through the filter and delay equals the `BiquadCascade` impulse response shifted by D, bit-exact (same float operations), and the reference stays identical to the input; applying the filter to the reference too goes RED |
| **T2** — `OutputEngine` loopback | `measure/SyntheticInput.h/.cpp`, `MainComponent.cpp` (`setSyntheticMode`) | `setSyntheticMode` | SYNTHETIC → LOCATE or SWEEP | `tests_juce/test_synthetic_loopback.cpp`: with an armed `Oscillator`, the bus reference equals the engine output bit-exact; with the engine quiescent, pink behaves as today. MainComponent test: SYNTHETIC + LOCATE reaches a 4-sample suggestion (the closed-form synthetic delay, `MainComponent.cpp:174`). Deleting the `render` call makes LOCATE time out, RED |
| **T3** — `analyseSweepCapture` (pure function) | `measure/SweepIrPipeline.h/.cpp`, `measure/SweepIrOutcome.h` | T4's default `analyse` | via T6 | `test_sweep_ir_pipeline.cpp`: (a) pure delay 37 → IR peak − `originIndex` == 37 exactly, positive sign; skipping the re-reference goes RED. (b) the derived tolerance above, on the shipped config; starting the window at `refPeak` with no lead-in goes RED on the skirt (record `:902`). (c) tail at `required−1` → `TailTooShort`, at `required` → accepted; dropping the check goes RED. (d) inverted reference → `ReferenceNotFound`. (e) arrival beyond N − leadIn − ⌈0.1·fs⌉ → refused. (f) the gain applied equals `inBandNormalisation(dRef, f_lo, f_hi)` |
| **T4** — `SweepIrWorker` | `measure/SweepIrWorker.h/.cpp` | `MainComponentSweep::poll` | SWEEP | inject an `analyse` that blocks on a latch: `start()` returns and `latest() == nullptr`; after release, the result arrives within 2 s; the destructor joins a running job. Making `start()` synchronous goes RED (the latch wait times out; no deadlock) |
| **T5** — `traceFromSweepIr` + `IrStore` | `trace/CaptureConverter.h/.cpp`, `trace/IrStore.h` | `MainComponentSweep::poll` | SWEEP | N rule gives 44100→32768, 48000→32768, 96000→65536; the trace has phase and no coherence (never zero-filled), `appliedDelaySamples` 0, channel roles from T0; a 9th `IrStore::add` evicts the first. Zero-filling coherence goes RED; an N one power of two off goes RED |
| **T6** — `MainComponentSweep` + wiring | new `MainComponentSweep.h/.cpp`; `MainComponent.h/.cpp`, `MainComponentLayout.cpp`, `MainComponentDelay.cpp:19`, `MainComponentCalibration.cpp:38,50`, `MainComponentTestAccess.h`, CMake lists | the ctor wires `onClick`; `timerCallback` calls `sweep_.poll()` | **the SWEEP button** | `tests_juce/test_main_component_sweep.cpp`, SYNTHETIC, 2 s sweep and 0.5 s tail set via TestAccess: (a) one press → library +1 `SWEEP @` in group SWEEP, readout "+4 samples"; (b) SWEEP during LOCATE, and LOCATE/CAL during SWEEP → refused, library unchanged; (c) SYNTHETIC off mid-sweep → aborted, source disarmed, `busy()` false; (d) no REF role → timeout readout; (e) the rail-layout test shows no overlap. Removing `\|\| sweep_.busy()` → (b) RED; removing the epoch check → (c) RED |
| **T7** — XOVER axis guard | `view/CrossoverPaneView.h/.cpp` | `refreshFromLibrary` (`CrossoverPaneView.cpp:143`) | XOVER pickers after SWEEP + STORE | a 4096-point STORE trace picked against a 32768-point SWEEP trace → a refusal naming both sizes, no surface; two SWEEP traces → drawn. Deleting the guard goes RED |
| **T8** — close the wave | — | — | — | `tools/orphan_check.py` exits 0 over the lane range; every touched file stays within the budget below |
| **T9** — human-try | — | — | see "Human-try row" | a human sees it; `main-live.png` is re-rendered for the report |

`IrStore::add` and `size` (used in the readout) are live in this lane. The
lookup for ALIGN is added by H5 (ALIGN-UI) and is not built here, so that it
does not become an orphan.

## File-length budget (hard cap 400)

| file | now | after |
|---|---|---|
| `MainComponent.h` | 400 (→ ≤ 360 by H1) | +6 |
| `MainComponent.cpp` | 306 | +10 |
| `MainComponentLayout.cpp` | 125 | +8 |
| `MainComponentDelay.cpp` | 122 | +1 |
| `MainComponentCalibration.cpp` | 266 | +2 |
| `MainComponentStore.cpp` | 164 | −14 |
| `MainComponentTestAccess.h` | 122 | +8 |
| `MainComponentSweep.h/.cpp` | new | ~110 / ~230 |
| `SweepIrPipeline.h/.cpp` | new | ~90 / ~220 |
| `SweepIrWorker.h/.cpp` | new | ~70 / ~70 |
| `SweepIrOutcome.h`, `IrStore.h` | new | ~50, ~60 |
| `CaptureConverter.h/.cpp` | 78 / 146 | +25 / +70 |
| `SyntheticInput.h/.cpp` | 123 / 139 | +15 / +30 |
| `CrossoverPaneView.h/.cpp` | 153 / 299 | +3 / +15 |
| `AnalysisThread.h` | 398 | **0 — do not touch** |

## Hot files (serialise with the other H-lanes)

- `MainComponent.h`, `MainComponent.cpp`, `MainComponentLayout.cpp`,
  `MainComponentDelay.cpp`, `MainComponentCalibration.cpp`,
  `MainComponentStore.cpp`, `MainComponentTestAccess.h`.
- `app/cmake/rtatool_sources.cmake` and `rtatool_snapshot_sources.cmake`. The
  snapshot target compiles the `MainComponent*` sources, so new files go in
  both lists.
- `app/tests/cmake/impl_test_sources.cmake`, `app/tests_juce/CMakeLists.txt`
  and `main_component_tests.cmake`.
- `tools/snapshot.cpp`: no edit is expected; re-render it.

## Human-try row (real hardware)

1. Wire the interface: out 1 → a Y-split → the speaker amp and in 2. Route in 2
   as REF and the microphone on in 1 as MEAS, both on route 1.
2. Press SWEEP. The operator should hear 10 s of sweep with no click at either
   end.
3. The readout should show D ≈ distance/343 · fs samples.
4. Move the microphone 1 m back and sweep again. D should grow by about 140
   samples at 48 kHz.
5. Sweep the sub and then the main (re-patch between them), open XOVER, and
   pick both. The sum draws.
6. Pick a STORE trace against a SWEEP trace. The pane refuses and names both
   sizes.

SYNTHETIC cannot show latency, drift or real noise. That is why this row
exists.

## Not built here

- A taper or draggable gate (G25).
- A clock-drift threshold. It is unmeasured, and a single device shares one
  clock.
- Persisting IRs.
- SWEEP↔STORE interop.
- Repetitions or averaging.
- A level-check pre-pass.
- An acoustic timing reference.
- THD from the harmonic packets (G3).
- The RT60 UI (H6) and ALIGN's IR lookup (H5).
- The XOVER `appliedDelaySamples` defect (research correction 4).

## H4 as built — amendments

*Written 2026-10-20 against the merged code (PR #67 `079d1b8`, #68 `50defce`,
#69 `65ee5f6`), after it existed (CLAUDE.md, "A record amendment is written
against the shipped code"). Where this section and the plan above disagree,
this section is what shipped. Line numbers are at `65ee5f6`.*

| # | plan said | shipped | evidence |
|---|---|---|---|
| A1 | decision 8: `bandPass(40, 16000, fs, 1)` | **N = 2** (two SOS sections) | `MainComponentSynthetic.cpp:59`, comment above it |
| A2 | T2: "SYNTHETIC + LOCATE reaches a 4-sample suggestion" | LOCATE suggests **5 samples** (4 + the band-pass's own delay; the PHAT estimate reads 4.76) | `test_synthetic_loopback.cpp`, "SYNTHETIC + LOCATE ..." |
| A3 | T3 (b) bound: tail term once, four float transforms | tail term **twice**, plus **one extra float stage** | `test_sweep_ir_tolerance.cpp:25-47` |
| A4 | statuses `TailTooShort`, `ReferenceNotFound`, `ArrivalOutOfWindow` | also `CaptureTooShort`, `MeasurementNotFound`, `Failed`; and a 20x peak/rms gate on the loop | `SweepIrOutcome.h:33-41`, `SweepIrPipeline.cpp:29,97,118` |
| A5 | T4: `start(shared_ptr<const LocateCapture>, ...)` | `start(SweepCapture, ...)` **by value** | `SweepIrWorker.h:49` (declaration; its doc comment starts at `:43`; the cited `:46` fell inside that comment), `MainComponentSweep.cpp:170` (the `worker_.start(` call; the cited `:157` is stale) |
| A6 | abort on `outputEpoch()` change | also aborts when the SYNTHETIC flag differs from the one at arm time | `MainComponentSweep.cpp:124` |
| A7 | trace metadata unspecified | device name and channel roles captured **at click** | `MainComponentSweep.cpp:100-101,171` |
| A8 | T7: refuse unequal axes (sizes named) | also refuses equal `fftSize` at different sample rates | `CrossoverPaneView.cpp:186-191` |
| A9 | readout `IR +D samples (x.x ms) · loop a..b dB` | `... · IRs N/8`, with ` (oldest dropped)` when this one evicted | `MainComponentSweep.cpp:186-194` |

**A1 — N = 1 throws (CONFIRMED by reading the code, 2026-10-20).** A
band-pass built from one Butterworth pole is two poles; `pairIntoSections`
requires every pole to be part of a complex pair and throws
`std::logic_error` otherwise (`ButterworthDesign.cpp:181-194`,
`if (!poleSplit.reals.empty() || ...)`). Whether the two poles are real depends
on the band: lowpass-to-bandpass of the prototype pole -1 gives real poles when
the prewarped bandwidth exceeds twice the centre, `bw > 2*w0`. For 40 Hz -
16 kHz at 48 kHz, `wLo = 2fs*tan(pi*40/fs) = 251.3`, `wHi = 2fs*tan(pi*16000/fs)
= 166277`, `bw/w0 = 166026/6464 = 25.7`, so both poles are real and the call
throws. This is read-and-computed, not re-run as an N = 1 call. In the shipped
app it killed `setSyntheticMode` itself, which is what the comment at
`MainComponentSynthetic.cpp:54-58` records.

**A2.** The plan's "4 samples" ignored the filter decision 8 put on the
measurement copy. The expected value in the test is an **external numeric
oracle** (pink noise, 4-sample delay, `butter(2,[40,16000])`, -30 dB noise,
GCC-PHAT with regularisation 1e-10 and parabolic refinement: 4.756 - 4.766),
labelled a regression lock. The first version of that test derived 4.75 as a
mean group delay over 200 Hz - 12 kHz; PHAT in `MainComponentDelay.cpp`
(`PhatOptions` min/max Hz both 0) weighs DC to Nyquist, so that derivation was
wrong and was removed (closeout PR, L1). **Corrected 2026-10-20 (H5 closeout,
C1):** the earlier text here said the same formula over DC..Nyquist "gives
6.26"; that figure is not reproducible. Re-measured: the phase-slope mean group
delay of `butter(2,[40,16000])` at 48 kHz over DC..Nyquist is **0.50 samples**
(scipy `freqz` at 4096 / 16384 / 262144 points: 0.533 / 0.508 / 0.5005; two-point
endpoint slope 0.49999), i.e. 4.50 with the 4-sample delay, which is not the
oracle's 4.76 either. PHAT is a whitened correlation peak, not a mean group
delay, so no closed form is claimed for 4.76; it stays an external numeric
oracle, a regression lock.

**A3.** In `test_sweep_ir_tolerance.cpp`, `h`'s tail beyond M samples enters
both `S_m` and `G.S_r`, so it counts twice, `2*||r||_1*||g_tail||_1`; the plan
wrote it once. The float sum gains one stage for rounding the filtered
measurement to float before it enters the pipeline. **Measured** (PR #68 body):
worst residual/bound **0.7403 at 324 Hz** (the 300 Hz skirt), 0 violations over
81 - 19726 Hz; worst bound **1.67 dB at 19726 Hz**, where the test filter is
about -80 dB so the absolute bound is large against the target; **0.007 dB
inside 300 - 3000 Hz**. Looser than 0.1 dB out of band; reported, not
tightened.

**A4.** The 20x gate: the loop's deconvolution must have a positive peak at
least `kMinPeakToRms = 20` times its own rms. Derivation
(`SweepIrPipeline.cpp:24-29`): Gaussian noise over 10^6 samples peaks near
`sqrt(2 ln n)` = 5.3 rms, so 20 leaves a factor 3.8. The matched filter's gain
is about `sqrt(Ns)` = 693 for a 2 s loop; a clean 2 s loop measured **439**
(PR #68 body), 22 times the gate. `CaptureTooShort` is a capture shorter than
the sweep or channels of unequal length; `MeasurementNotFound` is a measurement
that deconvolves to exact silence; `Failed` carries an exception's text
(`SweepIrWorker.cpp`; since the closeout PR a non-`std::exception` also becomes
`Failed("unknown exception")` instead of `std::terminate`).

**A5.** The worker owns its input: `SweepCapture` (two `std::vector<float>`,
JUCE-free) is passed by value and moved into the thread, so the caller can drop
`lastHandled_` immediately (`MainComponentSweep.cpp:159`) and nothing it does
afterwards can reach the job. `LocateCapture` lives in `AnalysisThread.h`, which
includes JUCE, and the worker header must not.

**A6.** Entering SYNTHETIC bumps `outputEpoch()` (the `SyntheticInput`
constructor calls `prepare()`), but leaving it does not: destroying the
synthetic writer prepares nothing. A sweep armed in SYNTHETIC and interrupted by
toggling it off would therefore pass the epoch check, so the abort also
compares `host_.synthetic()` with the value captured at arm time.

**A7.** The capture belongs to the device and role assignment at click time,
even if SYNTHETIC or the device changes while the worker runs; ALIGN compares
the role string.

**A8.** Two traces with the same `fftSize` but different sample rates do not
share a frequency axis either; the pane names both rates.

**A9.** Format: `IR +D samples (+x.x ms) · loop a..b dB · IRs N/8`, with
` (oldest dropped)` appended at 8/8 when the add evicted. `IrStore::size` and
`kCapacity` are therefore live in production through this readout, which is what
removed the last orphan in the lane.

### Lane LOWs recorded at close

L1, L2, L4, L6 were fixed in the closeout PR; L3 goes to H5; L5, L7, L8 are
the amendments above; L9 is deferred with LOCATE; L10 is cosmetic. The table
with dispositions is in `docs/HUMAN-QA-QUEUE.md`, "H4 SWEEP lane LOWs".
