# L7-DELAY — auto-delay and delay suggestions (lane L7, sub-lane L7-DELAY)

> **For agentic workers:** REQUIRED SUB-SKILL `superpowers:test-driven-development` and `superpowers:executing-plans`. Steps are checkboxes; the failing test is written and *seen to fail* before the code. Station 5 (adversarial verify, a reviewer with **no Write tools**) is not optional.

*2026-09-07, lane L7, sub-lane L7-DELAY, station 3. Written from the decision record `docs/dsp/2026-09-06-l7-auto-delay.md` after reading the real files it cites — `core/include/rta/dsp/{DelayFinder,DualFftEngine,TransferEstimator}.h`, `core/src/dsp/DelayFinder.cpp`, `core/tests/test_delay_finder.cpp`, `core/include/rta/gen/Noise.h`, `platform/types/include/rta/platform/OutputEngine.h`, `platform/include/rta/platform/AudioIo.h`, `app/src/measure/{Analyser,AnalysisThread,OutputPolicy,CaptureSequencer}.h`, `core/tests/CMakeLists.txt`, `app/tests/CMakeLists.txt` — not the record's description of them. Worktree `.claude\worktrees\continue-pending-work-f8aa84`, HEAD `1ad48da`. Auto-delay is a **policy layer over the existing `findDelayPhat`**, not a new correlator (record §0, §2). Every core acceptance number below is a closed-form identity or a fixed-seed statistical bound; L7-DELAY needs **no new golden vector** (record §12).*

## 0. What L7-DELAY is, and what already exists (verified)

An operator needs the number to type into a delay line. `rta::dsp::findDelayPhat` (`DelayFinder.h:97`, impl `DelayFinder.cpp:42-181`) already answers it: GCC-PHAT, linear zero-pad to `m = bit_ceil(max(2L,4))` (`DelayFinder.cpp:51`), relative regularisation floor `1e-10·max|G|` (`:97`), argmax over `|r|` (`:117-125`), signed-triple parabolic refinement (`:139-178`), normalised `peak` and an `inverted` flag. **Nine fixtures** pin it (`test_delay_finder.cpp`: integer delay, negative delay, inverted cable, sub-sample, noise, band limit, refuses-args, silence, relative-reg). They are the regression lock for the refactor.

Verified present and reused unchanged:
- `DualFftEngine::crossPsd()` → `std::span<const std::complex<double>>` averaged `Sxy` (`DualFftEngine.h:81`); `numBins()`, `binWidthHz()`, `config().fftSize` (default 4096). `referenceDelaySamples` is **construction-time** (`DualFftEngine.h:101-105`: `referenceSkip_`/`measurementSkip_` count down once) — an Apply is an engine rebuild (record §1.6, §8).
- `TransferSnapshot::coherence` is `std::optional<std::vector<float>>`, **nullopt below the gate** (`TransferEstimator.h:68`); `makeSnapshot` is the ONE writer, enforced by `coherence_gate_is_not_bypassed` (`core/tests/CMakeLists.txt:107`). `magnitudeSquaredCoherence` is the γ² function (`TransferEstimator.h:55`).
- **Wave-1 `OutputEngine` is BUILT** (`platform/types`): `setSource(SourceVariant&&)`, `armSource()`, `disarmSource()`, `sourceIsQuiescent()`, `routeOutput(ch,active)`, `renderedSamples()`, `outputEpoch()` (`OutputEngine.h`). `AudioIo::output()` returns `OutputEngine&` (`AudioIo.h:120`). `OutputPolicy.h` (`soloOutput`/`muteAll`/`applyToggle`, JUCE-free) is BUILT.
- `gen::PinkNoise(Pcg32, level)` (`Noise.h:183`) — PCG32, aperiodic over any capture; Locate's default excitation. `gen::Mls` is periodic (`2^n−1`) and **must not** excite Locate (record §1.5).
- `Analyser::pushPair`, `Analyser::transferSnapshot()` → `optional<TransferSnapshot>`, `config_.referenceDelaySamples` (`Analyser.h`). `AnalysisThread::drainPaired` feeds the per-route Analysers (`AnalysisThread.h:121`); `AnalysisThread` is `juce::Thread` (JUCE-owning).
- **KNOWN GAP (verifier-confirmed, still true):** grep of `app/` for `findDelayPhat`/`DelayEstimate` returns nothing; there is **no raw-capture path** — `drainPaired` hands hops to `Analyser::pushPair`, nothing accumulates a multi-second span of both channels. Locate needs one BUILT (Task F).

## Global constraints

