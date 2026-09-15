# L7-ALIGN — the alignment wizard, the virtual processor and the crossover surface (lane L7, sub-lane L7-ALIGN: G11 + G17 + G18, with relative polarity ρ)

> **For agentic workers:** REQUIRED SUB-SKILL `superpowers:test-driven-development` and `superpowers:executing-plans`. Steps are checkboxes; the failing test is written and *seen to fail* before the code. Station 5 (adversarial verify, a reviewer with **no Write tools**) is not optional.

*2026-09-15, lane L7, sub-lane L7-ALIGN (Wave 3), station 3. Written from the decision record `docs/dsp/2026-09-06-l7-alignment-wizard.md` after reading the real files it cites — not the record's description of them. Worktree `.claude\worktrees\agent-a96fd5d9928b2e92d`, branched from `main` at `23b7ea0`. Every path below was existence-checked; a path marked **NEW** is not in the tree today. Eight places where the record's spelling and the landed code disagree are in "Reconciliations"; the orchestrator amends the record, the builder does not silently re-decide. Every acceptance number is a closed-form identity or a bounded, reported residual; **L7-ALIGN needs no new golden vector** (record §10).*

## 0. Hazard first: what this lane may NOT do, and the one thing it is waiting on

**The forbidden move.** G17 **asks** topology and asks whether the processor already inverts one output. It never derives either from a measurement, and "maximise the measured sum" / "minimise the band ripple" **is** that derivation wearing a different hat (record §6, owner ruling 2026-08-30, HANDOFF "Ba quyết định phiên sau KHÔNG suy diễn lại" #2). A builder who adds an optimiser over `|H_A + H_B|` has broken the lane's premise, not improved it. Task I carries a structural test that no such entry point exists.

**The order-4 question is SETTLED — ruling from PR #3 (pending merge).** Record §13.1 / `docs/HUMAN-QA-QUEUE.md:80` asked why the `N·90°` identity predicts the *right* sign at BW4 while L4a *measured* the wrong sign at orders 2 **and** 4. The probe on branch `l7/align-order4-probe` (PR #3, research doc **`docs/research/2026-09-15-l7-align-order4-probe.md`** — **NEW, not merged into `main` yet**) answers it, and this plan adopts the answer:

1. **The identity holds exactly under this repo's convention**, analog and digital SOS, Butterworth and Linkwitz-Riley, orders 1–8, at every frequency (probe §2, §8). **Record §3's table ships unchanged.** A row for a new *order* stays arithmetic.
2. **L4a's wrong sign came from the estimator, not from a convention**: a cross-correlation **peak-sign** rule (`core/src/dsp/DelayFinder.cpp:34`) applied across two systems with *different passbands*. Two band-pass boxes spread the HP−LP offset by **62.9°** across the overlap instead of holding it constant, stretching the correlation envelope until a neighbouring, oppositely-signed lobe outgrows lag 0 (peak at −3.08 ms, 1.45× the lag-0 value) — while the value **at lag 0 stays positive**, exactly as the identity says (probe §4, §5).
3. **No sign convention in this engine can reach the order-4 reading.** Conjugating the transfer function negates the offset, and −0° = 0° and −180° = 180°, so an even-order pair reads identically either way (probe §6).

**What that binds on this lane.** (a) The wizard **never reads a topology sign off a cross-correlation peak** — §4's complex band fit with its bounded `R` is the estimator, and `R` collapsing is precisely how it reports "these two are not a matched pair". (b) An even-order sign discrepancy is evidence about the **system** (a band-passed source, a processor whose HP is `(−s)^N/D`, or a wiring inversion the operator must be asked about under question (c)) — **never** evidence about a convention.

**Where the ruling is consumed.** Three places, each with a red-able fixture, each citing the probe doc: **D5** (the shipped signed lookup), **H9** (the wizard's comparison of `φ₀` against that lookup) and **I1b** (the surface's target line). The earlier draft of this plan claimed the seam was confined to D5; it was not (verifier defect 7) — H and I consume `expectedOffset` too, and now each pins a sign. Since PR #3 is **not merged**, a builder who reaches D5 before it lands cites the doc by branch and says so in the commit message; nothing here waits, because the ruling keeps the table as written.

**What already exists (verified, file:line).**

| thing | where | note |
|---|---|---|
| complex biquad response | `core/include/rta/dsp/BiquadResponse.h:20`, `:25` | `biquadResponse`, **`cascadeResponse`** — its own comment (`:23-24`) says it *is* "the G11 biquad-cascade virtual-processor op (ALIGN Sec.5)". **Task A does not re-derive it.** |
| RBJ design + signed response | `core/include/rta/eq/BiquadDesign.h:18`, `:25` | `designBiquad`, `responseDb` (`+ = boost`, `:21-24`); throws `std::invalid_argument` out of shelf domain (`c61b5dc`) |
| the filter vocabulary | `core/include/rta/eq/FilterSpec.h:9`, `:14-19` | `FilterType{Peaking,LowShelf,HighShelf}`, `{type,fcHz,q,gainDb}` — **four fields, that is all** |
| cascade magnitude, the other spelling | `core/include/rta/dsp/Biquad.h:75`, `:80`, `:87` | `BiquadCascade::attenuationDb` — **decibels of ATTENUATION, positive = down** |
| delay estimator + policy | `DelayFinder.h:11-53`, `:97`; `DelayPolicy.h:41-65`, `:86` | `DelayEstimate{delaySamples,subSample,peak,inverted}`; `DelaySuggestion{best,trust,nullFloor,ambiguity,candidates,verdict}` |
| L4a polarity | `core/include/rta/ir/Polarity.h:11`, `:23-55`, `:57-88`, `:143` | `Sign`, `Refusal::BandTooHigh` (`:30`), `PolarityConfig::searchSeconds` (`:68`), `findPolarity` |
| the arrival | `core/include/rta/ir/Deconvolver.h:34` | `Deconvolution::originIndex` |
| interpolated crossing | `core/src/ir/Polarity.cpp:39-48` | file-local `interpolatedCrossing` — **not exported**; Task B re-expresses the same two-point form in its own file |
| the output primitive | `platform/types/include/rta/platform/OutputEngine.h:63`, `:52-69` | `routeOutput` is the only gate call |
| strict solo | `app/src/measure/OutputPolicy.h:20`, `:34`, `:45` | `SoloMode{StrictSolo,Additive}`, `soloOutput`, `applyToggle`; owner ruling in the comment `:16-19` |
| the live spectrum | `core/include/rta/dsp/TransferEstimator.h:60-71` | `TransferSnapshot{estimator,sampleRate,binWidthHz,effectiveAverages,h,magnitudeDb,phaseRadians,coherence}`; `coherence` is `std::optional`, `:68` |
| the stored trace | `app/src/trace/Trace.h:15`, `:25-41`, `:75-77` | `Field{Magnitude,Phase,Coherence}`, `CaptureMeta`, access via `field(Field)` |
| the Bode geometry | `app/src/view/BodeLayout.h:22-37` | `PaneRect`, `BodePanes`, JUCE-free, in `rta::view` |
| the G18 mockup | `app/src/dev/preview/PhaseAlignPreview.h:14` (comment `:9-13`) | paint-only, **canned data**; already in the build (`app/CMakeLists.txt:170-173`) and rendered by `rtatool_snapshot` |

**What does not exist anywhere in code (docs only — this lane builds all of it):** `VirtualTrace`, `spectralCrossover`, `crossoverBandFit`, `relativePolarity`.

## Global constraints

- SPDX header `// SPDX-License-Identifier: AGPL-3.0-or-later` on every new file.
- **`core/` never includes JUCE/Qt/a device API** (`core_has_no_framework_deps`, `core/tests/CMakeLists.txt:81`). The new core files are spans and `std::complex<double>` in, numbers out.
- **Nothing in `core/` knows what a topology is** (record §9). The §3 table is an `app/` lookup over an enum the operator chose. A builder who puts `enum class CrossoverFamily` in `core/` has moved the ruling's boundary.
- **The sum's per-bin trust field must NOT be named `coherence`** (record §5). `coherence_gate_is_not_bypassed` (`core/tests/CMakeLists.txt:114`, script `check_coherence_gate.cmake:44-47`) greps `core/src/*.cpp` + `core/include/*.h` for `.coherence =`, `->coherence =`, `coherence = std::vector`, `coherence .emplace` and reference-binds, with `TransferEstimator.cpp` as the exempt sentinel. Name the field `summationTrust`.
- **The JUCE-free half of `app/` stays JUCE-free** (`measure_has_no_framework_deps`, `app/tests/CMakeLists.txt:117`, GLOBS on `:119`). Every new `app/src/measure/`, `app/src/trace/` and `app/src/view/` file in this lane is added to that GLOBS string and is tested in the **OFF** config (`CMakeLists.txt:46` adds `app/tests` outside `RTA_BUILD_APP`). Only the `PhaseAlignPreview` specimen is JUCE, tested ON.
- **`filter_design_has_no_polynomial_form`** (`core/tests/CMakeLists.txt:92`) scans `tools/*.py` too: the ρ probe scripts (Task F) must not use `output='ba'`, `zpk2tf`, `tf2sos`, `tf2zpk`, `sos2tf`, `np.poly(`, `signal.lfilter`, `freqz(b`.
- **Hard cap 400 lines, aim 300**, headers too. Budgets per file below.
- **Never assert a value the implementation produced** (CLAUDE.md verification standard). Acceptance is a closed-form identity, a cited standard, or a bounded statistical residual **printed beside its tolerance**. No new golden vector (record §10).
- **Float32 awareness** (`memory/float32-fft-precision.md`): the G11 ops and the fit run in `double` on constructed fixtures → plain `1e-9`..`1e-12`. Anything crossing `Trace`'s `float` storage (Task G) uses a float-shaped tolerance with the residual reported.
- Build dirs **`build-l7align`** (OFF) / **`build-l7align-on`** (ON), **Visual Studio generator, never Ninja** (`memory/a-misconfigured-build-goes-99-percent-of-the-way.md`). MSVC `/W4` is the truth: **0 warnings**.

```
cmake -S . -B build-l7align -G "Visual Studio 18 2026" -A x64
cmake --build build-l7align --config Release --parallel
ctest --test-dir build-l7align -C Release --output-on-failure
```

For ON append `-DRTA_BUILD_APP=ON -DRTA_JUCE_PATH=<path>` and use `build-l7align-on`.

- **CI is now a merge gate** (`63a3f9b`, `.github/workflows/ci.yml`): the OFF config builds and tests on `ubuntu-latest`, `macos-latest` and `windows-latest`. Every core and JUCE-free-app file this lane adds must compile on GCC and Clang, not only MSVC — no `__declspec`, no MSVC-only pragma, no reliance on MSVC's `<complex>` extensions, and no test that assumes Windows path separators.

## Namespace decision

**Spectral estimator math → `rta::dsp`. Time-domain IR work → `rta::ir`. App orchestration, the topology table and the surface model → `rta::measure` / `rta::view`.** This inherits L7-DELAY's rule (`docs/plans/2026-09-07-L7-delay-impl-plan.md` §"Namespace decision") and splits along the existing seams, adding no namespace.

`relativePolarity` is the one placement the record states differently — see ALIGN-R3.

## The API this lane builds (record §5, §9; names are this plan's)

```cpp
// core/include/rta/dsp/VirtualProcessor.h      NEW   (rta_core, namespace rta::dsp)
namespace rta::dsp {

/// Every op: frequency-domain, stateless, spans in, span out. `binWidthHz` is
/// the axis (TransferSnapshot::binWidthHz, :63) -- there is no frequency
/// vector in this codebase (ALIGN-R4). Bin k is at k*binWidthHz.
void applyDelay   (std::span<const std::complex<double>> in, double binWidthHz,
                   double tauSeconds, std::span<std::complex<double>> out) noexcept;
void applyPolarity(std::span<const std::complex<double>> in,
                   std::span<std::complex<double>> out) noexcept;
void applyGain    (std::span<const std::complex<double>> in, double gainLinear,
                   std::span<std::complex<double>> out) noexcept;
/// Delegates per bin to rta::dsp::cascadeResponse (BiquadResponse.h:25) --
/// the numerator/denominator is NOT re-derived here (W0-R3, ALIGN-R1).
void applyBiquads (std::span<const std::complex<double>> in, double binWidthHz,
                   double sampleRate, std::span<const Biquad::Coeffs> sections,
                   std::span<std::complex<double>> out);

/// The sum carries no coherence -- it is not an estimate a cross-spectrum
/// defines (record §5). It carries a per-bin TRUST, and the field is
/// deliberately not called `coherence` (see Global constraints).
struct SummedResponse {
    std::vector<std::complex<double>> h;
    std::vector<float> summationTrust;   // min(gamma^2_A, gamma^2_B)
    bool trustPresent = false;           // false when either input's coherence was absent
};
[[nodiscard]] SummedResponse sumResponses(std::span<const std::complex<double>> a,
                                          std::span<const std::complex<double>> b,
                                          const std::optional<std::vector<float>>& coherenceA,
                                          const std::optional<std::vector<float>>& coherenceB);
}  // namespace rta::dsp
```

```cpp
// core/include/rta/dsp/CrossoverFit.h          NEW   (rta_core, namespace rta::dsp)
namespace rta::dsp {

enum class CrossoverRefusal { None, AllBinsAbsent, NoCrossing, TooFewBins, RangeEmpty };

struct SpectralCrossover {
    double frequencyHz = 0.0;               // interpolated, Polarity.cpp:39-48's two-point form
    std::vector<double> allCrossingsHz;     // ascending; the wizard asks when there is no seed
    CrossoverRefusal refusal = CrossoverRefusal::AllBinsAbsent;
};
/// ARGUMENT CONTRACT, load-bearing (ALIGN-R14): **A is the HIGH-PASS side,
/// B is the LOW-PASS side**, for this function and for crossoverBandFit. It is
/// what makes record §3's arg(H_HP) - arg(H_LP) and record §4's
/// R_k = H_A conj(H_B) the same quantity. Transposing the two silently flips
/// both tau's sign and phi_0's comparison against the §3 table.
[[nodiscard]] SpectralCrossover spectralCrossover(
    std::span<const float> magnitudeDbA, std::span<const float> magnitudeDbB,
    const std::optional<std::vector<float>>& coherenceA,
    const std::optional<std::vector<float>>& coherenceB,
    double binWidthHz, double minimumGatedCoherence, std::optional<double> seedHz);

struct DelayCandidateTau { double tauSeconds = 0.0; double agreement = 0.0; };

struct BandFit {
    double tauSeconds = 0.0;      // + => B (the LP side) arrives later than A (the HP
                                  //      side); delay the HP side by this
    double interceptRadians = 0.0;// phi_0 = arg(H_A) - arg(H_B) = arg(H_HP) - arg(H_LP)
                                  //      after tau* is removed; wrapped to (-pi, pi].
                                  //      Compared directly against §3's expectedOffset.
    double agreement = 0.0;       // R in [0,1] by the triangle inequality
    double meanFrequencyHz = 0.0; // f_bar, the weight-weighted mean -- defines the cycle spacing
    std::vector<DelayCandidateTau> cycleCandidates;
    CrossoverRefusal refusal = CrossoverRefusal::AllBinsAbsent;
};

struct BandFitOptions {
    double centreHz = 0.0;
    double octavesEachSide = 1.0;      // record §4: proposed default, MEASURED before it ships
    double tauRangeSeconds = 0.020;    // bounded search, caller-supplied
    double tauGridSeconds = 1.0e-6;    // grid step delta, refined parabolically
    int maxCycleCandidates = 3;
    double minimumGatedCoherence = 0.0;
    /// D_B - D_A from CaptureMeta::appliedDelaySamples (Trace.h:38). H_B is
    /// pre-rotated by e^{+j2 pi f (D_B - D_A)/fs} BEFORE the fit (record §1.4,
    /// §4). Core owns it so §10.5 is a core test (ALIGN-R2).
    double appliedDelayDifferenceSamples = 0.0;
    double sampleRate = 48000.0;
};

/// hA = the HIGH-PASS side, hB = the LOW-PASS side (the contract above).
[[nodiscard]] BandFit crossoverBandFit(std::span<const std::complex<double>> hA,
                                       std::span<const std::complex<double>> hB,
                                       const std::optional<std::vector<float>>& coherenceA,
                                       const std::optional<std::vector<float>>& coherenceB,
                                       double binWidthHz, const BandFitOptions& options);
}  // namespace rta::dsp
```

```cpp
// core/include/rta/ir/RelativePolarity.h       NEW   (rta_core, namespace rta::ir)
namespace rta::ir {

struct RelativePolarityConfig {
    /// The SAME arrival window L4a uses. `searchSeconds` lives on
    /// PolarityConfig (Polarity.h:68), not on Deconvolution -- ALIGN-R3.
    double searchSeconds = 0.05;   // == PolarityConfig's default, cited not re-chosen
};

/// rho = |r(l*)| / sqrt(E_a E_b) in [0,1] by Cauchy-Schwarz, and the sign of
/// r(l*). NO VERDICT, NO THRESHOLD: record §8 forbids shipping a number until
/// two independent grids agree (Task F).
struct RelativePolarity {
    double rho = 0.0;
    Sign sign = Sign::Unknown;
    std::ptrdiff_t lag = 0;
    Refusal refusal = Refusal::NoSignal;   // reuse L4a's enum (Polarity.h:23-55)
};

[[nodiscard]] RelativePolarity relativePolarity(const Deconvolution& a, const Deconvolution& b,
                                                const RelativePolarityConfig& config);
}  // namespace rta::ir
```

```cpp
// app/src/measure/CrossoverTopology.h          NEW   (namespace rta::measure)
enum class CrossoverFamily { LinkwitzRiley, Butterworth };
enum class ProcessorInversion { Yes, No, Unknown };          // wizard question (c)
struct Topology { CrossoverFamily family = CrossoverFamily::LinkwitzRiley; int order = 4; };
struct ExpectedOffset { double radians = 0.0; bool ambiguous = false; double alternativeRadians = 0.0; };
/// The §3 table as ONE closed form, evaluated. `ambiguous` is question (c)'s
/// "Unknown" branch: two candidate lines, the operator picks (record §13.3).
[[nodiscard]] ExpectedOffset expectedOffset(Topology t, ProcessorInversion inversion);
```

```cpp
// app/src/trace/VirtualTrace.h                 NEW   (namespace rta::trace)
/// NOT a Trace, has no CaptureMeta, cannot reach TraceLibrary (record §5).
/// Builds H = 10^{dB/20} e^{j phi} from a Trace's field(Magnitude)/field(Phase)
/// (Trace.h:77) or takes `h` from a live TransferSnapshot, runs the chain in
/// core/, converts back for display. The ONE conversion point.
struct VirtualOp { /* Delay | Polarity | Gain | Biquads, a variant */ };
class VirtualTrace { /* sources; std::vector<VirtualOp> chain; render(); */ };
```

```cpp
// app/src/measure/AlignmentWizard.h            NEW   (namespace rta::measure)
// app/src/view/CrossoverSurface.h              NEW   (namespace rta::view, JUCE-free)
```

## Reconciliations made while planning (take to the orchestrator; do not silently re-decide in code)

- **ALIGN-R1 — `sectionAttenuationDb` does not exist as the record spells it; the consistency lock is `20log₁₀|H| == −attenuationDb`.** Record §5's biquad row says "`20log₁₀|H_i|` equals `sectionAttenuationDb` to 1e-12". In the tree, `sectionAttenuationDb` is a **private static member function** of `BiquadCascade` (`core/include/rta/dsp/Biquad.h:109`) — unreachable from a test — and the public spelling is `BiquadCascade::attenuationDb` (`:80`, `:87`), documented at `:75` as "**decibels of ATTENUATION (positive = down)**". *Decision:* Task A4's lock is `20*log10(std::abs(cascadeResponse(s, w))) == -rta::dsp::BiquadCascade::attenuationDb(s, w)` to 1e-12. This is the same sign correction `BiquadDesign.h:21-24` already applies to `responseDb` and the one EQ-R5 shipped. **The record needs a one-line amendment** — HANDOFF already carries this as an owed touch-up ("Record touch-up còn nợ (closeout)").
- **ALIGN-R2 — the `appliedDelaySamples` pre-rotation is a fit OPTION in `core/`, not an app-side pre-multiply.** Record §4 says H_B is pre-rotated; record §9 files "the `appliedDelaySamples` reconciliation" under `app/`. Split across the seam, §10.5's "off by exactly 48/fs without it" would not be a core test and no fixture would lock it. *Decision:* `BandFitOptions::appliedDelayDifferenceSamples` + `sampleRate`; `app/` reads the two `CaptureMeta::appliedDelaySamples` values (`Trace.h:38`) and passes the difference. One place, testable OFF, and the correction is impossible to forget because it is a required field of the options struct.
- **ALIGN-R3 — `relativePolarity` lives in `rta::ir`, not `rta::dsp`.** Record §9 files it under `core/ (rta::dsp)`. Its inputs are two `rta::ir::Deconvolution` and its window is L4a's arrival window; putting it in `rta::dsp` would make `dsp` include `rta/ir/Deconvolver.h`, inverting a dependency direction that holds everywhere today. *Decision:* `core/include/rta/ir/RelativePolarity.h`, beside `Polarity.h`, reusing `rta::ir::Sign` and `rta::ir::Refusal`.
- **ALIGN-R4 — `Deconvolution` has no `searchSeconds`, and `Trace` has no `magnitudeDb`/`phaseRadians`.** Record §7 says ρ's window is "L4a's arrival window (`originIndex`, `searchSeconds`)": `originIndex` is `Deconvolver.h:34`, but `searchSeconds` is `PolarityConfig::searchSeconds` (`Polarity.h:68`). Record §1 says "`Trace` stores `magnitudeDb` and `phaseRadians` as separate `float` arrays (`app/src/trace/Trace.h:100-101`)": those are the **private** members `magnitude_`, `phase_`, `coherence_` (`:100-102`); the public surface is `field(Field)` / `has(Field)` (`:75-77`), and `magnitudeDb`/`phaseRadians` are `TransferSnapshot`'s names (`TransferEstimator.h:66-67`). *Decision:* ρ gets its own `RelativePolarityConfig{searchSeconds = 0.05}` citing `PolarityConfig`'s default; `VirtualTrace` reads through `field(Field)`.
- **ALIGN-R5 — there is no frequency axis to pass; `binWidthHz` is the axis.** Record §5 says each op takes "frequency axis in". `TransferSnapshot` carries `binWidthHz` (`TransferEstimator.h:63`) and no frequency vector; the engine side has `DualFftEngine::binFrequency(bin)` (`DualFftEngine.h:66`). *Decision:* every op takes `double binWidthHz`; bin k is `k*binWidthHz`. This also keeps the ops allocation-free.
- **ALIGN-R6 — `OutputEngine` has no `soloOutput`; strict solo is `rta::measure::soloOutput`.** Record §2 and §9 say "`soloOutput(A)`" and "`soloOutput` as L7-OUT §9 already places it". The primitive is `OutputEngine::routeOutput` (`OutputEngine.h:63`); solo is the app loop `rta::measure::soloOutput` (`OutputPolicy.h:34`). *Decision:* the wizard calls `rta::measure::soloOutput` and, per owner decision 4 (2026-09-06) and `OutputPolicy.h:16-19`, the wizard's auto-step sequence is `SoloMode::StrictSolo`; a manual toggle is `Additive`. No new solo path.
- **ALIGN-R7 — `CaptureMeta` has no reference id, so the "same reference" refusal uses `channelRoles`.** Record §9 requires a refusal when two traces disagree on "`sampleRate`, `fftSize` or reference". `CaptureMeta` (`Trace.h:25-41`) has `sampleRate` (`:30`), `fftSize` (`:31`) and `channelRoles` (`:29`) — a string — but **no reference-channel field**; the only structured one is `TransferFunctionSpec::referenceChannel` (`app/src/trace/SessionCodec.h:54`), which a `Trace` does not carry. *Decision:* the wizard compares `channelRoles` string-equal and names the refusal `ReferenceMismatch`, with a comment saying the proxy is a string because the type has nothing better. Flagged: if the orchestrator wants this structural, it is a `CaptureMeta` field and a session-schema bump, which this lane does **not** do.
- **ALIGN-R8 — G18 ships as the existing dev-preview specimen driven by a real model, not as `MainComponent` wiring.** Mirrors EQ-R4 and the OUT G20 note (HANDOFF). `app/src/dev/preview/PhaseAlignPreview.{h,cpp}` already exists as a **canned-data** mockup of exactly this view (`PhaseAlignPreview.h:8-13`) and is already in the build (`app/CMakeLists.txt:170-173`, rendered by `rtatool_snapshot`; note `app/src/dev/preview/WIRING.md` still says 117-120 — stale, and its own subject is stale build claims). *Decision:* all surface logic is a JUCE-free `rta::view::CrossoverSurface` model tested OFF; the specimen is repointed from canned arrays to that model and checked by the snapshot path (CLAUDE.md "Seeing the GUI"). Nobody should hunt for a `MainComponent` hook.
- **ALIGN-R9 — the G11 biquad op consumes `rta::eq::FilterSpec`, and does NOT wait for EQ Task E/F.** EQ-R3 left the seam: EQ writes an accepted `std::vector<FilterSpec>` into an app session model and G11 later filters with it. EQ Task E/F (app) is **still not built** (HANDOFF). *Decision:* `VirtualTrace`'s biquad op takes `std::span<const rta::eq::FilterSpec>` plus a sample rate, calls `designBiquad` (`BiquadDesign.h:18`) once per spec, and hands the coefficients to `applyBiquads`. It needs nothing from EQ's session model, so Wave 3 is not blocked by the unfinished EQ tasks. When E lands, its vector is the input; no code changes.
- **ALIGN-R10 — a cross-lane drift the orchestrator must fix in the EQ record, not here: EQ shipped MEAN, the record says MEDIAN.** `docs/dsp/2026-09-06-l7-auto-eq.md` §4.3.4 specifies a median broadband-delay deviation; the shipped code uses the **mean** (measured 57.5 samples' deviation vs 0.28 — HANDOFF "Wave 2"), and `EqInput` gained a field `hHalfGrid` the record does not name. **Nothing in L7-ALIGN inherits it** — the fit's `τ*`/`φ₀`/`R` contain no median and no mean-of-estimates; ρ is an argmax. Listed so the orchestrator amends the EQ record in the same pass as ALIGN-R1 and nobody "fixes" ALIGN into consistency with a sentence that is already wrong elsewhere.
- **ALIGN-R11 — the queue says the probe blocks the whole of Wave 3; this plan narrows that, and the orchestrator must ratify the narrowing.** `docs/HUMAN-QA-QUEUE.md:82` reads "KHÔNG chặn Wave 0/1/2; **chặn Wave 3 (ALIGN) build**", and `docs/HANDOFF.md:258` reads "Đề xuất: probe settle trước khi ALIGN build". *Decision:* with PR #3's ruling in hand the block is discharged for the whole lane — the ruling **keeps record §3's table**, so no task changes shape. The three places that consume the signed lookup (D5, H9, I1b) cite the probe doc and each carry a sign fixture that can go red. Filed as a reconciliation rather than assumed, because narrowing a recorded blocking item is the orchestrator's call, not a planner's (verifier defect 10).
- **ALIGN-R12 — record §10.4's `R = 1 − 1e-12` is not simultaneously satisfiable with its own τ tolerance.** `R` is evaluated at `τ*`, so `1 − R ≃ (πΔf·dτ)²/6`. Over the record's own 40–160 Hz window, `dτ = tauGridSeconds/20 = 5e-8 s` gives `1 − R = 5.9e-11`; `1e-12` needs `dτ ≤ 6.50e-9 s = grid/154`. *Decision:* C1 asserts the **derived** floor `R ≥ 1 − 1.01·(πΔf·dτ_max)²/6` so the two tolerances are one statement. The record's `1e-12` should be re-stated as a function of the refinement bound, or its τ bound tightened to `grid/154`.
- **ALIGN-R13 — record §3's `(−s)^N/D` explanation of the order-4 reading is arithmetically empty.** `(−s)^N = (−1)^N s^N`, so at every **even** N it is the identity map and cannot change an order-4 sign. PR #3 supplies the real mechanism (a peak-sign rule across two different passbands, `DelayFinder.cpp:34`; probe §5). *Decision:* the sentence is struck from the record at the orchestrator's amendment pass; this plan does not use it as evidence, and Task D's mutation is PR #3's `√2` red instead.
- **ALIGN-R14 — the `A = HP side, B = LP side` argument contract is load-bearing and the record never states it.** Record §3 defines the offset as `arg(H_HP) − arg(H_LP)`; record §4 defines `R_k = H_A·conj(H_B)` and "τ* is how much later the LP side arrives than the HP side". Those are the same quantity **only** if the caller passes A = HP. One transposition at the call site flips both `τ`'s sign and `φ₀`'s comparison against the §3 table, silently. *Decision:* the contract is written into the API block above, and pinned by a test in H5 (flipping `highPassSide()` flips `tauSeconds`), by H9 and by I1b.

---

## Task A — the five G11 virtual-processor ops (core, OFF; record §5, §10.1, §10.3)

Smallest self-contained core deliverable; Task G and Task I both consume it.

**Files.** Create `core/include/rta/dsp/VirtualProcessor.h` (**NEW**, ≤ 140), `core/src/dsp/VirtualProcessor.cpp` (**NEW**, ≤ 200), `core/tests/test_virtual_processor.cpp` (**NEW**, ≤ 320); modify `core/tests/CMakeLists.txt` (add the test to the `add_executable` source list, `:11-63`) and `core/CMakeLists.txt` (add the `.cpp`).

**RED first.** The test opens with `#include "rta/dsp/VirtualProcessor.h"`; the build fails at the include. Paste it.

| # | case (record §5, §10) | closed-form acceptance |
|---|---|---|
| A1 | delay, magnitude | `H ≡ 1`, τ = 17.3/48000 s, 512 bins at `binWidthHz = 46.875`: `abs(out[k]) == 1` to **1e-15** at every bin |
| A2 | delay, phase and its **sign** | same fixture: `arg(out[k]) == wrapToPi(−2π·k·binWidthHz·τ)` to **1e-12**; and at the bin nearest `f = 1/(4τ)` the argument is **negative** — the fixture that fails if the exponent's sign is flipped (`memory/dual-fft-conventions.md` item 2) |
| A3 | polarity | random fixed-seed `H`: `abs(out) == abs(in)` **bitwise** (negation is exact in IEEE-754, sign-bit only); `|wrapToPi(arg(out) − arg(in) − π)| ≤ 1e-15` |
| A4 | gain | `g ∈ {0.5, 1.0, 2.0, 10.0}`: `20log10|out| − 20log10|in| == 20log10(g)` to **1e-12**; `|wrapToPi(arg(out) − arg(in))| ≤ 4·ε` with `ε = std::numeric_limits<double>::epsilon()` — **not bitwise**: for `g = 10` the real and imaginary parts round separately and IEEE-754 does not require `atan2(10b,10a) == atan2(b,a)`, and CI runs three different libms (verifier defect 12c). For the exact powers of two the test additionally asserts bitwise equality, where scaling by a power of two *is* exact |
| A5 | biquad endpoints | for a peaking and a low-shelf `Coeffs` from `designBiquad`: at `ω=0`, `out/in == (b0+b1+b2)/(1+a1+a2)` with `imag == 0` to 1e-15; at `ω=π`, `== (b0−b1+b2)/(1−a1+a2)` likewise |
| A6 | **the two-spelling lock (ALIGN-R1)** | over 64 log-spaced `ω ∈ [1e-4, π)`: `20*log10(std::abs(cascadeResponse(s,ω))) == −BiquadCascade::attenuationDb(s,ω)` to **1e-12**. Labelled in the test as *a consistency lock between two spellings of one formula*, **not** an independent correctness proof |
| A7 | sum identity | two unit sources at φ ∈ {0,30,60,90,120,150,180}°: `20log10|H_Σ| == 20log10(2·|cos(φ/2)|)` to **1e-9** (constructed doubles; `memory/float32-fft-precision.md` does not apply). At 0° that is **+6.0206 dB**, at 90° **+3.0103 dB**, at 120° **0 dB**, at 180° `abs(H_Σ) < 1e-15` |
| A8 | the sum's trust is not coherence | `summationTrust[k] == min(γ²_A[k], γ²_B[k])` to 1e-7 (float inputs); with either input's `coherence` absent, `trustPresent == false` and `summationTrust` is **empty** — not a vector of zeros (`memory/a-placeholder-for-an-absent-result-erases-its-state.md`) |

- [ ] **Accept:** OFF ctest `base_core + N` (N read from ctest, not predicted). Zero `/W4`.
- [ ] **Mutation:** flip the delay exponent to `e^{+j2πfτ}` → A2's sign check fails; revert. **Mutation 2:** in `sumResponses`, return a zero-filled `summationTrust` instead of leaving it empty when coherence is absent → A8 fails; revert.
- [ ] **Commit:** `feat(core): the five G11 virtual-processor ops — delay, polarity, gain, biquad cascade, sum with a trust that is not coherence`

## Task B — `spectralCrossover` (core, OFF; record §2 computed-(1), §9)

**Files.** Create `core/include/rta/dsp/CrossoverFit.h` (**NEW**, ≤ 170 — shared with Task C), `core/src/dsp/CrossoverFit.cpp` (**NEW**, ≤ 220 for `spectralCrossover` alone), `core/tests/test_crossover_fit.cpp` (**NEW**, ≤ 300 — **B1–B5 only**); modify `core/tests/CMakeLists.txt` (source list `:11-63`), `core/CMakeLists.txt`.

**The split seam is named now, not discovered later (verifier defect 11).** Task C adds `core/src/dsp/CrossoverBandFit.cpp` and `core/tests/test_crossover_band_fit.cpp` rather than extending these two files: C's fixtures (an analytic BW4 grid, a 200-draw bounded-ness loop, cycle candidates) would push both past the 400-line cap the acceptance gate enforces. One header, two implementation files, two test files.

| # | case | closed-form acceptance |
|---|---|---|
| B1 | the interpolation is exact where the difference is linear | `magnitudeDbA[k] = +m·(k − 12.4)`, `magnitudeDbB[k] = −m·(k − 12.4)`, `m = 0.7`, `binWidthHz = 10`: the dB difference is exactly linear in k, so the two-point crossing is exact — `frequencyHz == 124.0` to **1e-9**. This pins the same arithmetic `core/src/ir/Polarity.cpp:39-48` uses |
| B2 | a real pair, with the bound stated | analytic BW4 LP/HP at `fc = 100 Hz` sampled on a 4096-point, 48 kHz grid: `|frequencyHz − 100| ≤ binWidthHz/2` **and the residual is printed**. Asserted as an interpolation bound, never as "whatever it printed" |
| B3 | every crossing is listed, the seed selects | a fixture with crossings near 100 Hz and 2 kHz: `allCrossingsHz.size() == 2`, ascending; `seedHz = 1500` selects the 2 kHz one, `seedHz = 300` selects the 100 Hz one; **no seed** → `refusal == None` with `frequencyHz == allCrossingsHz.front()` and the full list for the wizard to ask about |
| B4 | the gate excludes, it does not fabricate | bins whose `γ²` on either side is below `minimumGatedCoherence` take no part: a fixture whose only crossing sits in a low-coherence stretch → `refusal == NoCrossing`, `frequencyHz == 0.0` and `allCrossingsHz` empty |
| B5 | absent coherence | either `coherence` is `std::nullopt` → `refusal == AllBinsAbsent`, **made red once** by having the implementation treat `nullopt` as γ²≡1 |

- [ ] **Accept:** OFF ctest count rises; zero `/W4`.
- [ ] **Mutation:** clamp the interpolation parameter `t` to `{0,1}` instead of the open interval → B1's exact crossing lands on a bin edge and fails; revert.
- [ ] **Commit:** `feat(core): spectralCrossover — coherence-gated |H_A|=|H_B| crossings, two-point interpolation, all crossings listed, named refusals`

## Task C — `crossoverBandFit`: the complex-domain delay search (core, OFF; record §4, §10.4-10.7)

The lane's centre of gravity. **No unwrap anywhere** (`docs/dsp/2026-08-28-dual-fft.md` §6; record §4).

**Files.** Extend `core/include/rta/dsp/CrossoverFit.h` (the `BandFit`/`BandFitOptions` half, header stays ≤ 170); create `core/src/dsp/CrossoverBandFit.cpp` (**NEW**, ≤ 300) and `core/tests/test_crossover_band_fit.cpp` (**NEW**, ≤ 360); modify `core/tests/CMakeLists.txt` (source list `:11-63`), `core/CMakeLists.txt`. **Task B's two files are not extended** (the named split seam, verifier defect 11).

| # | case (record §10) | closed-form acceptance |
|---|---|---|
| C1 | exact case (§10.4), **tolerances derived from each other** | `H_A ≡ 1`, `H_B = e^{j(φ₀ − 2πf τ₀)}`, τ₀ = +3.7 ms, φ₀ = π, unit coherence, window 40–160 Hz (`Δf = 120 Hz`). `R` is evaluated at `τ*`, **not** at `τ₀`, so it is a function of the refinement error `dτ` and the two assertions must be one: over a window of width `Δf` with unit weights, `R(dτ) = |sin(πΔf·dτ)/(πΔf·dτ)|`, hence **`1 − R ≃ (πΔf·dτ)²/6`**. Assert `|tauSeconds − τ₀| ≤ dτ_max` with `dτ_max = tauGridSeconds/20 = 5e-8 s`, **and** `agreement ≥ 1 − 1.01·(π·120·dτ_max)²/6 = 1 − 5.98e-11` (the 1% is the discrete-bin vs continuum headroom). `|interceptRadians − π| ≤ 1e-9`. **Both residuals printed.** The record's `R = 1 − 1e-12` needs `dτ ≤ grid/154`, not `grid/20` — see ALIGN-R12 |
| C2 | the sign flips (§10.4) | same with τ₀ = **−3.7 ms**: `tauSeconds < 0` and `|tauSeconds + 3.7e-3| ≤ tauGridSeconds/20`. This is the fixture a flipped convention fails |
| C3 | `appliedDelaySamples` reconciliation (§10.5, ALIGN-R2) | same fixture with `appliedDelayDifferenceSamples = 48`, `sampleRate = 48000`: `tauSeconds` **unchanged** vs C1 to 1e-9 — and with the field left at 0, off by **exactly 48/48000 s = 1.0 ms** to 1e-9. **Made red once** by deleting the pre-rotation |
| C4 | refusals (§10.6) | all coherence absent → `AllBinsAbsent`, no fit; exactly one gated bin in the window → `TooFewBins` (a fit needs two distinct frequencies; one bin must never read `agreement == 1`); `octavesEachSide` so small the window is empty → `RangeEmpty` |
| C5 | cycle candidates (§10.7) | 40–160 Hz window, τ₀ = 9 ms: `cycleCandidates` **contains** a τ within `tauGridSeconds` of τ₀ and of `τ₀ ± 1/meanFrequencyHz`. Their `agreement` values are **printed, not asserted** — the fixture cannot know which is highest without asserting the implementation's own output |
| C6 | the weight is the summation cross-term, **against the closed form** | `|H_A| = 1` everywhere; `|H_B| = 1` on the lower half of the window and `ε = 1e-3` on the upper half; the two halves carry relative phases `φ_s` and `φ_s + d` with **`d = 2.0 rad`** exactly; equal bin counts. Record §4's `R_k` is **unit**, so the weighted circular mean is `arg(N/2·e^{jφ_s} + N/2·ε·e^{j(φ_s+d)})`, i.e. the intercept sits **`atan2(ε sin d, 1 + ε cos d) = 9.096757e-4 rad`** from `φ_s` — *not* within 1e-6 of it. Assert `|interceptRadians − (φ_s + 9.096757e-4)| ≤ 1e-9`. With the weights replaced by `1` the same algebra gives `atan2(sin d, 1 + cos d) = d/2 = 1.0 rad`, so assert that variant is `φ_s + 1.0` to 1e-9. The two differ by **0.999 rad**, so the weighting is load-bearing and visible. (The earlier draft's 1e-6 was unreachable by ~900× and the only green route was to copy what the code printed — verifier defect 2.) |
| C7 | `R` is bounded | over 200 fixed-seed random `H_A`, `H_B` and coherence draws: `0 ≤ agreement ≤ 1` always (triangle inequality — closed form, not a measurement) |

- [ ] **Accept:** OFF ctest count rises; zero `/W4`; `wc -l core/src/dsp/CrossoverBandFit.cpp core/tests/test_crossover_band_fit.cpp` reported (both < 400, aim < 300).
- [ ] **Mutation:** replace the circular mean with an arithmetic mean of `std::arg(R_k)` → C1's intercept at φ₀ = π (bins straddling ±π) collapses toward 0; revert. **Mutation 2:** drop `|H_A||H_B|` from `w_k` → C6 fails; revert.
- [ ] **Commit:** `feat(core): crossoverBandFit — complex-domain tau search, circular-mean intercept, bounded agreement, cross-term weights, cycle candidates, named refusals`

## Task D — the topology → offset table (app, OFF; record §3, §10.2) — **D5 carries the PR #3 ruling**

> **RULING FROM PR #3 (pending merge).** The sign convention of the shipped lookup — which side `arg(H_HP) − arg(H_LP)` is taken from, the odd-order `±90°` row assignment, and the order-4 attribution of record §13.1 — is **settled** by `docs/research/2026-09-15-l7-align-order4-probe.md` §8 (branch `l7/align-order4-probe`, PR #3, **not merged into `main` yet**): the `N·90°` identity holds exactly under this repo's convention at every order, so **record §3's table ships unchanged**, and L4a's order-4 reading is attributed to a cross-correlation peak-sign rule applied across two different passbands (`DelayFinder.cpp:34`), not to any convention. The builder cites the doc by § and notes in the commit message that PR #3 was unmerged at the time. **Three tasks consume the ruling, each with a sign fixture that can go red: D5, H9, I1b** (verifier defect 7 — the seam was never confined to D5).

**Files.** Create `app/src/measure/CrossoverTopology.h` (**NEW**, ≤ 120), `app/src/measure/CrossoverTopology.cpp` (**NEW**, ≤ 140), `app/tests/test_crossover_topology.cpp` (**NEW**, ≤ 300); modify `app/tests/CMakeLists.txt` — test source into `:11-56`, the `.cpp` into the compiled app-source list `:57-86`, and **both new files appended to the `measure_has_no_framework_deps` GLOBS on `:119`**; and **`app/CMakeLists.txt`'s `target_sources(rtatool_snapshot PRIVATE ...)` explicit list at `:169`** — `src/measure/CrossoverTopology.cpp` must be added there too, because Task I7's specimen draws I1's `expectedOffset` target line and that target has **no glob** (verifier defect 6).

**Fold in PR #3's file.** `core/tests/test_align_order4_identity.cpp` (on `l7/align-order4-probe`) already carries D1–D4 as five closed-form Catch2 cases and is registered in `core/tests/CMakeLists.txt`. When PR #3 merges, Task D **reuses** it and adds only D5 and D6; until then the builder writes D1–D4 in the app test and deletes the duplicate at the merge. Say which path was taken in the commit message.

Prototypes are built **in the test** from the Butterworth pole formula: `ButterworthDesign` (`core/include/rta/dsp/ButterworthDesign.h:41-52`) offers only `bandPass`/`bandPassZpk`/`pairIntoSections` — **there is no low-pass or high-pass** to borrow (the record's §1 note is correct).

| # | case (record §3, §10.2) | closed-form acceptance |
|---|---|---|
| D1 | the identity, all orders | `L(s) = 1/D_N(s)`, `H(s) = s^N/D_N(s)` on `s = jω/ω_c`: for N = 1..8 and 64 log-spaced ω, `wrapToPi(arg H − arg L − N·π/2) == 0` to **1e-9**. **Already locked** by `core/tests/test_align_order4_identity.cpp` case 2 on PR #3 — fold that file in, do not re-derive it (probe §7) |
| D2 | BW2 at fc, both ways (PR #3 case 4) | `|L+H|` at ω = ω_c **< 1e-12** (the **null**); `|L−H|` at ω = ω_c → `+3.0103 dB` (`20log10 √2`) to 1e-9. This is the row the station-1 research had **backwards** (record §1.1) |
| D3 | LR4 is allpass-summing (PR #3 case 3) | LR4 = BW2 cascaded with itself: `20log10|L+H| == 0` to **1e-9** at 64 log-spaced frequencies |
| D4 | BW3 sums flat either way (PR #3 case 5 covers the odd-order convention half) | `20log10|L+H| == 0` **and** `20log10|L−H| == 0` to 1e-9 at 64 frequencies — the row that makes "maximise the sum" a flat objective (record §6.2) |
| **D5** | **the shipped lookup — ruling from PR #3 (pending merge)** | `expectedOffset(...)` reproduces **record §3's table unchanged** for `LR{2,4,6,8}` and `BW{1..8}` × `ProcessorInversion{Yes,No,Unknown}`; `Unknown` returns `ambiguous == true` with both candidate lines (record §13.3). The sign convention is `arg(H_HP) − arg(H_LP) = +N·90°` (ALIGN-R14's A = HP contract), **as ruled by `docs/research/2026-09-15-l7-align-order4-probe.md` §8** — cite it by § in the test comment and note in the commit message that PR #3 was not yet merged. **Sign-distinguishing rows, so a transposed table goes red:** `expectedOffset(BW1,·).radians == +π/2` and `expectedOffset(BW3,·).radians == −π/2` differ by **π**; `LR2` with `ProcessorInversion::Yes` is `0` and with `No` is `π` |
| D6 | core stays topology-free | `grep -rniE "linkwitz|butterworth|crossover ?family|topolog" core/include core/src` returns only the pre-existing `ButterworthDesign` hits — no new one (record §9) |

- [ ] **Accept:** OFF ctest count rises; zero `/W4`; `measure_has_no_framework_deps` green with the two new files in GLOBS and the scanned count risen (paste it).
- [ ] **Mutation 1 (the one that actually flips an even order):** change D2's order-4 row to assert the **L4a claim** — that the order-4 pair nulls un-inverted — i.e. `CHECK(std::abs(lp + hp) < 1e-15)` at N = 4. It fails with the expansion **`1.41421356237309537 < 1e-15`** — `√2`, i.e. the order-4 pair sums **in phase** at +3.0103 dB. This is PR #3's own red step (probe §7); paste it and revert.
- [ ] **Mutation 2 (the shipped lookup):** swap question (c)'s two branches in `expectedOffset` for `LR-N, N ≡ 2 mod 4` (return `π` when the processor inverts and `0` when it does not) → D5 fails; revert.
- [ ] **NOT a mutation:** `(−s)^N/D_N` is `(−1)^N·s^N/D_N`, so at every **even** N it is the identity map and can say nothing about order 4. Record §3's sentence that such a processor "would reproduce" the order-4 wrong sign is wrong for that reason — filed as **ALIGN-R13**, not fixed here.
- [ ] **Commit:** `feat(app): the crossover topology table — one closed form (HP leads LP by N*90deg), question (c)'s inversion branch, prototypes proven in-test`

## Task E — `relativePolarity` ρ (core, OFF; record §7, §10.8-10.9)

**Files.** Create `core/include/rta/ir/RelativePolarity.h` (**NEW**, ≤ 120), `core/src/ir/RelativePolarity.cpp` (**NEW**, ≤ 180), `core/tests/test_relative_polarity.cpp` (**NEW**, ≤ 300); modify `core/tests/CMakeLists.txt`, `core/CMakeLists.txt`.

| # | case (record §10.8-9) | closed-form acceptance |
|---|---|---|
| E1 | identity | `b = a` → `rho == 1` to 1e-12, `sign == Sign::Positive`, `lag == 0` |
| E2 | scale invariance carries the sign | `b = −2a` → `rho == 1` to 1e-12, `sign == Sign::Negative`, `lag == 0` |
| E3 | shift | `b = a[n−D]`, D = 96 → `rho == 1` to 1e-12, `lag == 96` |
| E4 | noise, with the coherence-shaped bound | `b = a + n` at power ratio S ∈ {1, 10, 100}, fixed seed, window length `N_W`: `|rho − √(S/(1+S))| ≤ 3/√(N_W)` — the same form as `dual-fft.md` §7.3's `γ² = S/(1+S)`. **Residual printed beside the tolerance** |
| E5 | Cauchy–Schwarz | 200 fixed-seed draws: `0 ≤ rho ≤ 1` always. This is the property the three dead L4a gate variables lacked (`memory/a-threshold-read-off-a-grid-is-that-grids-floor.md`) |
| E6 | **no verdict ships** | the grep must match a **declaration**, never the comment that explains why there is none — the earlier word-grep went red against this plan's own prescribed header (verifier defect 1). Use `grep -nE '^[[:space:]]*[A-Za-z_][A-Za-z0-9_:<>]*[[:space:]]+[A-Za-z_]*([Tt]hreshold|[Vv]erdict|[Aa]ccept)[A-Za-z_]*[[:space:]]*(=|;)' core/include/rta/ir/RelativePolarity.h` → **no match**. Validated on this tree before shipping: it **matches** `DelayPolicy.h:64` (`DelayVerdict verdict = …`) and `:75` (`double acceptMultiple = 4.0;`), and **misses** `Polarity.h` and any `///` prose. Record §8's last line: until two grids agree, ρ is a figure, not a gate |
| E7 | the window is L4a's arrival | a fixture whose room tail is louder than its arrival: ρ computed over the arrival window is high; over the whole capture it is materially lower — **both printed**, the *difference* asserted to be > 0.1 (record §7: the tail is what pulls ρ down) |
| E8 | **the documented failure** (§10.9) | a synthetic **correctly-wired BW2 pair**: `sign == Sign::Negative` and `rho > 0.5`. Asserted **as the documented failure**, with a comment saying so and naming record §7, so a future session that "fixes" ρ into agreeing with the wiring goes red and reads why |

- [ ] **Accept:** OFF ctest count rises; zero `/W4`.
- [ ] **Mutation:** normalise by `max(E_a, E_b)` instead of `√(E_a E_b)` → E2 (`b = −2a`) falls to 0.5 and fails; revert. **Mutation 2:** take `argmax r(l)` instead of `argmax |r(l)|` → E2's `lag` moves off 0; revert.
- [ ] **Commit:** `feat(core): relativePolarity — bounded unwhitened same-system similarity over L4a's arrival window, rho and sign, NO verdict`

## Task F — the two-grid ρ threshold survey (tools, build-time; record §8) — **no number ships**

Record §8 is explicit and `memory/a-threshold-read-off-a-grid-is-that-grids-floor.md` is the reason: **two grids, two authors, no shared script, neither reads the other's code first.** This is one task with two *independently dispatched* builders.

**Files.** Create `tools/probe_rho_a.py` and `tools/probe_rho_b.py` (**NEW**, both with `argparse` — `memory/a-gen-script-runs-the-moment-you-invoke-it.md`; the existing `tools/probe_polarity_*.py` trio has none, do not copy that). Neither may use `output='ba'`, `zpk2tf`, `tf2sos`, `tf2zpk`, `sos2tf`, `np.poly(`, `signal.lfilter` or `freqz(b` — `filter_design_has_no_polynomial_form` scans `tools/*.py` (`core/tests/CMakeLists.txt:92`).

- [ ] **Axes, all swept** (record §8.2, none fixed at L4a's single values): SNR 10–40 dB; D/R −12..+12 dB with a synthetic exponential tail; offset 0–30 ms; family/order `butter`/`cheby1`/`ellip`/`bessel` orders 2–16 plus linear- and minimum-phase FIR; unit spread ±2 dB and ±10°; sample rates 44.1 / 48 / 96 kHz.
- [ ] **Cross-system cells** (record §8.4): every §3 row built as a pair, its ρ and sign recorded — so the order-4 question is answered by data as well as by the probe.
- [ ] **Measured, not chosen** (§8.3): the distribution of ρ over correct-sign cells and over wrong-sign cells; the candidate threshold is the lowest ρ above which **both grids** show zero wrong signs, **published with the fraction of correct cells it refuses** (L4a's zero-lie margin gate refused 74–88% of good boxes — that is the failure to check for).
- [ ] **Run both:** main-checkout venv (`memory/build-toolchain-on-this-machine.md` — the venv is the main checkout's, not a worktree's; `memory/the-venv-imports-acoustics-only-through-a-shim.md` if `acoustics` is reached for). Paste both tables into the lane report.
- [ ] **Ship nothing.** `relativePolarity` keeps returning ρ and the sign with no verdict (Task E6). The number, if the two grids agree, is a follow-up commit labelled **observed**, naming both grids, with a header comment reading "moving this invalidates two surveys; re-run both".
- [ ] **No commit of generated data** — measurements, not goldens. Commit only the two scripts: `test(tools): two independent rho surveys (two authors, no shared script) — distributions only, no threshold shipped`

## Task G — `VirtualTrace` and the one conversion boundary (app, OFF; record §5, §10.10)

**Files.** Create `app/src/trace/VirtualTrace.h` (**NEW**, ≤ 150), `app/src/trace/VirtualTrace.cpp` (**NEW**, ≤ 200), `app/tests/test_virtual_trace.cpp` (**NEW**, ≤ 280); modify `app/tests/CMakeLists.txt` (`:11-56`, `:57-86`, GLOBS `:119`).

| # | case | closed-form acceptance |
|---|---|---|
| G1 | round-trip is identity (§10.10) | a `Trace` whose magnitude spans `−120 .. +20 dB` and whose phase spans `(−π, π]`, through an **empty** chain: `|dB' − dB| ≤ 1e-4` and `|φ' − φ| ≤ 1e-6` at **every** bin including the `−120 dB` floor (`TransferSnapshot::kMagnitudeFloorDb`, `TransferEstimator.h:70`). **Max residual printed** |
| G2 | the floor is harmless in a sum | a `−120 dB` bin becomes `1e-6`; summed with a unit bin, `|20log10|1 + 1e-6|| ≤ 8.7e-6 dB` — the record's claim, asserted |
| G3 | **cannot become a Trace** | `static_assert(!std::is_constructible_v<rta::trace::Trace, const VirtualTrace&>)` and a detection-idiom `static_assert` that `library.add(virtualTrace, "", "")` is **ill-formed**. `TraceLibrary::add` takes `Trace` by value (`TraceLibrary.h:63`); the barrier is the type, not a naming convention (record §5, research D8) |
| G4 | **no `CaptureMeta`** | `static_assert` that `VirtualTrace` has no `meta()` member (detection idiom); `grep -n CaptureMeta app/src/trace/VirtualTrace.h` → no match |
| G5 | trust passes through unchanged | a `VirtualTrace` over one source with a delay+polarity+gain chain: the rendered coherence span is **bitwise equal** to the source's `field(Field::Coherence)` — delay, polarity and gain do not change how much a trace is trusted (record §5) |
| G6 | the sum's trust is not coherence | the two-source render exposes `summationTrust`; `grep -niE "\bcoherence\b" app/src/trace/VirtualTrace.h` shows only the pass-through read, never a field named `coherence` on the sum |
| G7 | biquad op via `FilterSpec` (ALIGN-R9) | a chain carrying `std::vector<rta::eq::FilterSpec>`: the rendered dB delta equals `Σ_i responseDb(spec_i, fs, f_k)` (`BiquadDesign.h:25`) to **1e-9** — one response path, not two |

- [ ] **Accept:** OFF ctest count rises; zero `/W4`; `measure_has_no_framework_deps` green with the new files in GLOBS.
- [ ] **Mutation:** give `VirtualTrace` an implicit `operator Trace()` → G3 fails to compile the `static_assert`, which **is** the test passing its own red step; revert and paste both states.
- [ ] **Commit:** `feat(app): VirtualTrace — the one dB<->complex conversion point, a preview that cannot reach the library, trust that is not coherence`

## Task H — the wizard state machine (app, OFF, JUCE-free; record §2, §9)

**Files.** Create `app/src/measure/AlignmentWizard.h` (**NEW**, ≤ 180), `app/src/measure/AlignmentWizard.cpp` (**NEW**, ≤ 300), `app/tests/test_alignment_wizard.cpp` (**NEW**, ≤ 340); modify `app/tests/CMakeLists.txt` (`:11-56`, `:57-86`, GLOBS `:119`). Device-free over `rta::platform::OutputEngine`, the way `app/tests/test_output_policy.cpp` already proves the output path with no sound card.

| # | case (record §2, §7, §9) | acceptance |
|---|---|---|
| H1 | **asked, never inferred** | feeding both captures into the wizard leaves `topology()`, `inversionAnswer()` and `highPassSide()` **bitwise unchanged**; `grep -nE "topology_|inversion_|highPassSide_" app/src/measure/AlignmentWizard.cpp` shows writes **only** from the four `answer*()` setters |
| H2 | the L7-OUT sequence, in order | `soloOutput(A)` → capture → `soloOutput(B)` → capture → compute → `routeOutput(A,true)` + `routeOutput(B,true)` → measured sum. After each step assert `engine.role(ch)` for **every** channel: exactly one `Routed` during each solo (StrictSolo, ALIGN-R6, owner decision 4), **both** `Routed` at the final step (L7-OUT §11: one signal on two outputs) |
| H3 | the non-stationary head is respected | the wizard does not begin a capture window before `engine.renderedSamples() >= 480 * fs/48000` (L7-OUT §11); made red by removing the wait — the capture then includes the ramp |
| H4 | refusals on mismatched captures | two traces whose `meta().sampleRate` differ → `Refusal::SampleRateMismatch`, no fit; `meta().fftSize` differ → `FftSizeMismatch`; `meta().channelRoles` differ → `ReferenceMismatch` (ALIGN-R7). Each named, none silent |
| H5 | the meta delay is passed once, **and A is the HP side** | `BandFitOptions::appliedDelayDifferenceSamples == metaB.appliedDelaySamples − metaA.appliedDelaySamples` exactly, where **A is the trace the operator named the high-pass side and B the low-pass side** (ALIGN-R14). Assert the mapping directly: with `highPassSide()` set to the *main*, the fit receives the main as `hA`; flipping `highPassSide()` swaps which trace lands in `hA` **and** flips the sign of `BandFit::tauSeconds`. The wizard applies **no** rotation of its own (ALIGN-R2) — `grep -c "exp(" AlignmentWizard.cpp` shows no phase rotation in the app layer |
| H6 | the cycle integer comes from the IR or from the operator | with both sources carrying a `Deconvolution`, the integer is `(originIndex_B − originIndex_A)` (`Deconvolver.h:34`); with either absent the state is `AskingCycle` and **no default is chosen**. `grep -nE "findDelayPhat|suggestDelay" app/src/measure/AlignmentWizard.cpp` → **no match** (record §4: PHAT on a band-limited source relocates the ambiguity, it does not resolve it) |
| **H9** | **the comparison against the asked topology, with a sign that can go red** (ruling from PR #3 §8) | a constructed pair whose true offset is exactly `+π/2` with a **known** HP side: with the operator answering `BW1`, `verdict().deltaRadians == 0` to 1e-9; with the operator answering `BW3` (expected `−π/2`) it is `π` to 1e-9. **Made red** by negating `expectedOffset`'s return, and again by transposing `highPassSide()` — either flips the delta and the test catches it. This is the fixture the earlier draft lacked: H consumed `expectedOffset` with no sign assertion at all (`memory/a-fixture-can-be-too-well-behaved-to-fail.md`) |
| H10 | an even-order discrepancy is evidence about the SYSTEM, not the convention (PR #3 §8) | on a pair whose band fit reads `agreement < 0.5`, the wizard reports the low `R` as "not a matched pair" and **does not** offer a topology change; `grep -nE 'topolog|family|order' app/src/measure/AlignmentWizard.cpp` shows no write outside the `answer*()` setters (the H1 property, re-asserted where it matters) |
| H7 | the §7 disagreement table is data | for a full-range pair where `findPolarity` answers and ρ answers, `polaritySignals()` returns both with their reasons; when they disagree the state is `AskingPolarity` — never a silent pick (research D7). For a **sub** pair, `findPolarity` returns `Refusal::BandTooHigh` (`Polarity.h:30`) and the signal is listed as refused, with `inverted` **never** promoted to fill the gap (record §7 table, row 2) |
| H8 | across the crossover, no time-domain sign is the answer — **stated behaviourally, because a grep cannot scope to "the verdict path"** (verifier defect 9) | ρ and `inverted` are returned flagged `Advisory` with the literal reason "sign undefined across a crossover". The mechanical test is an **invariance**: run the wizard twice on inputs that differ **only** in ρ's sign (negate one `Deconvolution`), and assert `verdict()` is **field-for-field identical** while `polaritySignals()` differs. That is red-able (feeding ρ's sign into the verdict breaks it) and expressible, which the earlier file-scoped grep was not — H7 requires `polaritySignals()` in the same file to read that sign |

- [ ] **Accept:** OFF ctest count rises; zero `/W4`; `measure_has_no_framework_deps` green with the new files in GLOBS, scanned count pasted.
- [ ] **Mutation:** let the wizard set `inversionAnswer()` from the sign of the fitted intercept → H1 fails; revert. This mutation **is** the forbidden move, and the test is what stops it landing.
- [ ] **Commit:** `feat(app): the alignment wizard — four asked questions, the L7-OUT solo sequence, named refusals, the polarity-signal table that asks instead of picking`

## Task I — the G18 crossover surface (app; model OFF, specimen ON; record §6)

**Files.** Create `app/src/view/CrossoverSurface.h` (**NEW**, ≤ 160), `app/src/view/CrossoverSurface.cpp` (**NEW**, ≤ 240), `app/tests/test_crossover_surface.cpp` (**NEW**, ≤ 280); modify `app/tests/CMakeLists.txt` (`:11-56`, `:57-86`, GLOBS `:119`), **`app/CMakeLists.txt`'s `target_sources(rtatool_snapshot PRIVATE ...)` explicit list at `:169`** — add `src/view/CrossoverSurface.cpp` **and** `src/measure/CrossoverTopology.cpp` (that target names every source one by one, there is no glob; without both, I7 fails at link with undefined symbols — verifier defect 6) — and, ON only, `app/src/dev/preview/PhaseAlignPreview.{h,cpp}` (ALIGN-R8). The model reuses `rta::view::BodeLayout` (`app/src/view/BodeLayout.h:22-37`) and stays JUCE-free.

| # | case (record §6) | acceptance |
|---|---|---|
| I1 | four traces and one asked line | the model exposes `H_A`, `H_B`, `H_Σ` and `arg(H_A·conj H_B)` over the fit window, plus a horizontal target at `expectedOffset(...)`. Point counts equal the trace point count (`Trace::pointCount()`); the relative-phase series is defined only inside the window and is **empty** outside it, not zero-filled |
| **I1b** | **the target line carries a sign that can go red** (ruling from PR #3 §8) | `targetRadians()` for `BW1` is `+π/2` and for `BW3` is `−π/2` — the two differ by **π**, so a flipped odd-order row goes red; for `LR2` it is `0` with `ProcessorInversion::Yes` and `π` with `No`. **Made red** by negating the value the model reads from `expectedOffset`. Like H9, this is the assertion the earlier draft lacked while still consuming the probe-dependent lookup (verifier defect 7) |
| I2 | the ghost and the measured sum | the pre-alignment `H_Σ` is retained as a separate series (the L6b reference-display pattern) and the measured sum is a third; changing a pending G11 op recomputes the predicted sum and leaves the ghost and the measured series untouched (bitwise) |
| I3 | the marks are marks | `+6.0206 dB` and the topology's designed sum (`0 dB` for LR-N and odd BW-N, `+3.0103 dB` for BW2 inverted) are exposed as reference **marks**. The structural test must match a **declaration**, not the vocabulary a crossover header is obliged to use — the earlier word-grep matched "low-pass", "high-pass" and "look" (verifier defect 5, demonstrated on `ButterworthDesign.h` and `Polarity.h`). Use `grep -nE '^[[:space:]]*[A-Za-z_][A-Za-z0-9_:<>]*[[:space:]]+[A-Za-z_]*([Pp]ass(ed|es)?|[Ff]ail(ed|s)?|[Aa]ligned|[Oo][Kk])[A-Za-z_]*[[:space:]]*(=|;)' app/src/view/CrossoverSurface.h` → **no match**. Validated on this tree: it misses `/// The low-pass side. Have a look.` and matches `bool passed = false;` |
| I4 | **no objective exists** | `grep -rniE "maximi[sz]e|optimi[sz]e|minimi[sz]e ?(ripple|sum)|argmax.*sum" app/src/view/CrossoverSurface.* app/src/measure/AlignmentWizard.*` → **no match**. The surface *shows* the sum; the operator reads it against the asked line (record §6) |
| I5 | a gap is a number, not a re-derivation | predicted-vs-measured is reported as a per-bin dB difference and a summary; the model carries **no** field that could express "topology inferred" — a structural assertion on the struct's members |
| I6 | `ui/` stays portable | `git diff --stat main -- ui/` is **empty**. No measurement vocabulary enters `az_ui` (CLAUDE.md module boundaries); the surface is drawn from `app/src/view/` primitives, and `az_ui` has no plot primitive to extend (`ui/az_ui/theme/Primitives.h` is dividers, wells and captions) |
| I7 | the specimen renders real data (ON) | `PhaseAlignPreview` is repointed from its canned arrays to a `CrossoverSurface` built on a synthetic BW4 pair. Then: `cmake --build build-l7align-on --config Release --target rtatool_snapshot --parallel` and `build-l7align-on/app/rtatool_snapshot_artefacts/Release/rtatool_snapshot.exe shots 1100 760` (via `cmd //c` from Git Bash) exits **0** and writes `shots/preview-phase.png`; **read the PNG** and report what is on it |

- [ ] **Accept:** OFF ctest count rises by the I1–I6 group; ON builds and the snapshot exits 0; zero `/W4` in both.
- [ ] **Mutation:** add a `bestDelayForLoudestSum()` helper to the model → I4's grep fails; revert. It is the one mutation this lane most needs to stay red.
- [ ] **Commit:** `feat(app): the G18 crossover surface — four traces, the asked target line, ghost and measured sum as marks, and no objective anywhere`

## Task J — prove the guards still GUARD (both configs; record §10.11)

- [ ] **GREEN, core framework guard.** `core_has_no_framework_deps` green with `VirtualProcessor.{h,cpp}`, `CrossoverFit.{h,cpp}`, `CrossoverBandFit.cpp`, `RelativePolarity.{h,cpp}` in scope; paste the risen `(N files scanned)` line. **RED once:** add `#include <juce_core/juce_core.h>` atop `CrossoverFit.h`, reconfigure, `ctest -R core_has_no_framework_deps` → paste the failure naming the file → remove it.
- [ ] **GREEN, coherence gate, and RED once in the shape that actually trips it.** `coherence_gate_is_not_bypassed` green with `summationTrust` present. The guard matches **assignments**, not declarations (`check_coherence_gate.cmake:44-47`), so the mutation must be both: rename the field to `coherence` **and** assign it as `result.coherence = ...` inside `core/src/dsp/VirtualProcessor.cpp`. Paste the failure; rename back. (Record §10.11 says "renaming that field" — renaming alone would **not** go red. Take to the orchestrator as a record correction.)
- [ ] **GREEN, app measure guard.** `measure_has_no_framework_deps` green with `CrossoverTopology.{h,cpp}`, `VirtualTrace.{h,cpp}`, `AlignmentWizard.{h,cpp}`, `CrossoverSurface.{h,cpp}` appended to the GLOBS on `app/tests/CMakeLists.txt:119`; paste the risen scanned count. **RED once:** a JUCE include atop `AlignmentWizard.h` → paste → remove.
- [ ] **GREEN, polynomial-form guard over the new Python.** `filter_design_has_no_polynomial_form` green with `tools/probe_rho_a.py` and `probe_rho_b.py` in scope; paste the risen scanned count.
- [ ] **GREEN, the snapshot target links.** `app/CMakeLists.txt`'s `target_sources(rtatool_snapshot PRIVATE ...)` list at `:169` carries `src/view/CrossoverSurface.cpp` and `src/measure/CrossoverTopology.cpp`; the ON build of `rtatool_snapshot` links with no undefined symbol (verifier defect 6). Paste the link line.
- [ ] **GREEN, RT guards untouched.** `output_render_has_no_rt_hazards` and (ON) `audioio_callback_has_no_rt_hazards`, `audioio_scoped_no_denormals_is_first` still green — this lane adds nothing to the audio callback; `git diff --stat main -- platform/` **empty**.
- [ ] **Lengths.** `wc -l` over every new file: all **< 400**, and `CrossoverFit.cpp`, `CrossoverBandFit.cpp`, `test_crossover_band_fit.cpp`, `AlignmentWizard.cpp`, `CrossoverSurface.cpp` reported against the aim of 300.
- [ ] **Untouched-by-construction.** `git diff --stat main -- core/include/rta/dsp/Biquad.h core/include/rta/dsp/BiquadResponse.h core/include/rta/eq/ ui/` **empty** — this lane reuses Wave 0 verbatim and adds nothing to `ui/`.
- [ ] **Mutation hygiene.** Delete the test `.exe` before every rebuild used for a mutation probe (`memory/mutation-testing-needs-the-exe-deleted-first.md`) — `cmake --build --target X` can log "-> X.exe" without relinking and false-PASS a mutation.
- [ ] **Commit:** `test: L7-ALIGN guards shown to guard — framework, coherence-gate (assignment-shaped), app measure, polynomial form`

---

## Numbers the builder must measure, not copy

Every figure below is a prediction to falsify. Measure the baselines **before** Task A on the branch point; if a measurement disagrees, the plan is wrong and the orchestrator hears about it.

| quantity | how |
|---|---|
| ctest OFF baseline `base_off` | `--clean-first` OFF build, `build-l7align`. **Do not copy 551** — EQ Task E/F may have landed since, and PR #3 adds five cases in `test_align_order4_identity.cpp` (the probe measured 551 → **556** on its own branch). Read it from ctest |
| ctest ON baseline `base_on` | `--clean-first` ON build, `build-l7align-on`. Do not copy 597 |
| OFF after A / B / C / D / E / G / H / I | re-read from ctest each time a `TEST_CASE` group lands |
| ON after I7 | re-read from ctest; plus the snapshot's exit code |
| `core_has_no_framework_deps`, `measure_has_no_framework_deps`, `filter_design_has_no_polynomial_form` scanned counts | each guard prints `(N files scanned)`; all three rise |
| B2's interpolation residual, C1's τ residual, E4's ρ residual, G1's round-trip residual | printed beside the tolerance in the test output |
| the fit window default (±1 octave) and `tauGridSeconds` | record §4 and §12 require these **measured across the §3 table** before they ship as defaults; report the sweep, or ship them as caller-supplied with no default at all |
| MSVC `/W4` warnings, and the CI matrix | grep the build log for `warning C` → 0; CI must be green on ubuntu **and** macos **and** windows |

## Build sequence and acceptance gate

1. **Task A** (five G11 ops, OFF) — everything downstream consumes them.
2. **Task B** (`spectralCrossover`, OFF).
3. **Task C** (`crossoverBandFit`, OFF) — the lane's centre.
4. **Task D** (topology table, app OFF) **∥** **Task E** (ρ, core OFF) — independent of each other. **Nothing blocks on the probe any more** (ALIGN-R11): PR #3 ruled the table unchanged. If PR #3 has merged, Task D folds in `core/tests/test_align_order4_identity.cpp` for D1–D4 instead of re-deriving them.
5. **Task F** (two ρ surveys) — runs any time after E; two separately dispatched authors.
6. **Task G** (`VirtualTrace`, app OFF) — needs A.
7. **Task H** (wizard, app OFF) — needs B, C, D, E, G.
8. **Task I** (surface model OFF, specimen ON) — needs D, G, H.
9. **Task J** (guards, both configs).

**Acceptance gate.** OFF and ON ctest both green at the measured counts; **0 `/W4`** in both; **CI green on all three OSes** (`.github/workflows/ci.yml:15`, the three-OS matrix at `-DRTA_BUILD_APP=OFF`; the PR-and-CI merge gate itself arrives on commit `63a3f9b`, which is **not an ancestor of this branch** — it lands via PR #1); every guard in Task J green with its scanned count risen and each shown red once in the shape that actually trips it; every new file < 400 lines; the snapshot writes a readable `preview-phase.png`; **`relativePolarity` ships with no verdict and no threshold**; and the three structural greps (H1, I4, D6) return no match.

## What L7-ALIGN does NOT include (deferred; record §12)

- **Bessel and any non-Butterworth-derived family** — its HP/LP offset is not constant in f; a new *family* is a station-2 pass, not a table edit (record §3, Rane Note 147).
- **The ρ threshold value.** Task F produces two distributions; the number is a later, labelled-**observed** commit naming both grids (record §8, §12).
- **The fit-window and τ-range defaults as constants baked into `core/`** — refused by the record. They ship as caller-supplied options with the proposed values measured and reported.
- **Where the wizard's output persists** (delay, polarity, gain per output). Processor state, not a trace; it belongs beside L7-OUT §12's member→output map in schema 4 (record §12).
- **Whether the LF fit runs on the fixed-FFT trace or the MTW trace** when both exist (record §12).
- **L7-DELAY's own trust figure displayed beside `R`** — this lane consumes `DelayEstimate`/`DelaySuggestion` as shipped and adds no field (record §12).
- **A per-processor table of which presets carry the LR2/LR6/BW2 inversion** — useful, a document, not code.
- **`MainComponent` wiring** for the wizard or the surface (ALIGN-R8), and any sequencer panel — no `CaptureSequencer` has a UI owner today (HANDOFF, OUT note 1).
- **EQ Task E/F.** Not this lane's; ALIGN does not block on it (ALIGN-R9).
- **`Biquad.h::maxPoleRadius`'s `std::max(0.0, NaN) == 0.0`** tech-debt (HANDOFF, Wave 0). `Biquad.h` is frozen; this lane must not touch it.

## Open, needs a human

1. **Ratify ALIGN-R11, and tick `HUMAN-QA-QUEUE.md:80`.** The order-4 attribution is **settled** by PR #3 (identity confirmed; the L4a reading attributed to a peak-sign rule across two different passbands). What is left is a decision, not a measurement: the queue entry says the probe blocks the whole Wave-3 build, and this plan discharges that for the whole lane. One line from the owner closes the entry. **NOT verified by the probe:** the exact band edges of the original L4a fixture (probe §9) — nothing here depends on them.
2. **A real sub/main pair (record §13.2).** Every check here is synthetic. Whether `R` stays high enough on a real room capture, and whether ±1 octave is the right window on a real 24 dB/oct pair, needs a person with a rack and a microphone. Same category as the EDT floor and the MTW fill question.
3. **Question (c)'s "unknown" branch (record §13.3).** Showing both candidate lines and letting the measured intercept sit on one *looks* like inferring. The record's reading is that it is not; it is the one place the ruling's edge is close, and the owner should see Task D5's `ambiguous` branch before it is built into the surface (Task I1).
4. **The ρ ruling's line in `HUMAN-QA-QUEUE.md`.** The ρ→L7-ALIGN fold is recorded at `:69-73`; record §13.4 asks the owner to confirm the wording says what was meant.
5. **ALIGN-R7's `channelRoles` proxy.** A string comparison standing in for "same reference" is honest but weak. If the owner wants it structural, that is a `CaptureMeta` field and a session-schema bump — out of this lane's scope, and the answer changes nothing else here.

## For the station-4 builder, first read

1. The record `docs/dsp/2026-09-06-l7-alignment-wizard.md` is binding; this plan implements its §9 boundary and flags **ALIGN-R1..R10**, which the orchestrator amends in the record **first**.
2. **`docs/research/2026-09-15-l7-align-order4-probe.md` §5, §7, §8** (PR #3, pending merge) — the order-4 ruling D5/H9/I1b cite, the five closed-form cases Task D folds in, and the two bindings it puts on the wizard: never read a topology sign off a correlation peak, and read an even-order discrepancy as evidence about the system.
3. `docs/dsp/2026-09-06-l7-output-path.md` §6, §11 — the output sequence Task H drives, and the five things a consumer may assume without re-deriving.
4. `memory/dual-fft-conventions.md` (the four sign choices, item 2 especially), `memory/a-threshold-read-off-a-grid-is-that-grids-floor.md` (why Task F is two grids), `memory/a-placeholder-for-an-absent-result-erases-its-state.md` (why A8/B5/C4 assert absence rather than a default), `memory/mutation-testing-needs-the-exe-deleted-first.md`.
5. Order is fixed by dependency: **A → B → C → (D ∥ E) → F → G → H → I → J**. One commit per task.
6. The failing test first, seen to fail (the missing `#include`), then the header, then the body. Paste the command and its output for every "done": a green build proves it compiles, not that the numbers are right (CLAUDE.md).
7. **If you find yourself writing a search over `|H_A + H_B|`, stop.** That is the one thing this lane exists to refuse (record §6). Report it to the orchestrator instead.
8. Every count here is a prediction you are expected to falsify if it is wrong.