- SPDX header `// SPDX-License-Identifier: AGPL-3.0-or-later` on every new file.
- **`core/` never includes JUCE/Qt/a device API** (guard `core_has_no_framework_deps`). The new core files are pure DSP: spans and accumulators in, numbers out. **`ResidualDelayTracker` READS `TransferSnapshot::coherence`; it never writes it**, so `coherence_gate_is_not_bypassed` stays untouched (record §11).
- **The JUCE-free half of `app/` must stay JUCE-free** (guard `measure_has_no_framework_deps`, `app/tests/CMakeLists.txt:108`): the raw accumulator and the Locate state machine are added to its GLOBS list; `AnalysisThread`/`MainComponent` are NOT (they are JUCE, already excluded).
- **Hard cap 400 lines, aim 300**, headers too. Budgets per file below.
- **Never assert a value the implementation produced** (CLAUDE.md verification standard). Acceptance is a closed-form identity, a cited standard, or a fixed-seed statistical bound labelled as a regression lock. No new golden vector (record §12).
- Build dirs **`build-l7delay`** (OFF) / **`build-l7delay-on`** (ON), **Visual Studio generator, never Ninja** (`memory/a-misconfigured-build-goes-99-percent-of-the-way.md`). MSVC `/W4` is the truth: **0 warnings**.

```
cmake -S . -B build-l7delay -G "Visual Studio 18 2026" -A x64
cmake --build build-l7delay --config Release --parallel
ctest --test-dir build-l7delay -C Release --output-on-failure
```

For the ON config append `-DRTA_BUILD_APP=ON` and use `build-l7delay-on`.

## Namespace decision (record §11 asks it explicitly)

**Core policy layer lives in `rta::dsp`; app orchestration lives in `rta::measure`. A new `rta::solve` namespace was considered and rejected.**

Justification: `suggestDelay` and `ResidualDelayTracker` are numbers-in-numbers-out over spans and the engine's accumulator — the same shape as everything else in `rta::dsp`, and they share the correlator's spectral internals with `findDelayPhat`, which the record fixes in `rta::dsp` with its signature unchanged. A `rta::solve` namespace would split one correlator across two namespaces for no boundary benefit (the shared spectral half stays a private detail either way). The genuine *policy/solver* work — arm/capture/apply sequencing, refusals an operator sees, driving `OutputEngine` — is `app/` orchestration and belongs in `rta::measure`, beside `CaptureSequencer` and `OutputPolicy`. The delay lane therefore splits along the existing core/app seam, not along a new namespace. L7-EQ/L7-ALIGN inherit the same rule: estimator math → `rta::dsp`, sequencing → `rta::measure`.

## The core API this lane builds (record §11; names are this plan's)

```cpp
// core/include/rta/dsp/DelayPolicy.h    (rta::core, namespace rta::dsp)
namespace rta::dsp {

struct DelayCandidate {
    std::ptrdiff_t delaySamples = 0;   // integer lag inside the window
    double subSample = 0.0;            // parabolic refinement, |.| <= 0.5
    double height = 0.0;               // SIGNED normalised |r| at this peak
    bool inverted = false;             // height < 0
};

enum class DelayVerdict { Accepted, BelowFloor, WindowEmpty };  // a refusal is named, never a silent zero (record §4)

struct DelaySuggestion {
    DelayEstimate best{};              // reuse the existing struct verbatim
    double trust = 0.0;               // peak / f_band, in [0,1] by construction (record §4)
    double nullFloor = 0.0;           // sqrt(ln m / M_in) (record §4)
    double ambiguity = 0.0;           // |second|/|best|, in [0,1], DISPLAY-ONLY (record §3)
    std::vector<DelayCandidate> candidates;   // <= policy.maxCandidates, ranked by |height|
    DelayVerdict verdict = DelayVerdict::WindowEmpty;
};

struct DelayPolicy {
    std::ptrdiff_t minLag = std::numeric_limits<std::ptrdiff_t>::min();  // full linear range by default (record §3)
    std::ptrdiff_t maxLag = std::numeric_limits<std::ptrdiff_t>::max();
    int maxCandidates = 4;            // K (record §3)
    double acceptMultiple = 4.0;      // c; accept iff trust >= c*nullFloor (record §4, proposed 4)
};

[[nodiscard]] DelaySuggestion suggestDelay(std::span<const float> reference,
                                           std::span<const float> measurement,
                                           const PhatOptions& options,
                                           const DelayPolicy& policy);
}  // namespace rta::dsp
```

```cpp
// core/include/rta/dsp/ResidualTracker.h   (rta::core, namespace rta::dsp)
namespace rta::dsp {

struct DelayTrack {
    std::ptrdiff_t residualSamples = 0;  // residual AFTER config.referenceDelaySamples (record §5)
    double subSample = 0.0;
    double peak = 0.0;                    // normalised, bounded by mean gated coherence (record §5)
    double meanCoherence = 0.0;          // mean gated gamma^2 over the band
    bool inverted = false;
};

/// Coherence-weighted PHAT on the engine's averaged cross-spectrum. Owns one
/// RealFft(fftSize) and its scratch, so update() never allocates (record §11).
class ResidualDelayTracker {
public:
    explicit ResidualDelayTracker(std::size_t fftSize);
    /// nullopt whenever snapshot.coherence is nullopt (record §5, §8b). Reads
    /// crossPsd() + coherence; never writes referenceDelaySamples or coherence.
    [[nodiscard]] std::optional<DelayTrack> update(const DualFftEngine& engine,
                                                   const TransferSnapshot& snapshot);
};
}  // namespace rta::dsp
```

`total = config.referenceDelaySamples + residual` is **app-layer presentation** (record §5, §11.4), not the tracker's job.

## Reconciliations made while planning (take to the orchestrator; do not silently re-decide in code)

- **DEL-R1 — the shared spectral half is a private detail header, not a public API.** Record §11 says "a spectral half ... with two front doors". *Decision:* factor whiten-and-weight → IFFT → windowed pick of ≤K local maxima → parabolic refine into `core/src/dsp/PhatCorrelation.h` (namespace `rta::dsp::detail`, header-only, ≤ 200), consumed by `DelayFinder.cpp`, `DelayPolicy.cpp` and `ResidualTracker.cpp`. It is NOT installed in `core/include`. Flagged because it adds one file the record's §11 list did not name. The refactor-lock test (Task A) proves `findDelayPhat` is bit-for-bit unchanged.
- **DEL-R2 — the app raw-capture accumulator and the Locate state machine are two files, both JUCE-free.** Record §11 puts the accumulator "on the analysis thread". *Decision:* the accumulator is a pure `rta::measure::RawCaptureBuffer` (two pre-sized `std::vector<float>`, fill from hops, JUCE-free, tested OFF), and the arm→wait→capture→disarm→suggest sequence is a pure `rta::measure::DelayLocator` over `rta::platform::OutputEngine&` (JUCE-free like `OutputPolicy`/`CaptureSequencer`, tested OFF). Only `AnalysisThread` feeding the buffer inside `drainPaired`, and `MainComponent`'s button + Apply, are JUCE (tested ON, `app/tests_juce`). This keeps the record's `render §11 app` list intact while respecting the guard split.
- **DEL-R3 — `c` is a `DelayPolicy` field defaulted to 4, and the survey (Task E) confirms or moves it.** Record §4 proposes 4 and §13 leaves the exact value to the survey. *Decision:* the shipped default is 4; the survey's two acceptance checks either confirm it or the record is amended in place and this default follows. The survey is a `tools/probe_*.py` measurement, **not** a ctest gate — the committed regression is the fixed-seed null-floor test (Task B), per record §12.4.
- **DEL-R4 — open questions §14.1-3 (default window, Locate solo default, tracker on-by-default) are folded where answered, else left as this plan's open list.** §14.2 (solo default) shares its answer with L7-OUT §13.2 (owner ruling already gave StrictSolo for a sequence); Locate uses `soloOutput` before capture (Task F). §14.1 and §14.3 need a human — listed at the end, planned as one constant / one bool each, no code fork.

---

## Task A — `suggestDelay`: window argmax, ranked candidates, ambiguity, and the refactor lock (core, OFF; record §2, §3, §12.1-3)

The spectral-half refactor plus the one-shot's peak selection. Smallest self-contained core deliverable and every later core task depends on the detail header, so it goes first.

**Files.** Create `core/src/dsp/PhatCorrelation.h` (detail, ≤ 200, DEL-R1), `core/include/rta/dsp/DelayPolicy.h` (≤ 130), `core/src/dsp/DelayPolicy.cpp` (≤ 220), `core/tests/test_delay_policy.cpp` (≤ 300); modify `core/src/dsp/DelayFinder.cpp` (re-express `findDelayPhat` on `detail::` — same result, fewer lines), `core/tests/CMakeLists.txt` (add `test_delay_policy.cpp` to `rta_core_tests`, line 13 group). `DelayFinder.h` is **unchanged**.

**RED first.** `test_delay_policy.cpp` opens with `#include "rta/dsp/DelayPolicy.h"`; the build fails at the include. Paste it. Then, once the header and the refactor exist, the refactor lock is the cleanest first assertion.

| # | case (record §12) | closed-form acceptance |
|---|---|---|
| **A1 (first)** | refactor lock (§12.1) | on each of the 9 `test_delay_finder.cpp` fixtures reproduced here, `suggestDelay(x,y,opts,{}).best` equals `findDelayPhat(x,y,opts)` field-for-field — `delaySamples`, `subSample`, `peak`, `inverted` **bit-for-bit** (`==`, not `Approx`). And the whole existing `test_delay_finder.cpp` stays green unchanged |
| A2 | two-arrival, ghost is causal-inverted (§3, §12.2) | `y=x[n−300]+a·x[n−420]`, white `x`, `L=2^14`: for `a∈{0.3,0.6,0.9}` `best.delaySamples==300`; `candidates` contains `420` (`inverted==false`) and `180` (`inverted==true`); at `a=0.3`, `|height@180|/|height@300|` is within `0.05` of `a/2` (first-order identity of §3, tight only for small `a`) |
| A3 | argmax flips past a=1 (§12.2) | same fixture, `a=1.5`: `best.delaySamples==420`, and `300` is the second candidate — asserted as **what argmax does**, labelled the case `soloOutput` removes (record §3), NOT as correctness |
| A4 | window is honoured, inversion survives (§12.3) | `a=1.5` with `policy.maxLag=350` → `best==300` (the louder later arrival is outside the window); `[minLag,maxLag]=[100,200]` → `best==180` with `inverted==true` (a wrong window is *visible*, not silently plausible); `[1000,2000]` where no local maximum clears the null floor → `verdict==WindowEmpty` |
| A5 | ambiguity is display-only, in range | for every A2 row `ambiguity==|second|/|best|` and `0<=ambiguity<=1`; no code path gates on it (grep the impl for a branch on `ambiguity` → none) |

- [ ] **Accept:** OFF ctest `base_core+N` (A1–A5 add one `TEST_CASE` group; N read from ctest, not predicted). Zero `/W4`. `test_delay_finder`'s 9 fixtures still green.
- [ ] **Mutation:** in `PhatCorrelation.h` widen the pick to ignore `[minLag,maxLag]` → A4 fails; revert. **Mutation 2:** take `|.|` of each of the three parabola samples independently instead of the signed triple → A1's `subSample` diverges on the inverted-cable fixture; revert.
- [ ] **Commit:** `git add core/src/dsp/PhatCorrelation.h core/include/rta/dsp/DelayPolicy.h core/src/dsp/DelayPolicy.cpp core/src/dsp/DelayFinder.cpp core/tests/test_delay_policy.cpp core/tests/CMakeLists.txt` → `feat(core): suggestDelay — windowed argmax, ranked candidates, ambiguity; findDelayPhat re-expressed on the shared spectral half, bit-for-bit`

## Task B — trust, the derived null floor, the accept gate, and the band-limit justification (core, OFF; record §4, §7, §12.4-5, §12.10)

Completes `DelaySuggestion`: `trust = peak/f_band`, `nullFloor = √(ln m / M_in)`, and the `Accepted`/`BelowFloor` verdict. `f_band = (maxHz−minHz)/(fs/2)` with the unset-sentinel handling `findDelayPhat` already uses (`DelayFinder.cpp:70-71`).

**Files.** Modify `core/src/dsp/DelayPolicy.cpp` (compute trust/nullFloor/verdict), `core/tests/test_delay_policy.cpp` (add the trust/floor/band cases). No new file.

**RED first.** Add a case asserting `suggestDelay(cleanPair,...).trust > 0.9`; before this task `trust` is still `0.0` from Task A → fails. Paste it.

| # | case (record §12) | closed-form acceptance |
|---|---|---|
| **B1 (first)** | trust=1 on a clean single arrival | `y=x[n−300]`, white `x`, full band: `trust==Approx(1.0).margin(0.05)`, `verdict==Accepted` (`peak≈f_band=1`) |
| B2 | null floor, full-band form (§4, §12.4) | 40 uncorrelated pairs (fixed seeds) at `m∈{2^14,2^15,2^16}`: mean of the max normalised peak within **15%** of `√(2 ln m/m)`, and no sample above **1.4×** it. Labelled a statistical bound, fixed seed |
| B3 | null floor, band-limited form (§4, §12.4) | same 40 pairs, `minHz/maxHz=200/4000`: mean-of-max and worst within the same bounds of `√(ln m / M_in)` with `M_in` the in-band bin count — the derivation's first measurement, labelled |
| B4 | accept above the floor (§12.5) | the shipped `x+2n` (−6 dB) noise fixture: `verdict==Accepted` at `c=4`, `trust>0.3`, `best.delaySamples==300` |
| B5 | refuse below the floor, no NaN (§12.5) | independent noise at −30 dB: `verdict==BelowFloor`; silence (both spans 0): `verdict==BelowFloor`, `std::isfinite(trust)`, never NaN |
| B6 | band limit rescues a narrowband box (§7, §12.10) | measurement through `butter(4)` 60–960 Hz band-pass, `D=300`, no noise: with `minHz/maxHz` set to the box's band `trust>0.75` and `verdict==Accepted`; with full band (no limit) `trust<0.2` — asserted in **both** directions |
| B7 | excess phase does not move the verdict (§7, §12.10) | 2nd-order allpass (1 kHz, Q 0.7) and an LR4-crossover sum on the measurement channel: `best.delaySamples==300` (allpass) / within `±4` (the filters' own in-band group delay, a real latency), `trust>0.75` both |

- [ ] **Accept:** OFF ctest count rises by the B-group; zero `/W4`. B6/B7 filter fixtures generated in-test from `rta::dsp` biquad/Butterworth already in core (no Python at test time).
- [ ] **Mutation:** drop the `/f_band` normalisation (use raw `peak` as trust) → B6's band-limited row reads far below 0.75 and fails; revert. **Mutation 2:** make the gate `trust >= nullFloor` (drop `c`) → B5's −30 dB noise sometimes accepts; revert.
- [ ] **Commit:** `git add core/src/dsp/DelayPolicy.cpp core/tests/test_delay_policy.cpp` → `feat(core): trust = peak/f_band against the derived null floor sqrt(ln m/M_in); accept iff trust >= c*floor, named refusals`

## Task C — `ResidualDelayTracker`: coherence-weighted PHAT on the averaged cross-spectrum (core, OFF; record §5, §8, §12.6-9, §12.11-12)

The live residual tracker. `ψ_k = γ²_k/|Sxy_k|` on `crossPsd()`, one real IFFT of `fftSize`, the same windowed pick as Task A, `±fftSize/4` in practice. Returns `nullopt` when `snapshot.coherence` is nullopt.

**Files.** Create `core/include/rta/dsp/ResidualTracker.h` (≤ 120), `core/src/dsp/ResidualTracker.cpp` (≤ 220), `core/tests/test_residual_tracker.cpp` (≤ 300); modify `core/tests/CMakeLists.txt`. Reuses `detail::` (Task A) for the pick + refine; the forward transform is the engine's own — the tracker only does the weighting IFFT.

**RED first.** `test_residual_tracker.cpp` includes `"rta/dsp/ResidualTracker.h"`; build fails at the include. Paste it. Then the absence contract (cleanest):

| # | case (record §12) | closed-form acceptance |
|---|---|---|
| **C1 (first)** | absence while the gate is closed (§12.6) | a fresh `DualFftEngine(fftSize=4096)`, one frame pushed (`makeSnapshot` → `coherence==nullopt`): `tracker.update(engine,snapshot)==std::nullopt` |
| C2 | closed form after the gate opens (§12.6) | `y=g·x[n−D]` through the real engine, ≥16 frames so the gate opens, noise −6 dB: `residualSamples==D` exactly for `D∈{0,3,37,700}`; `peak <= meanCoherence` at every publish and within `0.02` for `D<=37` (the record §5 identity: the peak is bounded by mean gated coherence, equality when phase is exactly linear) |
| C3 | residual is AFTER referenceDelaySamples (§12.7) | same signal with `config.referenceDelaySamples=D`: `residualSamples==0`; with `D−5`: `residualSamples==5` |
| C4 | the alias bound, asserted as the documented limit (§12.8) | `D=0.51·N`: `residualSamples` is **negative** — asserted so a later reader cannot mistake the documented wrap for a bug (record §5 row 5) |
| C5 | no silent fallback (§8b, §12.9) | mid-run, replace the measurement with independent noise; after the FIFO drains, `coherence` returns to nullopt and `update` returns `std::nullopt` — **never the last good value** (`memory/a-fixed-defect-returns-through-the-silent-fallback.md`) |
| C6 | update() allocates nothing (§12.11) | under a counting allocator, `update` after construction performs zero allocations (the `RealFft` and scratch are ctor-owned) |
| C7 | sub-sample honesty, no tighter claim (§12.12) | fractional offsets 0.1…0.9 by linear interpolation: `residualSamples+subSample` within **0.1** sample at each — the existing margin, and no tighter claim (the `.cpp`'s 0.11-sample divergence note stays documented and untested, per its own reasoning) |

- [ ] **Accept:** OFF ctest count rises by the C-group; zero `/W4`. `coherence_gate_is_not_bypassed` still green (the tracker reads, never writes, coherence — confirm in Task G).
- [ ] **Mutation:** make `update` return the previous `DelayTrack` instead of `nullopt` when coherence is absent → C1 and C5 fail; revert. **Mutation 2:** use flat weight `1/|Sxy|` (drop `γ²`) → C2's `peak<=meanCoherence` identity breaks; revert.
- [ ] **Commit:** `git add core/include/rta/dsp/ResidualTracker.h core/src/dsp/ResidualTracker.cpp core/tests/test_residual_tracker.cpp core/tests/CMakeLists.txt` → `feat(core): ResidualDelayTracker — gamma^2-weighted PHAT on the engine's Sxy, residual after referenceDelaySamples, nullopt when coherence is`

## Task E — the one-mechanism survey that CONFIRMS the derivation (tools, build-time; record §4, §7, §12.5)

Not a ctest — a build-time measurement (main-checkout venv, `numpy`/`scipy`) that confirms the null-floor derivation and fixes `c`. **One mechanism, four axes** (record §4): SNR, `D/L` overlap, band limit, excess-phase order. The pre-run slices are record §4 (SNR table, `D/L` = 0.06/0.25/0.50/0.75) and §7 (allpass, LR4, two band-passes).

**Files.** Create `tools/probe_delay_trust.py` (**argparse-guarded** per `memory/a-gen-script-runs-the-moment-you-invoke-it.md` — `--help` must NOT run the pipeline; no side effects, writes nothing), and `tools/probe_delay_nullfloor.py` likewise. Both print a decision→evidence table.

**Acceptance (the two checks record §4 names):**
1. the measured null floor within **30%** of `√(ln m / M_in)` across every slice;
2. **zero wrong answers** above `c·floor` (with `c=4`) across the SNR×`D/L`×band×excess-phase grid.

If both hold, `c=4` is confirmed and the record's §4 is annotated "confirmed by survey 2026-09-…". If check 2 fails at `c=4`, raise `c` to the smallest value that passes, amend record §4 in place, and update `DelayPolicy::acceptMultiple`'s default (DEL-R3).

- [ ] **Run:** `.venv/Scripts/python.exe tools/probe_delay_nullfloor.py` and `tools/probe_delay_trust.py`; paste both tables into the report.
- [ ] **Confirm-or-move:** state in the report whether `c=4` survived; if moved, name the new value and the amended record line.
- [ ] **No commit of generated data** — these are measurements, not goldens (record §12). Commit only the two scripts: `git add tools/probe_delay_trust.py tools/probe_delay_nullfloor.py` → `test(tools): argparse-guarded surveys confirming the delay null floor and the c=4 accept multiple`

## Task F — the raw-capture path and Locate orchestration (app; record §1.1, §11)

Builds the missing raw-capture path and the Locate sequence. Split by the guard line: the pure pieces (OFF) then the JUCE wiring (ON).

### F1 — `RawCaptureBuffer` + `DelayLocator` (app, OFF; JUCE-free)

**Files.** Create `app/src/measure/RawCaptureBuffer.h` (≤ 120, header-only: two pre-sized `std::vector<float>`, `arm(L)` sizes once, `feedHop(ref,meas)` appends until full, `isFull()`, `reference()`/`measurement()` spans), `app/src/measure/DelayLocator.h` (≤ 140) + `app/src/measure/DelayLocator.cpp` (≤ 200: the arm→wait→capture→suggest state machine over `rta::platform::OutputEngine&`, mirroring `CaptureSequencer`'s named-refusal shape), `app/tests/test_delay_locator.cpp` (≤ 260); modify `app/tests/CMakeLists.txt` (add the test + `DelayLocator.cpp` to `rtatool_analysis_tests`, links `rta::core rta::platform_types`) and **add the three JUCE-free files to `measure_has_no_framework_deps`'s GLOBS** (`app/tests/CMakeLists.txt:110`).

**Locate sequence (record §11.2), driven by `DelayLocator`:** `setSource(gen::PinkNoise{...})` → `soloOutput(engine, ch)` (record §3, DEL-R4) → `armSource()` → wait `renderedSamples() >= 480·fs/48000` (record §11: the first 10 ms are not stationary) → capture `L` via `RawCaptureBuffer` → `disarmSource()` → `suggestDelay(ref, meas, opts, policy)` → hold a `DelaySuggestion`. `L` default `>= 4·maxLag` (record §11.5, keeps overlap loss under §4's 0.77 row — a convenience default, not a gate). Never writes `referenceDelaySamples` itself (record §8).

**RED first.** `test_delay_locator.cpp` includes `RawCaptureBuffer.h`; build fails at the include. Paste it.

| # | case | closed-form acceptance |
|---|---|---|
| **F1a (first)** | the accumulator yields exactly L (§12.13) | `arm(L=4096)`, feed hops of mixed sizes summing past `L`: `reference()`/`measurement()` each have size exactly `L`, and their contents equal the first `L` samples of the fed stream, in order (no gap, no double-count) |
| F1b | the accumulator does not allocate mid-capture | `arm(L)` sizes the vectors once; `feedHop` under a counting allocator performs zero allocations after `arm` |
| F1c | Locate drives OutputEngine in order (§12.13) | against a real (JUCE-free) `OutputEngine`, stepping `DelayLocator` observes `sourceIsQuiescent()` false after arm, exactly one routed output (`role(ch)==Routed`, all others `None`), and `disarmSource` before `suggestDelay` runs — arm → capture → disarm order, no device |
| F1d | Locate refuses `gen::Mls`, uses PinkNoise | constructing `DelayLocator` with an `Mls` excitation returns a named refusal (periodic source, record §1.5); `PinkNoise` is accepted |
| F1e | end-to-end on a synthetic pair | feed the locator hops of `y=x[n−300]` (pink `x`): the held `DelaySuggestion.best.delaySamples==300`, `verdict==Accepted` |

- [ ] **Accept:** OFF ctest count rises; zero `/W4`; `measure_has_no_framework_deps` green with the three new files in its GLOBS (Task G).
- [ ] **Mutation:** make `DelayLocator` skip the `renderedSamples() >= 480·…` wait → F1e's `best` drifts off 300 (the non-stationary first 10 ms contaminate the capture); revert.
- [ ] **Commit:** `git add app/src/measure/RawCaptureBuffer.h app/src/measure/DelayLocator.h app/src/measure/DelayLocator.cpp app/tests/test_delay_locator.cpp app/tests/CMakeLists.txt` → `feat(app): the raw-capture path and the Locate sequence — pink-noise solo excitation, arm/capture/disarm, suggestDelay; JUCE-free`

### F2 — `AnalysisThread` accumulator feed + `MainComponent` Apply (app, ON; JUCE)

**Files.** Modify `app/src/measure/AnalysisThread.h`/`.cpp` (own a `RawCaptureBuffer` armed at Locate-arm time, never in the callback; feed it inside `drainPaired` from the reference and the measurement channel the active transfer function names — record §11.1); modify `app/src/MainComponent.h`/`.cpp` (a Locate button driving `DelayLocator` over `audioIo_.output()`; **Apply** rebuilds the `Analyser`/`AnalysisThread` with `referenceDelaySamples = best.delaySamples` — sub-sample shown, not applied, the engine offset is an integer by design (`DualFftEngine.h:43-50`) — and accepts the ~16-frame gate re-fill, record §1.6, §8); presentation per record §11.4: one-decimal ms + whole samples (spec `interactive-tuning-visuals.md:75`, and CLAUDE.md's "dB one decimal, Hz whole number" readout rule — ms one decimal, samples whole), `trust` as words or a bar never a bare float, candidates as a short list, `inverted` as a flag; the tracker's residual as the drift readout G16's chip consumes (Task not built here — see §G16).

**Files (test).** Add an output-contract `TEST_CASE` to an `app/tests_juce` file (e.g. a new `app/tests_juce/test_delay_locate.cpp`, registered in `app/tests_juce/CMakeLists.txt`).

**RED first.** The test arms Locate through `AnalysisThread` and asserts the accumulator fills from the same hop sequence the engine saw; before the feed is wired the buffer stays empty → fails. Paste it.

| # | case | closed-form acceptance |
|---|---|---|
| **F2a (first)** | the accumulator sees the engine's hops (§12.13) | driving `AnalysisThread` off a synthetic bus, the armed `RawCaptureBuffer` fills with exactly the reference/measurement hops `drainPaired` fed the Analyser, same order |
| F2b | Apply rebuilds and takes effect (§12.13) | after Apply with `best.delaySamples=D`, the next published snapshot's engine reports `referenceDelaySamples==D`; coherence is nullopt for ~16 frames then re-opens (the documented gate re-fill, record §1.6) |

- [ ] **Accept:** ON ctest count rises by the F2 group; zero `/W4`.
- [ ] **Commit:** `git add app/src/measure/AnalysisThread.h app/src/measure/AnalysisThread.cpp app/src/MainComponent.h app/src/MainComponent.cpp app/tests_juce/test_delay_locate.cpp app/tests_juce/CMakeLists.txt` → `feat(app): AnalysisThread feeds the raw accumulator; MainComponent Locate + Apply rebuilds referenceDelaySamples`

## Task G — prove the guards still GUARD (both configs; record §11)

No new files; a procedure whose output goes in the report. Every count is **read from the guard's own line**, never predicted here.

- [ ] **GREEN, core framework guard, new files scanned.** `core_has_no_framework_deps` green with `PhatCorrelation.h`, `DelayPolicy.{h,cpp}`, `ResidualTracker.{h,cpp}` in scope; paste the `(N files scanned)` line (it rises). **RED once:** add `#include <juce_core/juce_core.h>` atop `DelayPolicy.h`, reconfigure, `ctest -R core_has_no_framework_deps` → paste the failure naming `DelayPolicy.h` → remove the line.
- [ ] **GREEN, coherence gate untouched.** `coherence_gate_is_not_bypassed` green — `ResidualDelayTracker` reads `TransferSnapshot::coherence` and writes nothing; confirm the guard's file list is unchanged (the tracker is not a writer).
- [ ] **GREEN, app measure guard, new files in GLOBS.** `measure_has_no_framework_deps` green with `RawCaptureBuffer.h`, `DelayLocator.h`, `DelayLocator.cpp` added to its GLOBS; **RED once:** add a JUCE include to `DelayLocator.h` → paste the failure → remove it.
- [ ] **Lengths.** `wc -l` over every new file: all < 400, and `DelayPolicy.cpp` / `ResidualTracker.cpp` reported (aim < 300). `DelayFinder.h` untouched (`git diff --stat main -- core/include/rta/dsp/DelayFinder.h` empty).

---

## The G16 relationship (record §9) — noted, NOT built

G16 (environment compensation) is a **downstream consumer** of this lane, not an input. The tracker's residual over time *is* the measured drift the spec's V3d HUD chip watches (record §9: 1 °C ≈ 0.17% of `c`, ~7 samples on a 30 m throw). G16 supplies the *predicted* drift from an entered temperature; the chip compares the two. **Nothing in G16 changes what the correlator computes.** This plan builds only the tracker that supplies the measured drift; the chip, the `c(T,RH)` formula, and the amber threshold are G16's own station. Take to the orchestrator: the parity table row 46 should be re-worded at station 3 to "the delay *readout* gains an environment input; the drift watch consumes the L7-DELAY tracker" (record §9).

## Numbers the builder must measure, not copy

Every figure is a prediction to falsify. Measure the baselines on HEAD `1ad48da` **before** Task A; if a measurement disagrees, the plan is wrong and the orchestrator hears about it.

| quantity | how |
|---|---|
| ctest OFF baseline `base_core` / `base_app` | `--clean-first` OFF build, `build-l7delay` |
| ctest ON baseline `base_on` | `--clean-first` ON build, `build-l7delay-on` |
| OFF after A / B / C / F1 | re-read from ctest each time a `TEST_CASE` group lands |
| ON after F2 | re-read from ctest |
| `core_has_no_framework_deps` / `measure_has_no_framework_deps` scanned | the guard prints `(N files scanned)`; both rise |
| survey: null floor vs `√(ln m/M_in)`; wrong answers above `c·floor` | Task E, main-checkout venv |
| MSVC `/W4` warnings | grep the build log; a warning is a defect |

## Build sequence and acceptance gate

1. **Task A** (`suggestDelay` + spectral refactor + refactor lock, OFF) — the detail header everything else reuses.
2. **Task B** (trust, null floor, accept gate, band limit, OFF).
3. **Task C** (`ResidualDelayTracker`, OFF).
4. **Task E** (survey) — can run in parallel with F once A/B exist; confirms `c`.
5. **Task F1** (raw accumulator + Locate, OFF) then **F2** (AnalysisThread + MainComponent, ON).
6. **Task G** (guards shown to guard, both configs).

**Acceptance gate:** OFF and ON ctest both green at the measured counts; **0 `/W4`** in both; all 9 `test_delay_finder` fixtures green unchanged and `suggestDelay(...).best` bit-for-bit equal to `findDelayPhat` on each; `core_has_no_framework_deps` and `measure_has_no_framework_deps` green with risen scanned counts and each shown red once; `coherence_gate_is_not_bypassed` green and its writer list unchanged; the survey's two checks reported (c=4 confirmed or amended); every new file < 400 lines.

## What L7-DELAY does NOT include (deferred; record §13)

- The exact `c` beyond the survey's confirm-or-move (record §13); the value ships as a `DelayPolicy` field, amendable without a code change to the gate.
- G16's `c(T,RH)` formula, the samples→ms→metres readout conversion, and the amber drift-chip threshold (record §9, §13).
- A per-band residual over `MtwResult::bandSnapshots` — the tracker runs on the fixed engine only; nothing here forecloses the later amendment (record §13).
- Persisting the applied delay per transfer function — already a `[tf]` field in L6b schema 3 (record §13).
- Locate on programme material with no excitation — the core path supports it (one frame reads `γ²≡1`); whether the UI offers it is an L7 presentation call (record §13).
- The G17 wizard's use of `candidates`/`trust`/`ambiguity` — it receives the whole `DelaySuggestion`; what it does with it is its record's business (record §13).
- Decorrelated / multi-signal excitation (L7-OUT §12).

## Open, needs a human (record §14)

1. **Default plausibility window (§14.1).** Ships full-range with an operator distance-narrowing; should the app ship a venue-scale default (e.g. 150 m ≈ 440 ms) so a stadium and a studio do not start from the same window? One constant if yes — planned as a comment on `DelayPolicy`'s default, no fork.
2. **Does Locate solo by default (§14.2)?** This plan uses `soloOutput` before capture (DEL-R4), sharing L7-OUT §13.2's owner ruling (StrictSolo for a sequence). Confirm the same answer applies to a manual Locate.
3. **Tracker on by default (§14.3)?** One IFFT per publish, returns absence when the gate is closed, so "on" is cheap — but a residual reading 3 samples on every screen may invite chasing noise. The spec's V3d chip suggests it lives with the drift watch; confirm on-vs-armed. One bool, no fork.

## For the station-4 builder, first read

1. The record `docs/dsp/2026-09-06-l7-auto-delay.md` is binding; this plan implements its §11 boundary and flags DEL-R1..R4, which the orchestrator amends first.
2. Order is fixed by dependency: **A → B → C → (E ∥ F1) → F2 → G**. One commit per task.
3. The failing test first, seen to fail (the missing `#include`), then the header, then the body. Paste the command and its output for every "done" — a green build proves it compiles, not that the numbers are right (CLAUDE.md).
4. `DelayFinder.h` and `test_delay_finder.cpp` are the refactor lock, not edit targets (beyond `DelayFinder.cpp`'s internals) — prove them green and `DelayFinder.h` untouched (Task A, Task G).
5. Every count here is a prediction you are expected to falsify if it is wrong.
