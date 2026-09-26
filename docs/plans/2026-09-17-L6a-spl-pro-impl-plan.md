# L6a — SPL-pro: the publish path, the block log, Ln, alarms, dose, calibration, report and viewer (G7 + G8)

> **For agentic workers:** REQUIRED SUB-SKILL `superpowers:test-driven-development` and `superpowers:executing-plans`. Steps are checkboxes; the failing test is written and *seen to fail* before the code. Station 5 (adversarial verify, a reviewer with **no Write tools**) is not optional.

*2026-09-17, lane L6a, station 3. Written from the decision record `docs/dsp/2026-09-16-spl-pro-l6a.md` (merged as PR #12 at `af8a9d0`, after two verifier rounds on that PR) and the station-1 research `docs/research/2026-09-16-l6a-spl-pro-station1-research.md`, **after opening every file they cite** — not after reading their description of those files. Worktree `.claude\worktrees\agent-a0d2db65ff6a4c6b8`, branched from `main` at `af8a9d0` and **merged up to `a02fb29`** (PR #14, L-API's own station-3 plan) on 2026-09-18. Every path below was existence-checked; a path marked **NEW** is not in the tree today, and a path marked **NEW-BY-L-API** is one that lane's station 4 creates. Twelve places where the record's spelling and the landed code disagree are in "Reconciliations"; the orchestrator amends the record, the builder does not silently re-decide.*

> **Revision 2, 2026-09-18** — all fourteen defects from the adversarial verification of PR #15 at `bf74e52` are folded in. The seven that were red-on-first-run or would have shipped a wrong number silently: **W0-A A4**'s literal (halves at `L`/`L+10` are `10log10(5.5)`, not `10log10(5.05)` — 0.371 dB apart, and A4b now names the case the wrong literal belonged to); **A2/A4**'s tolerance (1e-12 is below the measured round-off floor of a 48 000-sample naive sum, so 1e-9 with `test_leq.cpp:69-74`'s own reason); **`histogramBaseDb`** (a hard `-20.0` made every uncalibrated session's Ln permanently `BelowSpan` — it is derived from `referenceOffsetDb` now, and W1-A7 is the fixture that would have caught it); **`Gap`** (the bit was unrecoverable from the log and `t_iso` was permanently early after the first drop — `droppedSamples` now rides the block **in the padding**, so `sizeof(Block)` and §4's ring table are unchanged); **the allocation probe** (a global `operator new` cannot be copied per test file — **W0-B0** extracts the one that already exists); **the drain tap** (the named precedent sat *below* the `kMaxTransferFunctions` check, so route positions ≥ 8 would have gone silent with no `Gap` — the tap is now pinned between `AnalysisThread.cpp:254` and `:256`, with D1b testing route 8); and **flag membership** (which flags exclude from `combineBlocks` was unstated — `CalibrationInvalid` alone, with five fixtures and the reason). The other seven are corrections in place, and **SPL-R7's justification was refuted and rewritten** to rest on D1f's bitwise check alone.

> **Revision 3, 2026-09-18** — round 2 confirmed all seven material fixes by measurement (`sizeof(Block) == 40` **compiled**, the tap lines read, A4/A4b re-derived, the exclusion reasoning judged sound) and **station 4 is GO on W0-A**. Four more, all small, all fixed here and each **measured on this machine, not argued**:
> **N1** — the guard-delta row said "sum to 19" over a list that sums to **18**, of which one is automatic, so hand-appended is **17**. All three figures are now printed with the convention each one uses, and the Wave-4b case named. That was defect 7's shape reappearing inside the fix for defect 7.
> **N2** — D1b/D1c had been loosened to 1e-12 on an **unmeasured negative**. Measured: `pow(10, log10(2)) == 2.0` bitwise on g++ 16.1.0 (MinGW-W64 ucrt) *and* the MSVC ucrt, and `D == 100.0` bitwise for `Q ∈ {3,4,5,6}` in both directions. **Exactness restored**, with a note that a CI toolchain which refutes it is a finding to report, never a tolerance to widen.
> **N3** (pre-existing, missed in round 1) — W0-B B2 quoted `1 − e^{−t/τ}` and printed `−1.75 dB`, which is `10·log10(e^{−0.4})`, the **decay** term. The step is `10·log10(1 − e^{−0.4}) = −4.8190745912` — **3.0819 dB away**, and the 0.5 dB tolerance derived from the wrong number would have failed a correct implementation by about 4.3 dB. B2 is rewritten around the right formula with a float-derived tolerance, and **B2b** now asserts both terms by name so the complement cannot be substituted again.
> **N4** — one sentence in SPL-R8: the offset is applied to the value **and** to the base, where it cancels, so the un-offset `levelDb` is what reaches `add()` and A8's bin 1600 holds.

*This lane needs **no new golden vector**: every acceptance below is a closed-form identity or a clause-derived bound, per record §7's own "all exact, no golden vector".*

---

## 0. Hazard first: three things that will bite, and the one thing this lane is waiting on

**(1) The audio callback is out of reach, and that is structural.** `AudioIo::audioDeviceIOCallbackWithContext` is `juce::ScopedNoDenormals` plus exactly two calls, guarded by `audioio_callback_has_no_rt_hazards` and `platform/tests/check_callback_shape.cmake`. **No weighting filter, no detector, no block accumulator, no file, no socket goes near it.** The SPL chain runs on the analysis thread, off the hops `AnalysisThread` has already drained. A builder editing anything under `platform/` has left the lane.

**(2) The measurement ring has ONE read cursor, and it is already spoken for.** `rta::dsp::RingBuffer` keeps a single `readIndex_` (`core/include/rta/dsp/RingBuffer.h:55`, `:84`, `:100`); `AnalysisThread::drainPaired` is that cursor's owner under a routing plan (`app/src/measure/AnalysisThread.cpp:203-282`). Research §C4's instruction — "the SPL meter must sit on the **measurement channel's own drain**, so that a session with no reference still logs" — **cannot be implemented as a second reader.** It is implemented as a tap on the scratch buffers the existing drain already filled, which is the shape `locateBuffer_.feedHop` already uses at `AnalysisThread.cpp:269` ("from these exact scratch buffers, never a second read of the ring"). The consequence is real and must be reported, not hidden: see **SPL-R1**.

**(3) The honesty guard's regex will fire on HTML.** `core_makes_no_class_1_claim` matches `[Cc][Ll][Aa][Ss][Ss][ \t_-]*[01]` (`core/tests/check_no_conformance_claim.cmake:48`). Record §11 requires its scope to grow to the report templates. A CSS class named `class-1`, `class_1` or `.class1` is then a **false positive by construction**. (`class="tier-1"` is safe — `=` is outside the character class.) See **SPL-R10**; Task G shows it red on that exact shape.

> *A hazard that is NOT current, recorded so nobody re-reports it (`memory/a-public-issue-has-a-date-too.md`):* `no_std_atomic_over_shared_ptr` used to re-lex every scanned source as CMake, so a `\x` escape anywhere under `app/` aborted it (commit `3d4eca1`, 2026-09-16). It was rewritten from a macro to a function the same day and the trap is **gone** — `core/tests/check_no_std_atomic_shared_ptr.cmake:128-131` plus the fixture `core/tests/guard_fixtures/hex_escape_comment.h` prove it. An embedded HTML/CSS/JS blob in a header under `app/` is safe from it now. Do not bend a template to work around a fixed bug.

**What this lane is waiting on, and it is exactly one thing.** Wave 4b (the served viewer) is a **client** of L-API's surface: same server, same port, same `Host` check, same rate limit, same token (`docs/dsp/2026-09-16-remote-api.md` §12, merged as PR #11 at `a39a02e`). It **must not open a second socket, a second port, a second bind default or a second auth model.** It therefore starts only after L-API **station 4** has landed `ApiServer` (plan `docs/plans/2026-09-17-remote-api-impl-plan.md`, **merged to `main` as PR #14 at `a02fb29`** — Task **I** builds the server, Task **J** wires it in). **Its static-asset mount is NOT in L-API v1** — that plan lists "static assets served from the same origin" under *What this lane does NOT include* — so Gate 1 is two agreements, not one. Waves 0–3 and 4a wait on nothing.

---

## Defaults this plan TAKES, each one named

The record's §13 has ten owner questions; `docs/HUMAN-QA-QUEUE.md` marks **Q1, Q2 and Q8** as the three that set station-3 scope. A plan that left them blank is a plan nobody can build, so each is taken as the record's own proposal, named, and marked. **A default is a decision the owner may overturn in one sentence; it is not a decision this plan is hiding.**

### The three scope-setting questions

| § | question | **SCOPE DEFAULT taken — owner may flip** | flipping costs X |
|---|---|---|---|
| **§13 Q1** — the 3.0103 dB seam | does the broadband SPL readout adopt the mean-square convention beside sine-referenced RTA bands, or do the bands move? | **record §13 Q1's own proposal: convert once at the meter seam, label both, change nothing that exists.** `Snapshot`'s SPL block is mean-square referenced (`10·log10(mean p²) + referenceOffsetDb`, IEC 61672-1 cl. 3.9's definition, what `meter::Leq` already implements); `bands` / `spectrumDb` stay sine-referenced dBFS (`Levels.h:22`, `:44`). The seam is **one** number in **one** place and W0-E is a closed-form test that a full-scale sine reads the same dB through both paths once the offset is applied | **a schema bump plus a golden regeneration, not a constant edit.** Moving the bands means `kFullScaleSineOffsetDb` (`Levels.h:22`) goes to `0.0`; every dBFS number on screen and **in every stored `Trace`** shifts by 3.0103 dB; `core/tests/golden/*.txt` rows that pin dB move; `rtatool_snapshot`'s eight PNGs change; `test_levels.cpp` / `test_synthetic.cpp` identities invert; and a session written under one convention and read under the other is silently 3 dB wrong unless `SessionCodec.h:30`'s `kSchemaVersion` rises to **4** with a `levelConvention` key. Approximately one extra wave, and it must land **before** anything else in this lane |
| **§13 Q2** — build the calibration flow inside L6a? | it is not in the L6a row of the master plan | **record §13 Q2's own proposal: build it.** It is **Wave 3**, and Wave 3 alone. `CaptureMeta::calibrationOffsetDb` (`Trace.h:39`) exists today with nothing that can honestly set it, and no report can carry ISO 1996-2 cl. 5.2's start/end pair without it | **three tasks disappear and nothing else changes.** Waves 0–2 take the calibration offset **as data** — a `double referenceOffsetDb` on `SplConfig` — precisely so that Wave 3's removal is a deletion, not a rewrite. What is lost if cut: `flags.calibrationInvalid` is never set; report item 3 (§9) prints *"calibration check not performed"* instead of the pair and the clause; the one-line `meter::calibrationOffsetDb` still ships in W0-A because W0-E's seam test uses it |
| **§13 Q8** — does the web viewer ship in L6a at all? | it is last in the build order and gated on an unrun Chrome test | **record §9's own build order: it ships, LAST, and gated.** Split into **4a — the report** (frozen payload, no server, no network, NOT gated) and **4b — the viewer** (same renderer, payload fetched from L-API). 4b starts only when (i) L-API station 4 has landed `ApiServer` (its Tasks I and J) **and** the static-asset mount has been agreed — it is explicitly *not* in L-API v1 — and (ii) the owner has run the named Chrome LNA test below and reported what they saw | **Wave 4b disappears whole; 4a and everything before it are untouched.** The record §9 already grants the fallback in its own words: the §12 constraint-2 rounding obligation is then **"recorded as untested for the viewer"**, and that sentence goes in the report. Cost of the flip: one wave deleted plus one sentence added. This is why the report is 4a and not part of 4b — the artefact a venue actually reads must not be cut along with the socket |

### The seven that do not set scope, taken so the waves are buildable

| § | question | default taken (all are the record's own proposal) |
|---|---|---|
| §13 Q3 | which Ln set ships | **six slots, defaulting to L1 / L5 / L10 / L50 / L90 / L95**, percentages settable — the Larson Davis `NUM_LNS = 6` shape |
| §13 Q4 | which NIOSH convention | **`q = 3/log10(2)`, reproducing Table 1-1**, and **the formulas, not the printed tables**. TWA printed with NIOSH's own `10·log10(D/100) + 85`, with the sentence that the two tables imply different exchange constants. The 99 dBA row ships as a **named, excluded erratum** |
| §13 Q5 | alarm window | **sliding**, with the regulated window and the operator's proxy window both shown |
| §13 Q6 | tamper-evidence | **a hash over the log segments, printed in the report** (§9 item 9). No signature — a key needs a story about where it lives |
| §13 Q7 | retention and rotation | **default log span 8 h** (§4's own table: 28 800 blocks, 1.099 MiB — "a show plus load-in"); **segment = one hour of blocks**, a number an operator can state rather than a byte count; **the app never deletes** |
| §13 Q9 | buy ISO 1996-2:2017 | **not needed to build.** §9's report list is assembled from market practice and the report says in its own words that it does not claim clause-13 conformance |
| §13 Q10 | who fixes "Table 2" in the guard | **this lane, as a named task** — Task G2. It is code, so it belongs to station 4 and not to this docs-only PR |

---

## Global constraints

- SPDX header `// SPDX-License-Identifier: AGPL-3.0-or-later` on every new file.
- **`core/` never includes JUCE/Qt/a device API** (`core_has_no_framework_deps`, `core/tests/CMakeLists.txt:93-97`). Every new `core/meter` type is spans and numbers in, numbers out — no clock, no file, no policy (record §11).
- **Nothing in `core/` knows what a jurisdiction is.** The dose presets are `(L_c, T_c, q, threshold)` **values** assembled in `app/`; `core/` holds the accumulator and never an `enum class Preset { Niosh, Osha }`. A builder who puts a regulator's name in `core/` has moved the record's own boundary (§11).
- **The JUCE-free half of `app/` stays JUCE-free** (`measure_has_no_framework_deps`, `app/tests/CMakeLists.txt:280-284`). Its `GLOBS` is an **explicit list**, not a directory glob, and `memory/core-must-not-include-frameworks.md` records that a misspelled entry silently reduces coverage. **Every new JUCE-free file in this lane is appended by hand**, and the guard's own `OK (N files scanned)` is the only proof it was. The list already ends with `${CMAKE_CURRENT_SOURCE_DIR}/*.h;*.hpp`, so new **test** headers raise N without an edit — the accepted figure is therefore always "N rose by exactly the number of files this task added", **read from the guard's output, never predicted here**.
- **Hard cap 400 lines, aim 300**, headers too. Per-file budgets are in each task. The HTML/CSS/JS template is the file most likely to breach it — it is split across three headers before it is written, not after.
- **`RTA_BUILD_APP=OFF` is where the proof lives.** CI builds OFF on three OSes (`.github/workflows/ci.yml`) and `app/tests` is registered outside the `RTA_BUILD_APP` guard (root `CMakeLists.txt:72`). The block accumulator, the histogram, the dose, the headroom, the alarm latch, the ring, the log format, the JSON, the report renderer and every guard are all provable there. **Only the drain feed (W0-D), the pane component and the specimen (W2-E) and the API route (W4b) are ON.**
- **Never assert a value the implementation produced** (CLAUDE.md verification standard). The sources of truth here are: a closed-form identity derived in the record; a clause quoted with its number; and a regulator's own printed formula evaluated at a stated point. There is **no golden vector in this lane** and none may be added — if a builder finds themselves writing `tools/gen_spl.py`, stop and report it. (And `memory/a-gen-script-runs-the-moment-you-invoke-it.md`: the `tools/gen_*.py` family is a *write* to the repo, not a program with a CLI.)
- **Float32 awareness** (`memory/float32-fft-precision.md`): the block energy, the dose and the headroom run in `double` on constructed fixtures → `1e-12`..`1e-9`. Anything crossing a `float` field in `Snapshot` (the published `valueDb`, `maxFastDb`, `peakCDb`) uses a float-shaped tolerance with the residual **printed beside it**.
- Build dirs **`build-l6a`** (OFF) / **`build-l6a-on`** (ON), **Visual Studio generator, never Ninja** (`memory/a-misconfigured-build-goes-99-percent-of-the-way.md`). MSVC `/W4` is the truth: **0 `warning C`**.

```
cmake -S . -B build-l6a -G "Visual Studio 18 2026" -A x64
cmake --build build-l6a --config Release --parallel
ctest --test-dir build-l6a -C Release --output-on-failure
```

For the ON config append `-DRTA_BUILD_APP=ON -DRTA_JUCE_PATH="D:/DEV CAVE EP3/PROJECT005-AZ-handsfree/external/JUCE"` and use `build-l6a-on`.

- **CI is the merge gate** (`docs/GIT-WORKFLOW.md` rule 3). Every core and JUCE-free-app file must compile on GCC and Clang, not only MSVC: no `__declspec`, no MSVC-only pragma, no test that assumes Windows path separators, and no reliance on MSVC's `<charconv>` floating-point extensions beyond what `SessionCodecDetail.h` already uses. **GitHub Actions is currently billing-blocked at the account level** — that does not relax the rule, it means the OFF tally is measured on this machine and pasted, and the verifier re-measures.

## Namespace decision

**Pure metering math → `rta::meter`** (joining `Detector`/`Leq`). **The chain, the ring, the alarm configuration and the log → `rta::measure`.** **The log/JSON/HTML text → `rta::splexport`**, a sibling of `rta::eqexport` (`app/src/export/EqTextExport.h:22`) and `rta::firexport`, because that directory has already established the split this lane needs: a pure formatter in a JUCE-free header, a thin writer that touches disk. **The pane geometry → `rta::view`.** No new namespace beyond `rta::splexport`, and that one follows a shipped precedent rather than inventing a level.

---

## The API this lane builds (record §2–§8, §10, §11; names are this plan's)

```cpp
// core/include/rta/meter/Block.h            NEW   (rta_core, namespace rta::meter)

/// The §2 block. The record's field list is 36 bytes and 40 as laid out,
/// because the `double` forces 4 bytes of padding after the leading
/// uint64 + uint32. **`droppedSamples` occupies exactly that padding** (SPL-R1,
/// SPL-R2, defect 5), so the block gains a field and `sizeof(Block)` stays 40:
/// §4's ring table is UNCHANGED. The layout is static_assert'ed, not assumed
/// (W0-A1).
struct Block {
    std::uint64_t blockIndex     = 0; ///< THE CLOCK. No wall time in any arithmetic.
    std::uint32_t blockSamples   = 0; ///< the divisor every mean in this block used
    std::uint32_t droppedSamples = 0; ///< samples the bus LOST before this block closed.
                                      ///< In the former padding, so the block is still 40 B.
                                      ///< A reader sums the column to reconstruct elapsed time.
    double        sumSquares     = 0.0; ///< the energy; leqDb = 10log10(sumSquares/blockSamples)
    float         maxFastDb      = static_cast<float>(kLevelFloorDb);
    float         maxSlowDb      = static_cast<float>(kLevelFloorDb);
    float         peakDb         = static_cast<float>(kLevelFloorDb);  ///< C-weighted, SAMPLED
    std::uint32_t flags          = 0;   ///< BlockFlag bitmask
};

enum class BlockFlag : std::uint32_t {
    Overload = 1u << 0, UnderRange = 1u << 1, Dropped = 1u << 2,
    CalibrationInvalid = 1u << 3, Gap = 1u << 4,   ///< Gap: see SPL-R1
};

/// WHICH FLAGS EXCLUDE A BLOCK FROM combineBlocks' MEMBERSHIP -- one decision,
/// and it changes every published Leq during a loud show, so it is stated here
/// rather than guessed in code (defect 14; W0-A A9-A13 is a test per flag).
///
///   CalibrationInvalid  EXCLUDES.  Its dB are referenced to an offset ISO
///                       1996-2 cl. 5.2's own discard rule says must not be
///                       trusted, and that is the ONE published normative
///                       criterion this lane has (§8).
///   Overload            INCLUDES.  A clipped waveform carries LESS energy than
///                       the signal that clipped it, so dropping the block
///                       removes the loudest moment of the show and biases the
///                       compliance number in the operator's favour -- the same
///                       "discards the loudest moment" failure §2 rejects when
///                       it refuses instantaneous sampling.
///   UnderRange          INCLUDES.  Excluding floor readings biases Leq UP.
///   Dropped, Gap        INCLUDE.  The energy they carry is real over the
///                       samples that existed, and `blockSamples` is the honest
///                       divisor for it. What is lost is TIME, and that is what
///                       `droppedSamples` records.
///
/// Every one of the five is COUNTED and REPORTED either way (§9 item 8).
[[nodiscard]] constexpr bool excludesFromWindow(BlockFlag f) noexcept;   ///< true only for CalibrationInvalid

/// Accumulates one block: sum of squares on the weighted stream, the two
/// detector maxima, the C-peak, and the overload run carried ACROSS calls
/// (SPL-R4). Fixed `blockSamples` set at construction; `poll()` returns a
/// completed Block and rearms, or nullopt.
class BlockAccumulator { /* push(span<const float> weighted, span<const float> cWeighted), poll() */ };

/// §3, exact: Leq(W) = 10log10( sum sumSquares_i / sum blockSamples_i ) + offset.
/// Recomputed over the CURRENT membership every call -- never a running
/// subtraction, because membership is mutable (record §3).
struct WindowResult { std::optional<double> leqDb; double maxFastDb, maxSlowDb, peakDb;
                      std::uint64_t blocks, samples; double bufferFill; };
[[nodiscard]] WindowResult combineBlocks(std::span<const Block>, double sampleRate,
                                         double referenceOffsetDb, std::uint64_t windowBlocks);

/// §8, one line, one identity: L_cal - L_meas.
[[nodiscard]] double calibrationOffsetDb(double calibratorLevelDb, double measuredLevelDb) noexcept;
```

```cpp
// core/include/rta/meter/LevelHistogram.h   NEW   (rta_core, namespace rta::meter)
/// §5. 2000 bins of 0.1 dB from `baseDb`, plus TWO out-of-span counters.
/// 2002 * 4 = 8 008 B, allocated once, constant for any session length.
class LevelHistogram {
public:
    static constexpr double  kBinWidthDb = 0.1;
    static constexpr std::size_t kBinCount = 2000;   ///< Larson Davis NUM_STAT_BINS
    explicit LevelHistogram(double baseDb = -20.0);
    void add(double levelDb) noexcept;               ///< time-weighted level, per ISO 1996-1 3.1.3
    /// Ln over bin CENTRES, the project's existing percentileLevelDb convention.
    /// ABSENT (with a reason) when the rank falls outside the span -- never clamped
    /// to the bottom of the span (record §5; memory/a-placeholder-...).
    [[nodiscard]] std::optional<double> percentileDb(double n) const;
    [[nodiscard]] LnAbsence lastAbsence() const noexcept;  ///< None|BelowSpan|AboveSpan|NoData
    void merge(const LevelHistogram& other);         ///< a[i] += b[i]; the composability §5 buys
};
```

```cpp
// core/include/rta/meter/Alarm.h            NEW   (rta_core, namespace rta::meter)
/// §6's closed form. Returns ABSENT when the bracket is <= 0 -- "the window is
/// already lost" is a fact about the arithmetic, not a threshold.
[[nodiscard]] std::optional<double> headroomDb(double windowSeconds, double elapsedSeconds,
                                               double elapsedLeqDb, double limitDb) noexcept;
/// Two-state latch on a windowed value. NO hysteresis, NO debounce -- record §6:
/// no surveyed product publishes one, so shipping one would be a number this
/// project originated and nobody can check.
class AlarmLatch { /* update(double windowedDb, double limitDb) -> Transition{None,Fired,Cleared} */ };
```

```cpp
// core/include/rta/meter/Dose.h             NEW   (rta_core, namespace rta::meter)
/// §7. `q` is a base-10 DENOMINATOR carried as a number, never a boolean
/// "3 dB or 5 dB". IEC 61252:2025 Formula (7) at q = 10; Formula (8) at
/// q = Q/log10(2). No regulator's name appears in this file (see Global
/// constraints).
struct DoseSettings { double criterionLevelDb, criterionSeconds, q, thresholdDb; };
class Dose {
public:
    explicit Dose(DoseSettings);
    void addBlock(double blockLeqDb, double blockSeconds) noexcept;  ///< below threshold contributes EXACTLY 0
    [[nodiscard]] double percent() const noexcept;
    [[nodiscard]] double projectedPercent() const noexcept;  ///< D_now * (T_c / T_elapsed); VENDOR convention
    [[nodiscard]] double twaDb() const noexcept;             ///< q*log10(D/100) + L_c
    [[nodiscard]] double secondsBelowThreshold() const noexcept;  ///< reported, NEVER folded in
};
/// L_EX,8h = L_Aeq,T + 10log10(T/8h). ENERGY. It does NOT take q (record §7).
[[nodiscard]] double exposureLevelDb(double leqDb, double seconds) noexcept;
```

```cpp
// app/src/measure/SplConfig.h               NEW   (rta::measure, OFF, framework-free)
struct SplMetricSpec { std::string id; rta::dsp::WeightingType weighting; 
                       rta::meter::TimeWeighting detector; std::uint64_t windowBlocks; };
struct SplAlarmSpec  { std::string metricId; double limitDb; std::uint64_t windowBlocks; };
struct SplConfig {
    double        blockSeconds        = 1.0;    ///< §2 default; settable. 1 s divides 3 s / 60 s / 5 min / 60 min
    double        referenceOffsetDb   = 0.0;    ///< DATA. Wave 3 sets it; without Wave 3 the operator types it
    bool          calibrated          = false;  ///< SPL-R5: the LevelUnit mapping happens at the log header
    double        logSpanSeconds      = 8 * 3600.0;   ///< §13 Q7 default; §4's table sizes the ring from it
    std::uint64_t segmentBlocks       = 3600;         ///< §13 Q7 default: one hour of blocks
    /// DERIVED, never a free constant (defect 4;
    /// memory/a-default-must-be-run-through-the-gate-it-feeds.md).
    /// baseDb = measure::kLevelFloorDb + referenceOffsetDb = -120.0 + offset.
    /// Uncalibrated (offset 0) the span is [-120, +80) dB re FS, which contains
    /// every reading the Q1 scope default can produce -- a full-scale sine at
    /// 0.0 dBFS lands at bin 1200 of 2000 -- 60 % up, 80 dB of headroom
    /// above it. Calibrated with a typical
    /// offset of 100 dB it becomes [-20, +180), which is EXACTLY record §5's
    /// own default recovered rather than contradicted. Fixed at session start
    /// with the offset, because §10 forbids a setting changing mid-log.
    double        histogramBaseDb() const noexcept { return kLevelFloorDb + referenceOffsetDb; }
    std::array<double, 6> lnPercents  {1.0, 5.0, 10.0, 50.0, 90.0, 95.0};   ///< §13 Q3 default
    std::vector<SplMetricSpec> metrics;
    std::vector<SplAlarmSpec>  alarms;
    std::array<rta::meter::DoseSettings, 2> dose;     ///< TWO accumulators, always (§7)
};

// app/src/measure/SplMeter.h/.cpp           NEW   (rta::measure, OFF, framework-free)
/// The per-channel chain: Weighting(A) -> BlockAccumulator + Detector(F) +
/// Detector(S); Weighting(C) -> the peak path. Fed HOPS (§0 hazard 2). Emits
/// Blocks on a SAMPLE-COUNT clock (§2). No allocation after construction.

// app/src/measure/SplHistory.h/.cpp         NEW   (rta::measure, OFF, framework-free)
/// §4: one ring of Blocks per logged channel, sized ONCE from logSpanSeconds,
/// never grown. Rolls; does not wrap silently. Stores NUMBERS, not pixels
/// (docs/dsp/2026-08-29-display-layer-l5c.md §7 item 2, :340-347). Carries the marker list:
/// kinds `alarm`, `overload`, `note`, `reset` (Smaart's taxonomy, §6).

// app/src/measure/Snapshot.h                MODIFY -- adds ONE optional field
struct SplMetricReading { std::string id; ...; float valueDb; float leqBufferFill; };
struct SplBlockView {
    std::uint64_t blockIndex; std::uint32_t blockSamples; double sampleRate;
    double referenceOffsetDb; bool calibrated;            ///< SPL-R5: no enum here
    std::vector<SplMetricReading> metrics;
    float maxFastDb, maxSlowDb, peakCDb; std::uint32_t flags;
    std::vector<SplAlarmReading> alarms;                  ///< `state` is SERVER-computed (§9)
    std::array<double, 2> dosePercent; std::array<double, 2> doseProjected;
    std::array<std::optional<double>, 6> lnDb;            ///< absent, never clamped
};
std::optional<SplBlockView> spl;   ///< absent until a logging session is running
```

```cpp
// app/src/export/SplLog.h                   NEW   (rta::splexport, OFF, framework-free)
/// §10. Pure content; writing to disk is the caller's job -- the exact split
/// FirExport.h / FirTextWriter.cpp already ships.
[[nodiscard]] std::string logHeader(const measure::SplConfig&, double sampleRate,
                                    std::uint64_t startedAtUnixMs);   ///< "# key=value" lines
[[nodiscard]] std::string logRow(const rta::meter::Block&, double referenceOffsetDb);
/// Discards a truncated final line and REPORTS how many bytes it discarded.
/// No atomicity claim is made, because none can be made portably (§10).
struct LogReadResult { std::vector<rta::meter::Block> blocks; std::size_t bytesDiscarded; };
[[nodiscard]] LogReadResult readLog(std::string_view text);
[[nodiscard]] std::string logJson(...);   ///< IDENTICAL field names; one schema, two encodings
```

```cpp
// app/src/export/SplReport.h/.cpp           NEW   (rta::splexport, OFF, framework-free)
/// §9. ONE renderer. Emits a SELF-CONTAINED HTML document: inline CSS, inline
/// SVG, no external asset, no <script src>. The REPORT freezes the payload in;
/// the VIEWER (Wave 4b) serves the same document with the payload fetched.
/// "PDF" is the browser's print-to-PDF of this file.
[[nodiscard]] std::string renderReport(const ReportPayload&);   ///< frozen
[[nodiscard]] std::string renderViewerShell();                  ///< same doc, payload fetched
```

---

## Reconciliations made while planning (take to the orchestrator; do not silently re-decide in code)

- **SPL-R1 — §C4's "the SPL meter must sit on the measurement channel's own drain" is not implementable as written, and the consequence is a real defect that must be reported.** `rta::dsp::RingBuffer` has one `readIndex_` (`RingBuffer.h:55,84,100`); `drainPaired` already owns it under a routing plan and computes `hops = min(refAvailable/hop, every route's measurementAvailable/hop)` (`AnalysisThread.cpp:219-229`), so **the measurement channel advances only when the reference can advance with it.** *Reconciled:* the SPL feed taps the scratch buffers both drains already fill (the `locateBuffer_.feedHop` precedent, `AnalysisThread.cpp:269`). In the **unrouted** path (`drainRole`, `:284-314`) the measurement channel drains on its own and §C4's requirement is met exactly. In the **routed** path it is not, and the block therefore carries a `Gap` flag driven by the delta of `CaptureBus::dropCount(channel)` (`CaptureBus.h:168`), so a reference-dry routed session produces a log that **says it stalled** instead of a log that silently lies. **And the `Gap` bit alone is not enough**: the counter is in memory and never reaches the file, so the block carries `std::uint32_t droppedSamples` in the four bytes the `double`'s alignment was already wasting (`sizeof(Block)` stays 40, §4's ring table does not move) and the log carries it as a column. A third party holding only the text can then reconstruct the true sample position of every block as `Σ(blockSamples + droppedSamples)`. A second read cursor on the ring is a `platform/` change; it is named in "does not include" and not built here. W0-A A8, W0-D D1b/D3 and W2-C C8 are the fixtures.
- **SPL-R2 — `Snapshot` deliberately carries NO timestamp, and §9's payload wants `t_iso`.** `Snapshot.h:198-201`: "Deliberately carries NO timestamp: two snapshots built from the same input must compare equal field-for-field, which is what makes `makeSyntheticSnapshot` deterministic and `rta-view.png` reviewable as a byte-for-byte diff". *Reconciled:* `SplBlockView` carries `blockIndex`, `blockSamples` and `sampleRate` and **no wall clock**. `t_iso` is minted at the log writer and at the API serialiser — which is §2's own rule ("a wall clock is recorded once per block as metadata for the human, and is never an input to any mean") placed where it does not break the snapshot-equality property eight PNG fixtures depend on. **The formula is `startedAtUnixMs + Σ_{i≤n}(blockSamples_i + droppedSamples_i)/fs`, and it is labelled as a reconstructed elapsed time rather than a wall-clock reading.** An earlier revision used `startedAtUnixMs + blockIndex·blockSamples/fs`, which SPL-R1's own `Gap` makes wrong: after one 12 000-sample drop every later timestamp is early by 0.25 s, permanently, for the rest of an eight-hour session — and §9's identification block prints those times. **The two reconciliations only close together**, which is why R1 now carries the field and R2 carries the formula.
- **SPL-R3 — §11 lists the new `core/meter` types but never says what happens to `rta::meter::Leq`, and the first `app/` caller is the one who trips it.** `Leq::process` is `noexcept` and does an unbounded `history_.push_back` (`Leq.cpp:49,64`); `percentileDb` copies-and-sorts and then calls `percentileLevelDb`, which copies and sorts **again** (`Leq.cpp:22,105-110`). *Reconciled:* **no `app/` code in this lane constructs a `rta::meter::Leq`.** The publish path is `Weighting` → `BlockAccumulator` + `Detector`; the percentiles are `LevelHistogram`. `Leq` is left exactly as it is, still unreached outside its own tests — an unreached defect is not a shipped one, and repairing it is a separate change nothing here blocks on. The lane adds **one** thing to it: W1-B's three-line `sumSquares()` accessor, which is additive and is what "`sumSquares` beside `leqDb`" means for that class.
- **SPL-R4 — `hasOverload` explicitly does not carry a run across calls, and a block boundary is a bigger version of the hop boundary it warns about.** `OverloadDetector.h:22-27`: "A run does not carry across separate calls: each call sees only the span it is given, so a hop boundary can split a run in two (record §8's capture-window latch is what stitches runs across hops, one layer up in `app/`)." *Reconciled:* `SplMeter` carries the consecutive-sample run count across `push()` calls and across block boundaries, and **that latch** — not a per-hop `hasOverload` call — is what sets `BlockFlag::Overload`. `hasOverload` remains available as the per-hop fast path. W0-B3 is the fixture whose three-sample run straddles a block boundary; without the carry it reads clean.
- **SPL-R5 — two spellings of one two-valued fact.** `rta::trace::LevelUnit{DbFs,DbSpl}` already exists (`Trace.h:20`), is stored on every `Trace` (`:39,:40`) and is round-tripped by `SessionCodec.cpp:31` / `SessionDecode.cpp:109-116` (which already refuses an unrecognised unit rather than defaulting). *Reconciled:* `SplBlockView` carries `double referenceOffsetDb` + `bool calibrated` and **no enum**, because `Snapshot.h` is the header every consumer includes and pulling `trace/Trace.h` into it puts the trace vocabulary into the API serialiser and the snapshot tool for the sake of one enum. The `LevelUnit` mapping happens once, at the log header and the report. **Moving `LevelUnit` into `measure/Levels.h` so there is exactly one enum is the better long-run answer**; it touches the session codec and is out of this lane unless the orchestrator says otherwise.
- **SPL-R6 — §10's header example puts several `key=value` pairs on one line; the shipped writer emits one per line.** `SessionCodecDetail.h:53` `writeLine(out, key, value)` appends `key`, `'='`, the escaped value and `'\n'`; it escapes backslash and newline and lets `=` through unescaped by splitting at the first occurrence. *Reconciled:* **one pair per `# ` line**, reusing `writeLine` / `writeNumeric` and their escape and round-trip. That is not cosmetic: `writeNumeric` emits `std::to_chars`' shortest round-tripping decimal, which is the only reason a reader can reproduce `sumSquares` — and therefore the report's own numbers — bit-exactly from the file (§10's "a reader that has only the rounded dB cannot reproduce the report's own numbers").
- **SPL-R7 — §7's `q` values are printed as rounded decimals; ship them computed, and D1f is the ONLY thing that can tell the difference.** `3/log10(2) = 9.9657842846620869…` against the record's readable `9.9657843`: `+1.5338e-08` absolute, `+1.5391e-09` relative — a rounding **up**, not a truncation (truncating gives `9.9657842`). `5/log10(2) = 16.6096404744368…` against `16.6096404`: `−7.4437e-08` absolute, `−4.4815e-09` relative. *Reconciled:* ship `3.0 / std::log10(2.0)` and `5.0 / std::log10(2.0)` **computed**, never the decimal; the decimals stay in prose as a rendering. **The justification is D1f's bitwise equality and nothing else.** An earlier revision of this line claimed the literal fails D2b's own bound at the top of the range, and that is refuted: against `D = 100·10^((L−85)(1/q_used − 1/q_true))` the literal deviates by `1.8e-07 %` at 80 dBA, `5.3e-07 %` at 100 and `1.6e-06 %` at 129, against bounds of `0.0656 %`, `0.1111 %` and `90.3055 %` — five to eight orders of margin, and it clears D2a's float tolerance too (`1.6e-8` relative against float eps `1.19e-7`). **No numeric acceptance in this lane can distinguish the two constants.** The rule survives because it is cheap and because a constant that is *derived* documents where it came from; the reason it was given did not survive.

  > **CORRECTION, 2026-09-18, station 4 Wave 1 — the sentence immediately above is FALSE, and PR #20's verifier refuted it by mutation.** The paragraph reasons against `D2b`'s printed-row bounds (`0.0656 %` at 80 dBA and so on) and then assumes `D2a` uses a *float* tolerance (`1.6e-8` relative against float eps `1.19e-7`). **Shipped, `D2a` uses an ABSOLUTE `1e-9 %` bound** — about four orders tighter than float eps — and at that bound the typed literals are rejected outright: `9.9657843` deviates by up to **`1.600e-06 %`** (at 130 dBA), failing by **1600x**, and is red at **50 of the 51** NIOSH levels, 85 dBA surviving only because its exponent is zero; `16.6096404` fails by **2485x**, worst `2.485e-06 %`. The computed constants clear the same bound by `1.0e4x` (worst `9.95e-14 %` over both tables). So **SPL-R7 stands on two independent legs, accuracy and bitwise exactness**, and D1f's bitwise check is the *weaker* of the two rather than the only one. W1-D1f now measures both legs over D2a's own grid instead of asserting about them. The shape of the error is worth more than the numbers: a correction written to retire a claim unsupported by measurement shipped a NEW claim unsupported by measurement, and a negative claim about a whole lane is the one kind nothing in the suite ever exercises — `memory/an-unmeasured-negative-claim-is-the-one-no-suite-exercises.md`.
- **SPL-R8 — §5's histogram span `[−20, +180)` does not contain either floor already in the tree.** `meter::kLevelFloorDb = −200.0` (`Detector.h:22`, returned by `Detector::levelDb` for a zero state) and `measure::kLevelFloorDb = −120.0` (`Levels.h:14`). *Reconciled, twice.* (a) The histogram is fed the **core** detector's `levelDb` (floor −200), so a silent channel lands entirely in `belowSpan` and every Ln over it is **absent with reason `BelowSpan`** — never the bottom of the span. W1-A5's fixture is pure silence and is red if the code clamps. **And it coheres with A8, which wants a 140 dB(A) reading at a +100 dB offset to land in-span: the offset is applied to the value AND to the base, where it cancels, so the bin index is offset-invariant and what actually reaches `add()` is the un-offset `levelDb`.** That is why (a) and (b) are not in tension — (a) fixes what is *fed*, (b) fixes where the span *sits*, and the two move together by construction. (b) **The base is DERIVED, not typed**: `histogramBaseDb() = measure::kLevelFloorDb + referenceOffsetDb`. The record's −20.0 was written assuming SPL throughout (§5: "the 200 dB span it implies covers every level any calibration offset can put on screen" — true *only once an offset exists*), but the Q1 scope default shipped beside it puts **uncalibrated** SPL in mean-square dBFS, where a real session sits at −30…−60 dBFS and a hard `[−20, +180)` span makes **every Ln of every out-of-the-box session permanently `BelowSpan`**. That is `memory/a-default-must-be-run-through-the-gate-it-feeds.md` verbatim, and an earlier revision of this plan shipped it. Uncalibrated the derived span is `[−120, +80)`; at a typical `+100 dB` offset it is `[−20, +180)`, which is §5's own number **recovered**. W1-A7 and W1-A8 are the two fixtures.
- **SPL-R9 — growing `core_makes_no_class_1_claim` to the report templates is not a glob edit.** The script takes `-DCORE_DIR` only and builds its `file(GLOB sources ...)` entirely from it (`check_no_conformance_claim.cmake:31-43`); it is registered once, from `core/tests/CMakeLists.txt:115-119`. *Reconciled:* give the script an optional `-DGLOBS=` mode — the exact shape `check_no_framework_deps.cmake` already accepts from `app/tests/CMakeLists.txt:282` — and register a **second** `add_test` from `app/tests/CMakeLists.txt`, named `report_makes_no_class_1_claim`. Two tests, two independent `OK (N files scanned)` counts, so a change to one scope cannot silently shrink the other. Its `FATAL_ERROR` text must also stop saying `core/` when it is scanning `app/`. **And extending it outward opens a hole inward that must be closed in the same commit (defect 8):** its `core/tests` coverage is **three named files** — `test_weighting.cpp`, `test_detector.cpp`, `test_leq.cpp` (`:40-42`) — while this lane's five new core test files (`test_block.cpp`, `test_level_histogram.cpp`, `test_alarm.cpp`, `test_dose.cpp`, `test_dose_tables.cpp`) are the densest quotations of IEC 61672-1 and NIOSH anywhere in the repo and would be **unguarded**. They are added by name, and because a misspelled name in such a list shrinks coverage *silently* (`memory/core-must-not-include-frameworks.md`), the risen scanned count is the only proof it was done.
- **SPL-R10 — that same guard's regex is a designed-in false positive over HTML/CSS.** `[Cc][Ll][Aa][Ss][Ss][ \t_-]*[01]` (`:48`) matches `class1`, `class_1`, `class-1` and `class 1`. It does **not** match `class="tier-1"`, because `=` is outside the character class. *Reconciled:* the template's CSS class names never follow the token `class` with a separator and a digit; and Task G shows the guard **red on that exact shape** and green after, so the constraint is a test rather than folklore.
- **SPL-R11 — `PaneView::Spl` is safe in one direction and silent in the other, and no schema bump is needed for it.** `PaneView` has two enumerators (`PaneRegistry.h:19`) and `resolvePaneView` already reports `fellBack` for an unknown name (`:25,:29,:34`). A session naming `"spl"` opened by an older build falls back to `Rta` **and says so** — which is the whole point of that field. *Reconciled:* no `kSchemaVersion` bump for the pane name (`SessionCodec.h:30` stays 3). The pane's **model** (`view/SplStrip.h`) is JUCE-free and OFF; only the component and `WorkspaceView`'s factory case are ON. Separately: **the alarm, dose and log-span settings are not persisted in v1** — there is no preferences store anywhere in `app/src` (`grep` for `PropertiesFile` / `ApplicationProperties` / `getUserSettings` returns nothing), the same gap L-API's plan recorded as API-R5. They are constructed by the composition root from `SplConfig`'s defaults.
- **SPL-R12 — §13 Q4's second half ("published tables including the errata, or the formulas?") is already answered in one direction by §7's own fixture.** The `100·r/T_exact` bound *detects* the 99 dBA erratum rather than tolerating it — a fixture built to reproduce the printed table would have to assert the wrong value there. *Reconciled:* the default taken is **the formulas**; the 99 dBA row ships as an explicitly named, excluded known erratum in W1-D2's table, never as a widened bound. `memory/a-threshold-read-off-a-grid-is-that-grids-floor.md` is why: a bound derived from the printing carries its justification from outside the grid it is measured on; a bound widened to swallow a typo does not.

---

## Wave 0 — SPL reaches `Snapshot` (record §1.1, §2, §11, §13 Q1)

*The lane's premise. `grep -rn "rta::meter\|rta/meter"` over the repository returns **16 lines, every one under `core/`** — re-measured on this branch point, unchanged from the record. `measure::Snapshot` carries no broadband level, no weighting, no detector. Until this wave lands, `"spl"` cannot appear in L-API's `available` list and nothing downstream in this lane has an input. §2's `Block` and `BlockAccumulator` are pulled forward from Wave 1's core math because Wave 0 is not expressible without them.*

### W0-A — `meter::Block`, `BlockAccumulator`, `combineBlocks`, `calibrationOffsetDb` (core, OFF; record §2, §3, §8, §11)

**Files.** Create `core/include/rta/meter/Block.h` (**NEW**, ≤ 190), `core/src/meter/Block.cpp` (**NEW**, ≤ 220), `core/tests/test_block.cpp` (**NEW**, ≤ 320); modify `core/CMakeLists.txt` (add the `.cpp` beside `:58-59`) and `core/tests/CMakeLists.txt` (add the test to the `add_executable` list, `:11-71`).

**RED first.** The test opens with `#include "rta/meter/Block.h"`; the build fails at the include. Paste it.

| # | case | closed-form acceptance |
|---|---|---|
| A1 | **the layout §4's table is sized on** | `static_assert(sizeof(Block) == 40)` and `static_assert(alignof(Block) == 8)`, with the field arithmetic in the comment: `8 + 4 + 4 + 8 + 4 + 4 + 4 + 4` — **`droppedSamples` sits in the 4 bytes the `double`'s alignment was already wasting**, so the record's 36-of-40 becomes 40-of-40 and §4's ring table does not move (defect 5). **Measure it — if the compiler disagrees, the plan is wrong and the ring table moves with it.** A CI job on GCC/Clang is what makes this a claim about the product rather than about MSVC |
| A2 | block Leq is the block's own energy | constant amplitude `a`, `blockSamples = 48000`: `10*log10(sumSquares/blockSamples) == 20*log10(a)` to **1e-9**, **not 1e-12** — `core/tests/test_leq.cpp:69-74` already widened this exact comparison and states the reason in its own comment: summing 48 000 individual double squares accumulates about `1e-12` of round-off against the single-multiplication closed form. Measured on this branch: `a = 0.1` → `2.636e-12`, `a = 0.3` → `2.874e-12`, both **over** 1e-12. The two exceptions, asserted separately and tightly: `a = 1` is `0.0` **exactly** (integers ≤ 48 000 are exact in double) and `a = 1/√2` lands at `8.882e-16`. A builder who wants 1e-12 back must put Kahan/Neumaier summation in `BlockAccumulator` and say so — the tolerance follows the algorithm, not the other way round |
| A3 | **the clock is samples** | feed `3 · blockSamples + 17` samples in hops of 1000 (so no hop is block-aligned): exactly **3** blocks come out, `blockIndex` 0,1,2, each with `blockSamples` equal to the configured value, and 17 samples remain pending. **Made red** by rearming on a hop boundary instead of a sample count |
| A4 | §3's energy sum is exact, and it is the identity `test_leq.cpp:30` already pins | **the fixture is halves at `L` and `L+10`**, equal lengths, so the mean power is `0.5·10^(L/10) + 0.5·10^((L+10)/10) = 5.5·10^(L/10)` and `combineBlocks(...).leqDb == L + 10*log10(5.5)` = **`L + 7.4036268949`**, to **1e-9** (A2's floor). Same fixture split into 1, 2, 4 and 900 blocks gives the **same** answer — which is what "the recompute has no precondition" means. **Assert the formula, never a typed-in literal**: `core/tests/test_leq.cpp:41-47` catalogues this repo's third wrong literal in a plan, and an earlier revision of this row was the fourth — it printed `10*log10(5.05) = 7.0329`, which is the **±10 dB** case below, 0.371 dB away from the fixture it was attached to |
| A4b | the ±10 case, named so the two are never confused again | halves at `L−10` and `L+10`: mean power `0.5·10^-1 + 0.5·10^1 = 5.05` times `10^(L/10)`, so `leqDb == L + 10*log10(5.05)` = **`L + 7.0329137812`**, to 1e-9. The pair is the point: two fixtures one dB apart in description and 0.371 dB apart in value |
| A5 | **recompute, not subtraction** | build a 900-block window; retire block 400 by setting `BlockFlag::CalibrationInvalid` on it; `combineBlocks` over the surviving membership equals a fresh `combineBlocks` over the same 899 blocks **bitwise**. This is record §3's actual reason, and it is the fixture a running-subtraction implementation cannot pass |
| A6 | `bufferFill` is honest | a window of 900 blocks with 300 present reports `bufferFill == 1.0/3.0` to 1e-12, and with 0 present reports `leqDb` **absent** — not `kLevelFloorDb`. §9: "a live Leq shown without saying that its window is not yet full is a number that is quietly wrong" |
| A7 | calibration offset | `calibrationOffsetDb(94.0, m) == 94.0 - m` exactly for `m ∈ {-3.0103, 0.0, 120.0}`; and the round trip `m + calibrationOffsetDb(c, m) == c` bitwise |
| **A8** | **a gap is recoverable from the block alone** (defect 5, SPL-R1 ∧ SPL-R2) | feed a stream in which the bus drops 12 000 samples between block 3 and block 4: block 4 carries `droppedSamples == 12000` and `BlockFlag::Gap`; every other block carries `0`. **Elapsed samples up to and including block *n* is `Σ(blockSamples + droppedSamples)` over `0..n`**, and it equals the true sample position **exactly** (integers, no float). **Made red** by setting the `Gap` bit without the count — which is the state the earlier revision of this plan shipped, where a third party reading the log could see that time was lost and could not say how much |
| **A9** | `CalibrationInvalid` **excludes** | one block in 900 flagged: `combineBlocks` returns the 899-block answer bitwise, and `excludedBlocks == 1`. The one exclusion with a clause behind it (ISO 1996-2 cl. 5.2) |
| **A10** | `Overload` **includes** | one block in 900 flagged, all 900 at the same level: `leqDb` is unchanged **bitwise** from the unflagged run, and `overloadBlocks == 1` is reported. Excluding it would delete the loudest moment of the show from the compliance number |
| **A11** | `UnderRange` **includes** | same shape; excluding floor readings biases Leq **up** |
| **A12** | `Dropped` and `Gap` **include** | a gapped block's energy is real over the samples it saw and `blockSamples` is the honest divisor; what was lost is **time**, and A8 is where that is recorded |
| **A13** | every flag is counted whether it excludes or not | `WindowResult` carries `excludedBlocks`, `overloadBlocks`, `underRangeBlocks`, `gapBlocks` and `droppedSamplesTotal`; §9 item 8 prints all five. **Made red** by reporting only the exclusions |

- [ ] **Accept:** OFF ctest `base_off + N` (N read from ctest, **not predicted**). Zero `warning C`.
- [ ] **Mutation:** make `combineBlocks` average the per-block `leqDb` instead of summing energy → A4 fails (`L + 5.0` against `L + 7.4036`); revert. **Mutation 2:** delete the `CalibrationInvalid` membership filter → A5 and A9 fail; revert. **Mutation 3:** make `Overload` exclude → A10 fails; revert. **Mutation 4:** drop `droppedSamples` from the block → A8 fails; revert.
- [ ] **Commit:** `feat(core): the SPL block, its sample-count clock, and the energy sum every longer window is recomputed from`

### W0-B0 — the shared allocation probe (app tests, OFF; defect 6)

*Twenty minutes, and without it W0-B4 and W2-A1 cannot both exist.* The pattern they were told to copy — `app/tests/test_average_group.cpp:157-175` — is an anonymous-namespace counter pair **plus a replacement of the global `operator new` / `operator delete`**, and its own comment says so. Replaceable global allocation functions are **one definition per program**; `test_spl_meter.cpp`, `test_spl_history.cpp` and `test_average_group.cpp` all link into `rtatool_analysis_tests`. Copying it twice is a duplicate-symbol link error, and not copying it leaves counters that are unreachable from another translation unit.

**Files.** Create `app/tests/AllocationProbe.h` (**NEW**, ≤ 60 — declares `resetAllocationProbe()`, `allocationBytes()` and a scoped `AllocationProbe` guard; **no** `operator new` here) and `app/tests/AllocationProbe.cpp` (**NEW**, ≤ 70 — **the one and only** definition of the replaced `operator new` / `operator delete` in this binary, moved verbatim from `test_average_group.cpp`); modify `app/tests/test_average_group.cpp` (delete its anonymous-namespace pair and its three `operator` definitions, include the header) and `app/tests/CMakeLists.txt` (add the `.cpp` to `rtatool_analysis_tests`' source list, `:10-167`).

| # | case | acceptance |
|---|---|---|
| B0a | **T12 is unchanged** | `app/tests/test_average_group.cpp`'s existing publish-churn test passes with the **same measured bytes** as before the move — `bytes(8) − bytes(4)` against its stated bound of `4·sizeof(PositionSummary) + 4096`. Measure both sides; do not copy the absolute figures, they are fixture-specific |
| B0b | one definition, and the linker says so | the ON and OFF builds link with no duplicate-symbol error, and a grep proves `operator new` is defined in **exactly one** file under `app/tests/` |
| B0c | **the counter is program-global, so every measuring case resets it** | `AllocationProbe` is a scope guard that resets on construction and stops counting on destruction; a test that measures without one reads another `TEST_CASE`'s allocations. Catch2 runs all three files in one process, so this is not hypothetical |

- [ ] **Accept:** OFF and ON ctest unchanged in count (nothing added, one file moved); `measure_has_no_framework_deps` rises by **1** — `AllocationProbe.h` is picked up by the trailing `*.h` glob at `app/tests/CMakeLists.txt:282` with no hand edit, and `AllocationProbe.cpp` is not globbed.
- [ ] **Commit:** `test(app): one allocation probe for the whole test binary -- a global operator new cannot be copied per file`

### W0-B — `SplConfig` + `SplMeter`: the per-channel chain (app, OFF; record §2, §11)

**Files.** Create `app/src/measure/SplConfig.h` (**NEW**, ≤ 140), `app/src/measure/SplMeter.h` (**NEW**, ≤ 150), `app/src/measure/SplMeter.cpp` (**NEW**, ≤ 240), `app/tests/test_spl_meter.cpp` (**NEW**, ≤ 300); modify `app/tests/CMakeLists.txt` — the `rtatool_analysis_tests` source list (`:10-167`) **and** `measure_has_no_framework_deps`'s `GLOBS` (`:282`), by hand, three entries.

| # | case | acceptance |
|---|---|---|
| B1 | the chain is the shipped parts, not new ones | `SplMeter` constructed with `WeightingType::A` reproduces `rta::dsp::Weighting::analyticDb(1000.0, A)` at its own output for a 1 kHz sine to **1e-6** (float path; residual printed). It does not reimplement a weighting curve |
| B2 | **short-term max-held, long-term integrated** (§2, Smaart v9.1 p.33) | a block containing **one burst of duration `t` at `L_burst`, 20 dB above the block's floor**. `Detector`'s own documented closed form (`Detector.h:26-31`) is that the **state is a mean square** whose step response is `1 − e^{−t/τ}`, so the level is `10·log10` of *that*: **`maxFastDb == L_burst + 10*log10(1 − e^{−t/τ})`**. The tolerance brackets **that predicted value**, never the steady value: `≤ 1e-4 dB`, derived from `Block::maxFastDb` being a **`float`** — measured float32 ULP at 100 dB is `7.62939453e-06`, so 1e-4 is >13× it, and the detector itself runs in `double`. Two fixtures: `t = 50 ms` gives **`−4.8190745912 dB`** of deficit (33 % of steady mean square — *nowhere near* within 0.5 dB of steady, which is the plain reading a correct implementation would have gone red on), and `t = 5τ` gives `−0.0286 dB`. τ is read from **`Detector::riseTimeConstant(TimeWeighting::Fast)`**, never typed: IEC 61672-1:2013 **clause 5.8** is the time-weighting clause (verified from its ToC), but the **125 ms value itself is UNVERIFIED** in record §14 — vendor pages only — so the test asserts the closed form against whatever the shipped constant is and depends on the unverified number for nothing. **Made red** by sampling the detector at the block boundary instead of max-holding: the value drops to the floor |
| B2b | **the deficit is the rise term, not the decay term** | `10*log10(1 − e^{−0.4}) = −4.8190745912` and `10*log10(e^{−0.4}) = −1.7371779276` — **3.0819 dB apart**, and an earlier revision of B2 quoted the first formula and printed the second number rounded. Both are asserted here, by name, so the complement can never be substituted for the step again. Measured on this branch with g++ 16.1.0 (MinGW-W64 ucrt) |
| B3 | **the overload run crosses the block boundary** (SPL-R4) | three consecutive samples at `kFullScaleThreshold`, positioned so that two fall in block *n* and one in block *n+1*: `BlockFlag::Overload` is set on the block that **completes** the run. **Made red** by calling `rta::dsp::hasOverload` per hop with no carried state — which is what the header at `OverloadDetector.h:22-27` warns about, in the exact shape it warns about |
| B4 | no allocation after construction | **through W0-B0's shared `AllocationProbe`**, never a second global `operator new`: `push()` over 10 blocks' worth of hops allocates **0 bytes**. The ring and every detector are sized at construction |
| B5 | the offset is DATA | `SplConfig::referenceOffsetDb = 94.0 - 20*log10(a) + 3.0102999566398120` applied to a full-scale sine makes the published metric read **94.0** to 1e-9. No calibration *flow* is in this path — which is what makes Wave 3 removable (§13 Q2's flip) |

- [ ] **Accept:** OFF ctest rises; `measure_has_no_framework_deps` scanned count rises by **exactly 3**, read from its own output.
- [ ] **Commit:** `feat(app): the per-channel SPL chain -- weighting, two detectors, C-peak, and an overload run that survives a block boundary`

### W0-C — `Snapshot::spl` and the publish fold (app, OFF; record §9, §11)

**Files.** Modify `app/src/measure/Snapshot.h` (add `SplMetricReading`, `SplAlarmReading`, `SplBlockView` and **one** `std::optional<SplBlockView> spl` field; the file is 266 lines and the budget is **≤ 360**), `app/src/measure/AnalysisPublish.h` / `.cpp` (fold the SPL view into `buildPublishedSnapshot`, `AnalysisPublish.h:109`); create `app/tests/test_spl_publish.cpp` (**NEW**, ≤ 260).

| # | case | acceptance |
|---|---|---|
| C1 | **absence is absence** | with no logging session running, `snapshot->spl` is `std::nullopt` — not a default-constructed block, not a zeroed one. `memory/a-placeholder-for-an-absent-result-erases-its-state.md`, and the same rule `transfer` (`Snapshot.h:221`) already follows |
| C2 | **no wall clock in `Snapshot`** (SPL-R2) | a structural grep over `Snapshot.h` for `time`, `chrono`, `Unix`, `iso` returns nothing new; and two snapshots built from the same input still compare equal field-for-field, which is what `Snapshot.h:198-201` and the eight `rtatool_snapshot` PNGs depend on |
| C3 | the alarm `state` is server-computed (§9) | `SplAlarmReading` carries `state` and `headroomDb`; a structural grep proves nothing downstream re-derives an alarm by comparing `valueDb` to `limitDb`. Task G repeats this grep over the viewer's JS |
| C4 | `blockIndex` monotonic across a publish | ten publishes over a synthetic stream: `spl->blockIndex` is non-decreasing and increases by exactly the number of completed blocks, independent of `kMinPublishIntervalMs`'s jitter (`AnalysisThread.cpp:21`) |

- [ ] **Accept:** OFF ctest rises; `Snapshot.h` under 400 lines; `measure_has_no_framework_deps` scanned count rises by **0** — `Snapshot.h`, `AnalysisPublish.h` and `AnalysisPublish.cpp` are all already in the GLOBS list, this task's only new file is `test_spl_publish.cpp`, and the trailing patterns at `app/tests/CMakeLists.txt:282` are `*.h` and `*.hpp` only, **never `*.cpp`**.
- [ ] **Commit:** `feat(app): SPL reaches the published Snapshot -- one optional block, no wall clock, absence when nothing is logging`

### W0-D — the drain feed, and the reference gate it inherits (app, **ON**; research §C4, SPL-R1)

**Files.** Modify `app/src/measure/AnalysisThread.h` (`:242` today; budget **≤ 300**) and `app/src/measure/AnalysisThread.cpp` (`:365` today; budget **≤ 400** — if it breaches, the seam is drain-versus-feed and the feed moves to `AnalysisPublish.cpp`, exactly as `publishIfDue` already split once).

> **THE TAP POINT, EXACTLY — and it is NOT where an earlier revision of this plan put it (defect 12).** The named precedent `locateBuffer_.feedHop` sits at `AnalysisThread.cpp:269`, which is **below** the cap check at `:256`:
>
> ```cpp
> :250        refRing->discard(hop);
> :251        for (std::size_t routeIndex = 0; routeIndex < plan.routes.size(); ++routeIndex) {
> :252            const auto& route = plan.routes[routeIndex];
> :253            if (route.referenceChannel != refChannel) continue;
> :254            bus_.ring(route.measurementChannel)->discard(hop);
> :255                                       <-- THE SPL TAP GOES HERE
> :256            if (routeIndex >= static_cast<std::size_t>(kMaxTransferFunctions)) continue;
> ```
>
> For a route at position ≥ `kMaxTransferFunctions` (**8**, `app/src/measure/RoutingPlan.h:29`) the measurement ring is **`discard`ed at `:254` and the loop `continue`s past every consumer**. A tap at `:269` would drop those channels entirely *while their samples were being consumed* — a log short by an unknown amount, with **no `Gap`**, because `CaptureBus::dropCount` never rises when the ring is drained on purpose. That is `memory/a-cap-checked-on-the-drain-path-is-unchecked-on-the-publish-path.md`, on the same function, one release later. **The tap goes between `:254` and `:256`**, reading `channelScratch_[route.measurementChannel]`, which the peek loop at `:239-247` filled for *every* matching route regardless of position. It must not go in the peek loop itself: that loop can `break` on `allPeeked == false` **before** anything is discarded, so a tap there double-counts on the retry.
>
> The SPL meters are **not** `analysers_`. They are their own array, sized by the number of **logged channels**, so `kMaxTransferFunctions` does not bound them — that constant is an MTW-memory policy (`RoutingPlan.h:25-29`), not a channel-count limit.

| # | case | acceptance |
|---|---|---|
| D1 | the tap is on the scratch, never a second ring read | a structural grep: no new `->peek(` or `->discard(` call appears anywhere in `AnalysisThread.cpp`. The feed is a call on the same `scratch`/`measurementScratch` the existing loops already filled (`:239-247`, `:311`) |
| **D1b** | **a route past the cap still logs** (defect 12) | a nine-route plan, SPL logging on the channel at route position **8** (the first past the cap): blocks keep completing and the log is continuous. **Made red** by moving the tap below `:256` — the channel goes **silent with no `Gap`**, which is the one outcome this lane may never ship. If the owner instead rules that capped routes must not log, the acceptance inverts to "the block carries `Gap` for its whole duration"; **silence is not an option either way** |
| D2 | **unrouted sessions log with no reference** (§C4's actual requirement) | with `plan.routes` empty and audio on the measurement channel only, blocks keep completing. **Made red** by hanging the feed off `drainPaired` instead of `drainRole` |
| D3 | the routed path's gate is REPORTED **and its length is recorded** (SPL-R1 ∧ SPL-R2, defect 5) | with a non-empty plan and the reference ring starved past the measurement ring's capacity, block production stops **and** the next block produced carries `BlockFlag::Gap` **together with `droppedSamples` equal to the delta of `CaptureBus::dropCount(channel)` (`CaptureBus.h:168`, incremented at `platform/types/src/CaptureBus.cpp:78-83`)**. The live counter is in memory only and never reaches the file, so the *number* must ride the block — W0-A A8 is where that is pinned and W2-C C8 is where it reaches the log. **Made red** twice: by emitting a clean block across the stall (the log that lies), and by setting the bit with `droppedSamples == 0` (the log that admits it lost time and cannot say how much). **A bounded stall loses nothing** — the drain catches up late — so the absence of a `Gap` there is correct, not a hole |
| D4 | the callback is untouched | `git diff main --stat -- platform/` is **empty**; `audioio_callback_has_no_rt_hazards` and `audioio_scoped_no_denormals_is_first` still green |

- [ ] **Accept:** ON ctest `base_on + N`; both platform guards green; 0 `warning C`.
- [ ] **Commit:** `feat(app): feed the SPL chain from the hops the drain already peeked, and flag the gap the paired drain cannot avoid`

### W0-E — the 3.0103 dB seam, closed form (app, OFF; record §13 Q1, research §C2) — **carries the SCOPE DEFAULT**

**Files.** Create `app/tests/test_spl_seam.cpp` (**NEW**, ≤ 160). No source file changes: this task exists to make the default *provable* rather than merely intended. Modify `app/src/view/Readouts.h` **only** if a label formatter is needed — and it is not: see below.

| # | case | acceptance |
|---|---|---|
| E1 | **the two conventions, stated** | a unit-amplitude sine, mean square `0.5`. Through the app's path: `rta::measure::levelDbFs(0.5) == 0.0` **exactly** (`Levels.h:44`, and `kFullScaleSineOffsetDb = 3.0102999566398120` at `:22`). Through the meter path with `referenceOffsetDb = 0`: `10*log10(0.5) == -3.0102999566398120` to **1e-15**. The difference is `kFullScaleSineOffsetDb` and nothing else |
| E2 | **one conversion, at one seam** | with `SplConfig::referenceOffsetDb = rta::measure::kFullScaleSineOffsetDb`, the published SPL metric for that same sine reads `0.0` to **1e-9** — the metric comes out of a 48 000-sample block sum and inherits W0-A A2's measured round-off floor, so 1e-12 would be red on a correct implementation — the same number the RTA band reads. **This is the acceptance the brief asks for: a full-scale sine reads the same dB through both paths.** It holds because the offset absorbs the constant, which is also why the mismatch bites only *uncalibrated* dBFS (§C2) |
| E3 | after calibration the question evaporates | with `referenceOffsetDb = calibrationOffsetDb(94.0, -3.0102999566398120)`, the metric reads **94.0** to 1e-9 (A2's floor again) and the band still reads `0.0 dBFS`. Two different quantities, two correct numbers, and the label is what distinguishes them |
| E4 | **the label carries the convention, and no fourth formatter is written** | the SPL readout is composed from `rta::view::formatTrim` (`Readouts.h:79`, `"{:.1f} dB"`) and `rta::view::formatHz` (`:72`) — **already tested at `app/tests/test_readouts.cpp:100-116`, already in the guard's GLOBS at `app/tests/CMakeLists.txt:282`, already with a live caller at `app/src/view/DevicePanel.cpp:102`.** The unit suffix is **inside** the returned string, so an assertion that omits it fails on the suffix rather than on the rounding. The convention word (`dB SPL` when `calibrated`, `dB re FS (mean square)` otherwise) is appended by the readout, never baked into a new formatter — record §9 and L-API §11 item 13: **no new formatter may be written** |

- [ ] **Accept:** OFF ctest rises by one `TEST_CASE` group.
- [ ] **Mutation:** add `kFullScaleSineOffsetDb` inside the meter path as well as at the seam → E2 reads `+3.0103` and fails; revert. This is the double-conversion bug the whole task exists to make impossible.
- [ ] **Commit:** `test(app): the 3.0103 dB seam is one conversion in one place -- a full-scale sine through both paths`

---

## Wave 1 — the core pure math (all OFF, all closed-form; record §3, §5, §6, §7, §7a)

*Every acceptance below is a closed-form identity or a clause-derived bound. Nothing in this wave needs a sound card, a socket, JUCE or a golden file, so all of it is proven on three operating systems.*

### W1-A — `meter::LevelHistogram`: Ln from 2000 + 2 bins (core, OFF; record §5)

**Files.** Create `core/include/rta/meter/LevelHistogram.h` (**NEW**, ≤ 160), `core/src/meter/LevelHistogram.cpp` (**NEW**, ≤ 200), `core/tests/test_level_histogram.cpp` (**NEW**, ≤ 300); modify `core/CMakeLists.txt`, `core/tests/CMakeLists.txt`.

| # | case | closed-form acceptance |
|---|---|---|
| A1 | the shape is fixed and provably sufficient | `kBinCount == 2000`, `kBinWidthDb == 0.1`, two out-of-span counters; `sizeof(counts) + 2*4 == 8008` B, `static_assert`ed. Counter width: `2^32 − 1` at 10 samples/s is `429 496 729.5 s = 13.62 years`, asserted as arithmetic, not as a comment |
| A2 | **the w/2 bound, and it is a theorem** | over ≥ 10 constructed distributions (uniform, bimodal, heavy-tailed, single-valued, two-valued at a bin edge) and `n ∈ {1,5,10,50,90,95,99}`: `\|percentileDb(n) − exactPercentile(n)\| ≤ 0.05` **always**, with the worst observed residual **printed beside the bound**. The bound is `w/2` from §5's monotone-map-plus-convex-combination argument; it is not a measured maximum |
| A3 | **centres, not edges** | a histogram with one sample in bin `k` returns `baseDb + (k + 0.5)*0.1`, not `baseDb + k*0.1`. **Made red** by dropping the `+ 0.5` — which is the bias NoiseCapture's floor-keyed bins carry (§B1.4) |
| A4 | the interpolation is the project's existing convention | against `rta::meter::percentileLevelDb` (`Leq.h:28`) on the same data quantised to centres: agreement to **1e-12**. One convention, two implementations, and the histogram is not allowed to invent a third |
| A5 | **absence, not clamping** (SPL-R8) | all samples at `Detector::levelDb`'s floor (`kLevelFloorDb = -200.0`, `Detector.h:22`) with `baseDb = -20.0`: `percentileDb(90)` returns `std::nullopt` and `lastAbsence() == BelowSpan`. **Made red** by returning `baseDb` — which reads as a measurement and is a confession that the span was wrong (§5) |
| A6 | composability, which is what the histogram buys over sorting | `merge(a, b)` then `percentileDb(n)` equals the percentile of the concatenated stream, **bitwise** (integer counts; no float arithmetic is involved in the merge) |
| **A7** | **the default span is run through the gate it feeds** (defect 4; `memory/a-default-must-be-run-through-the-gate-it-feeds.md`) | with `SplConfig`'s **uncalibrated** defaults (`referenceOffsetDb == 0.0`, so `histogramBaseDb() == -120.0`), a session of **real uncalibrated readings** — a full-scale sine at `0.0 dBFS`, and a programme at `-12`, `-30` and `-60 dBFS` — puts every sample **inside** the span: `lastAbsence() == None` and `percentileDb(50)` comes back **present**, with the full-scale sine landing at bin **1200** of 2000 (`(0.0 − (−120.0))/0.1`) — 60 % up the span, with 80 dB of headroom above it. **Made red** by hard-coding `baseDb = -20.0`, which is what an earlier revision of this plan shipped: with the Q1 scope default putting uncalibrated SPL in mean-square dBFS, a `[-20, +180)` span makes **every Ln of every out-of-the-box session permanently `BelowSpan`** — a default that guarantees its own gate fails |
| **A8** | the calibrated span is record §5's own number, recovered | with `referenceOffsetDb = 100.0` the span is `[-20, +180)` — **exactly** the record §5 default — and a 140 dB(A) peak lands at bin 1600. §5's "the 200 dB span covers every level any calibration offset can put on screen" is true *because* the base follows the offset, which is the sentence the record left implicit |

- [ ] **Accept:** OFF ctest rises. **Mutation:** clamp instead of returning absent → A5 red; revert. **Commit:** `feat(core): Ln from a fixed 0.1 dB histogram -- 2000 bins, two out-of-span counters, and a half-bin bound that is a theorem`

### W1-B — the windowed recompute and `Leq::sumSquares()` (core, OFF; record §3, SPL-R3)

**Files.** Modify `core/include/rta/meter/Leq.h` (add one accessor, ≤ 130) and `core/src/meter/Leq.cpp` (three lines); extend `core/tests/test_block.cpp` and `core/tests/test_leq.cpp`.

| # | case | acceptance |
|---|---|---|
| B1 | `sumSquares` beside `leqDb`, and they agree | `10*log10(leq.sumSquares() / leq.sampleCount()) + offset == leq.leqDb()` **bitwise** for ≥ 5 signals. The accessor exists so a caller never has to invert a logarithm to get the energy back — which is the same reason §10 puts `sumSquares` in the file next to `leqDb` |
| B2 | the sliding window is a recompute over current membership | a 900-block ring stepped 3600 times: at every step `combineBlocks` over the live window equals a fresh computation over the same blocks **bitwise** — the same 900 doubles in the same order, so anything looser would be hiding a real difference — and the cost is 900 additions with a relative error bound of `900 × 2^-53 ≈ 1.0e-13` = `4.3e-13 dB` — asserted as arithmetic |
| B3 | SEL is IEC 61672-1 cl. 3.12 Eq. (4) | `L_E(W) == Leq(W) + 10*log10(T_W / 1 s)` to 1e-12, and for `T_W = 0.1 s` it reads **10 dB below** `Leq` — the sign `Leq.h:75-77` already warns about |

- [ ] **Accept:** OFF ctest rises; `Leq.h` still under 400; `core_makes_no_class_1_claim` still green with its count unchanged (no new `meter/` file here). **Commit:** `feat(core): sumSquares beside leqDb, and the sliding window as a recompute over current membership`

### W1-C — `headroomDb` and `AlarmLatch` (core, OFF; record §6)

**Files.** Create `core/include/rta/meter/Alarm.h` (**NEW**, ≤ 140), `core/src/meter/Alarm.cpp` (**NEW**, ≤ 160), `core/tests/test_alarm.cpp` (**NEW**, ≤ 240); modify the two CMake lists.

| # | case | closed-form acceptance |
|---|---|---|
| C1 | the identity's fixed point | `L_t == L_lim` throughout ⇒ `headroomDb == L_lim` **exactly**, for every `T` and every `t ∈ [0, T)` |
| C2 | the record's own check value | `t = T/2`, `L_t = L_lim − 10`: numerator is `0.95·T·10^(L_lim/10)` and `headroomDb == L_lim + 10*log10(1.9) == L_lim + 2.7875360` to **1e-12** |
| C3 | **the window already lost is an absence, not a threshold** | `t = T/2`, `L_t = L_lim + 3.1`: the bracket is negative and `headroomDb` returns `std::nullopt`. **Made red** by returning `kLevelFloorDb` or by clamping to `L_lim` |
| C4 | **no hysteresis, no debounce, and the behavioural half is what proves it** | The grep half (`hyster`, `debounce`, `margin`, `dwell` over the two files) proves **nothing behavioural** — a latch holding `previousDb_` and an epsilon matches none of those words — and it is kept only as a naming check. The proof is the fixture, **and its amplitude is the whole test**: `AlarmLatch` fed a value alternating `L_lim ± δ` transitions on **every** crossing, for `δ = 1 ULP of L_lim in double` **and** for `δ = 0.01 dB`. A ±3 dB swing would pass with a 1 dB hysteresis quietly in place; the swing must be **the smallest step that could be swallowed**, which is `memory/a-fixture-can-be-too-well-behaved-to-fail.md` applied before it bites rather than after. 0.01 dB is also a fifth of the 0.05 dB semi-range IEC 61672-2 cl. 6.21 assigns a 0.1 dB display, so a hysteresis small enough to pass it is smaller than the instrument's own resolution. **Made red** by adding a 0.005 dB hysteresis — the 1 ULP case goes red, the 0.01 dB case does not, and that asymmetry is why both ship |
| C5 | it compares a windowed Leq, never an instantaneous level | `AlarmLatch::update` takes a `WindowResult`, and a structural test proves there is no overload taking a bare instantaneous dB |

- [ ] **Accept:** OFF ctest rises. **Commit:** `feat(core): remaining headroom as a closed form, and a two-state latch with no invented margin`

### W1-D — `meter::Dose`: one formula, `q` per preset, two accumulators (core, OFF; record §7) — **the two-part fixture**

**Files.** Create `core/include/rta/meter/Dose.h` (**NEW**, ≤ 170), `core/src/meter/Dose.cpp` (**NEW**, ≤ 180), `core/tests/test_dose.cpp` (**NEW**, ≤ 340 — **D1's identities and D3's structural checks**), `core/tests/test_dose_tables.cpp` (**NEW**, ≤ 340 — **D2's two-part table fixture alone**, split at the cap before it is written); modify the two CMake lists.

**Part one — the identities, exact for every `q` (record §7's own table):**

| # | case | acceptance |
|---|---|---|
| D1a | `L = L_c` for `T = T_c` | `D == 100 %` **exactly**, for `q ∈ {10, 3/log10 2, 5/log10 2, 4/log10 2}` |
| D1b | one exchange up, half the time | `L = L_c + Q`, `T = T_c/2`, `q = Q/log10(2)`: `D == 100 %` **exactly** (bitwise), for `Q ∈ {3,4,5,6}` |
| D1c | one exchange down, twice the time | `L = L_c − Q`, `T = 2·T_c`, same `q`: `D == 100 %` **exactly** (bitwise) |
| — | **why D1b/D1c are bitwise, and it is measured rather than assumed** | An earlier revision loosened both to 1e-12 on the *asserted* grounds that `std::pow(10.0, std::log10(2.0))` is not bit-identically `2.0`. **It is**, on both toolchains reachable from this machine: g++ 16.1.0 (MinGW-W64 ucrt) and the MSVC ucrt — `pow(10,log10(2)) == 2.0`, `10^(Q/q) == 2.0` and `10^(−Q/q) == 0.5` bitwise for all four `Q`, and `D == 100.0` bitwise in both directions. Giving up an available bitwise check on an unmeasured negative is the exact inverse of this plan's own rule that the tolerance follows the algorithm. **NOTE for station 4:** these run on three CI toolchains. If one of them makes a bitwise comparison fail, **that is a finding to report to the orchestrator — a named libm on a named OS, in the PR body — not a tolerance to widen in silence.** The same discipline W1-B B2 applies in the other direction |
| D1d | the asymmetry an average would hide | half at `L_c − Q` and half at `L_c + Q`, each `T_c/2`: `D == 100·(0.25 + 1.0) == 125 %` to 1e-12 |
| D1e | the threshold contributes **exactly zero** | every block below `thresholdDb`: `D == 0.0` **bitwise**, and `secondsBelowThreshold()` equals the total. Time below threshold is reported separately and **never folded in** |
| D1f | **the constant is computed, not typed** (SPL-R7) | the shipped NIOSH `q` equals `3.0/std::log10(2.0)` **bitwise**; asserting it against the literal `9.9657843` **fails**, and the test says so in its own name. Likewise OSHA against `5.0/std::log10(2.0)` vs `16.6096404`. **AMENDED 2026-09-18:** it also measures the ACCURACY leg the SPL-R7 note above wrongly retired — the worst `\|D-100\|` over D2a's own 80..130 grid for the computed constant against the typed one, for both regulators, asserting the first clears D2a's `1e-9 %` and the second fails it on 50 of 51 rows |
| D1g | the gap between the two NIOSH conventions is a closed form | `D(q=10)/D(q=3/log10 2) == 10^(−0.00034333·ΔL)` to 1e-9 at `ΔL ∈ {15,30,45,55}` → **1.1788 / 2.3437 / 3.4949 / 4.2549 %** low. At `ΔL = 30` the same fact is `2^10/10^3 = 1.024` exactly |
| D1h | **`L_EX,8h` never takes `q`** | `exposureLevelDb(L, T) == L + 10*log10(T/28800)`, and a structural grep proves the function has no `q` parameter and no overload that takes one. IEC 61252 Ed 2 Formula (5); record §7's own rule |

**Part two — the regulators' own tables, two halves (record §7; verified twice on PR #12):**

| # | case | acceptance |
|---|---|---|
| D2a | **the formula, at every 1 dB step** | for `L ∈ [80, 130]` take `T_exact = 480/2^((L−85)/3)` minutes (NIOSH §1.1.1 p.1) and `T_exact = 8/2^((L−90)/5)` hours (OSHA Table G-16a footnote): `D == 100 %` to float tolerance, **51 rows each**. This asserts the **formula** — which is exact and is what the code implements |
| D2b | **the printed rows, within their own printing** | for every **printed** row of NIOSH Table 1-1 and OSHA Table G-16a: `\|D − 100 %\| ≤ 100·r / T_exact`, where `r` is **that row's own printed resolution** — for Table 1-1, 1 min where an Hours cell is printed and 1 s where the row is printed in seconds; for Table G-16a, **one unit in that row's last printed decimal place** (0.1 h at `81 → 27.9`, 0.001 h at `130 → 0.031`). The bound is **derived from the printing, not chosen**: 0.0656 % at 80 dBA, 0.1111 % at 100, 11.2882 % at 120, 90.3055 % at 129. It holds on **all 51 G-16a rows** and on **all 50 Table 1-1 single-level rows except one** |
| D2c | **99 dBA is excluded and named** (SPL-R12) | Table 1-1 prints `18 min 59 s` = 1139 s where its own formula gives `18 min 53.93 s` = 1133.929 s; `\|D − 100 %\| = 0.4472 %` against a bound of `0.0882 %`. The fixture pins it as a **known erratum exclusion** with that arithmetic in the test's own comment — **the bound is not widened for it.** The same bound that tolerates the rounding is what detects the typo |
| D2d | the truncated rows are truncated, and that is stated | 124 dBA (exact 3.5156 s, printed 3) and 127 dBA (exact 1.7578 s, printed 1) floor rather than round; the set is exactly `{124, 127}`. Asserted, so a later "fix" to the fixture goes red |
| D2e | Table 1-2 pins `q = 10`, and pins it in both directions | `10*log10(325000) + 85 == 140.11883...` → prints **140.1**, matching the last row; at `q = 3/log10 2` it would be 139.93. And Table 1-1's printed rows **reject** `q = 10` (80 dBA deviates 0.4023 % against a 0.0656 % bound). Two tables, one chapter, two exchange constants — the record's central §7 finding, asserted rather than asserted-about |
| D2f | Table 1-2's own two errata are named | `50,000 % → 102.0` (the formula gives 111.9897) and `26,000,000 % → 139.0` (139.1497); every **other** row of 121 lands within **0.0497**. Excluded by name, with the monotonicity argument (45,000 → 111.5 and 60,000 → 112.8 bracket it) in the comment |
| D2g | **two accumulators run at once** | one `SplConfig` produces an OSHA `D` and a NIOSH `D` from the **same** blocks with **different** thresholds (90 vs 80 dBA), and a block at 85 dBA contributes to one and exactly zero to the other. Larson Davis `NUM_SLM_DOSES = 2`; Smaart's `Exposure O` + `Exposure N` |

**Part three — structural:**

| # | case | acceptance |
|---|---|---|
| D3a | **peak never enters the dose integral** (§7a) | a grep over `core/include/rta/meter/Dose.h` and `core/src/meter/Dose.cpp` for `peak` returns **nothing**, and `Dose` has no member, parameter or overload carrying one. The three 140s are three different measurements and a dose that swallowed a peak could not tell them apart |
| D3b | no regulator's name is in `core/` | a grep for `NIOSH`, `OSHA`, `VLAREM`, `EU`, `ANSI` over the two files returns nothing outside a clause citation in a comment. The presets are values assembled in `app/` |

- [ ] **Accept:** OFF ctest rises; both test files under 400 lines.
- [ ] **Mutation:** set `q = 10` for the NIOSH preset → D2b fails at the top of the range (4.25 % against a 90 % bound at 129 — so it fails at **100 dBA**, where the bound is 0.111 % and the gap is 1.179 %); revert. **Mutation 2:** fold `secondsBelowThreshold` into the dose → D1e red; revert.
- [ ] **Commit:** `feat(core): dose as one formula with a per-preset exchange denominator, two accumulators, and a fixture that detects NIOSH's own erratum`

### W1-E — C-peak outside dose, and the three 140s named (core + app, OFF; record §7a)

**Files.** Extend `core/tests/test_dose.cpp`; create `app/src/measure/SplCriteria.h` (**NEW**, ≤ 120 — the three ceilings as **named, separate** criteria with their scope), `app/tests/test_spl_criteria.cpp` (**NEW**, ≤ 200); modify `app/tests/CMakeLists.txt` (source list + GLOBS).

| # | case | acceptance |
|---|---|---|
| E1 | three criteria, three different quantities | `SplCriteria` exposes **OSHA** (a **peak** limit, **no weighting named**, scoped to **impulsive or impact noise only**), **EU 2003/10/EC** (140/137/135 dB**(C)** peak, all noise), **NIOSH** (140 dB**A**, a **level** ceiling, all noise types). A test asserts each carries its quantity name, its weighting-or-absence, and its event scope. **Made red** by collapsing any two into one row |
| E2 | the app never prints a bare "peak" | a structural grep over `app/src/export/` and `app/src/view/` for a readout label matching `peak` without an adjacent `L_Cpeak` / `L_Zpeak` / `L_AFmax` qualifier returns nothing |
| E3 | sampled, and it says so | the peak readout's label contains the word `sampled`. `Leq::peakDb` is `10*log10(max p²)` (`Leq.h:80-83`); the 4× polyphase true-peak meter was deferred by the weighting record and never came, so the inter-sample maximum can exceed what is logged. §12 carries the deferral rather than hiding it |

- [ ] **Accept:** OFF ctest rises; GLOBS count rises by 1. **Commit:** `feat(app): the three 140s are three criteria -- an OSHA peak scoped to impulsive noise, an EU dB(C) peak, a NIOSH dBA level`

---

## Wave 2 — the app: ring, alarms, log, export, settings, pane (record §4, §6, §10, §11)

### W2-A — `SplHistory`: the declared-span ring (app, OFF; record §4)

**Files.** Create `app/src/measure/SplHistory.h` (**NEW**, ≤ 150), `.cpp` (**NEW**, ≤ 200), `app/tests/test_spl_history.cpp` (**NEW**, ≤ 280); modify `app/tests/CMakeLists.txt` (source list + GLOBS, two entries).

| # | case | acceptance |
|---|---|---|
| A1 | **allocated once, never grown** | **through W0-B0's shared `AllocationProbe`** (never a second global `operator new`): construction allocates `capacityBlocks * sizeof(Block)` once; 10× the capacity pushed afterwards allocates **0**. The rule `platform/`'s capture rings already follow |
| A2 | the size table is arithmetic, not a guess | `capacityBlocks(8 h, 1 s) == 28800` and `28800 * 40 == 1 152 000` B = 1.0986 MiB; `24 h → 3 456 000` B = 3.2959 MiB; `7 d → 24 192 000` B = 23.0713 MiB. **Read `sizeof(Block)` from the type, never from the constant 40** — W0-A1 is what makes that safe |
| A3 | **roll, never wrap-silently** | at capacity + 1, the oldest block leaves memory, `oldestBlockIndex()` advances by one, and the session does **not** end. A reader asking for a block older than the ring gets an absence with a reason, not a stale block |
| A4 | **numbers, not pixels** | a structural grep over `SplHistory.h`/`.cpp` for `colour`, `color`, `Colour`, `pixel`, `argb`, `rgb` returns nothing, and a resize of any view leaves the history bit-identical. `docs/dsp/2026-08-29-display-layer-l5c.md` §7 item 2 (`:340-347`), adopted verbatim: OSM bakes colour into stored points and Friture's ring is as deep as the widget is wide |
| A5 | markers are Smaart's four kinds | `alarm`, `overload`, `note`, `reset`; each carries `blockIndex`, direction, quantity, window and value (§6's "what the alarm records") |

- [ ] **Accept:** OFF ctest rises; GLOBS +2. **Commit:** `feat(app): the SPL history ring -- a declared span, allocated once, storing numbers`

### W2-B — alarms wired, with the proxy window (app, OFF; record §6, §13 Q5)

**Files.** Create `app/src/measure/SplAlarms.h`/`.cpp` (**NEW**, ≤ 130 / ≤ 180), `app/tests/test_spl_alarms.cpp` (**NEW**, ≤ 240); modify `app/tests/CMakeLists.txt`.

| # | case | acceptance |
|---|---|---|
| B1 | **sliding, and it is the conservative one** (§13 Q5 default) | over a constructed 3600-block session the sliding 15-min maximum is **≥** the consecutive-fixed maximum, for ≥ 5 signals; the two are equal only when the signal is constant. Both are computed from §3's blocks, so the choice is a policy line, not an algorithm |
| B2 | the proxy offset is the **operator's** setting, with its precedents printed | the proxy window and its offset are fields with **no default margin**; the UK Pop Code cl. 4.12 ("typically some 2-3 dB(A) above the 15 minute value") and VLAREM II art. 5.32.2.2bis §2 1° (`LAeq,15min ≤ 102` deems `LAeq,60min ≤ 100` satisfied) appear **beside the field as printed precedents**, never as a shipped default. Record §6: reusing them as an amber margin is the category error the memory file exists to prevent |
| B3 | every transition is a marker | a fire and a clear each append exactly one marker, and the marker's value equals the windowed value that caused it, bitwise |
| B4 | `headroomDb` travels with the state | `SplAlarmReading` carries `state`, `limitDb`, `windowBlocks`, `sinceBlock` **and** `headroomDb` (absent when the window is lost). §9's payload list, and the reason it is more useful than the colour |

- [ ] **Accept:** OFF ctest rises; GLOBS +2. **Commit:** `feat(app): alarms over a sliding windowed Leq, with a proxy window whose offset is the operator's`

### W2-C — the log: format, writer, rotation, tolerant reader (app, OFF; record §10, §13 Q7)

**Files.** Create `app/src/export/SplLog.h` (**NEW**, ≤ 220 — pure content, the `EqTextExport.h` precedent), `app/src/export/SplLogWriter.cpp` (**NEW**, ≤ 200 — the `FirTextWriter.cpp` precedent, the only file here that touches disk), `app/tests/test_spl_log.cpp` (**NEW**, ≤ 320); modify `app/tests/CMakeLists.txt` (source list + GLOBS, two entries).

| # | case | acceptance |
|---|---|---|
| C1 | **the header is `# key=value`, one pair per line** (SPL-R6) | written through `rta::trace::detail::writeLine` / `writeNumeric` (`SessionCodecDetail.h:53`), so `=` splitting, backslash and newline escaping, and `std::to_chars`' shortest round-trip come from the one shipped implementation. Keys: `schema`, `startedAtUnixMs`, `sampleRate`, `blockSamples`, `weighting`, `detector`, `calibrationOffsetDb`, `calibrationUnit`, `calibratorLevelDb`, `histogramBaseDb`. **`calibrationOffsetDb` and `calibrationUnit` are spelled exactly as `app/src/trace/SessionCodec.cpp:30-31` already spells them; `calibratorLevelDb` and `histogramBaseDb` are NEW keys this lane introduces** — `grep -rn "calibratorLevelDb" app/ core/` returns nothing today, and an earlier revision of this row claimed a precedent for it that does not exist. That is PR #12's own defect E1 (`formatDb` / `formatCoherence`) repeating inside the plan written to avoid it |
| C2 | **`sumSquares` round-trips bit-exactly** | write 1000 blocks, read them back: every `sumSquares` compares **bitwise** equal — `writeNumeric`'s `std::to_chars` is called with no precision argument, i.e. the shortest round-tripping decimal, so the text format is lossless. This is the whole reason §10 puts it in the file next to `leqDb`, and it is what lets a third party reproduce the report's own numbers |
| C3 | **an interrupted append costs at most one line, and the reader says how much** | truncate the file at each of 20 arbitrary byte offsets inside the last row: `readLog` returns every complete block, drops the partial one, and reports `bytesDiscarded` equal to the truncated tail's length. **No atomicity claim is made** — the property is stated as tolerance, because none can be made portably |
| C4 | settings cannot change mid-log | a structural test: there is no per-row weighting/detector column, and changing either on a live `SplLogWriter` **starts a new log** rather than writing a mixed file. ISO 1996-2 cl. 5.2's discard rule exists to prevent exactly the instrument this would record |
| C5 | rotation is by a declared block count (§13 Q7 default) | at `segmentBlocks = 3600` the writer opens segment 1 at block 3600; the segment list is retrievable and is what the report prints (§9 item 8). **The app never deletes** — asserted by the absence of any unlink/remove call in `SplLogWriter.cpp` |
| C6 | one file per logged channel | a two-channel session writes two files plus one session header, and no wide-format CSV whose column count depends on the interface |
| C7 | JSON is the same schema, not a second one | `logJson` emits the **identical field names**; a test asserts the key set of a JSON row equals the CSV header's column set, exactly |
| **C8** | **the row carries `droppedSamples`, and time is reconstructible from the file alone** (defect 5) | the CSV header is `blockIndex,blockSamples,droppedSamples,sumSquares,leqDb,maxFastDb,maxSlowDb,peakCDb,flags` — **the record §10 row plus `blockSamples` and `droppedSamples`**, because without them a reader holding only the file cannot form §3's divisor or recover elapsed time across a gap. Write a log with a 12 000-sample drop before block 4; a reader that has **only the text** reproduces the true sample position of every block **exactly** as `Σ(blockSamples + droppedSamples)`. **Made red** by writing the `Gap` bit without the count |
| **C9** | **`t_iso` is a reconstructed elapsed time and the report says so** (SPL-R2 as amended) | `t_iso(n) = startedAtUnixMs + Σ_{i≤n}(blockSamples_i + droppedSamples_i)/fs`, and the report labels the column *"elapsed from session start, reconstructed from the sample clock"* — **not** a wall-clock reading. **Made red** by the pre-fix formula `startedAtUnixMs + blockIndex·blockSamples/fs`, which after a single 12 000-sample drop is early by 0.25 s **for the rest of an eight-hour session** and prints those times in §9's identification block |

- [ ] **Accept:** OFF ctest rises; GLOBS +2. **Mutation:** write `leqDb` only and drop `sumSquares` → C2 red; revert. **Commit:** `feat(app): the append-only SPL log -- one key=value header, sumSquares beside leqDb, and a reader that reports what it discarded`

### W2-D — settings, the `Spl` pane model, and the specimen (app, OFF + ON; record §11, SPL-R11)

**Files.** Modify `app/src/view/PaneRegistry.h` (add `PaneView::Spl` and its `"spl"` name, ≤ 90); create `app/src/view/SplStrip.h` (**NEW**, ≤ 180 — headless geometry, the `BodeLayout.h` precedent), `app/tests/test_spl_strip.cpp` (**NEW**, ≤ 220); **ON only**: `app/src/view/SplView.h`/`.cpp` (**NEW**, ≤ 150 / ≤ 260) and `app/src/dev/preview/SplPreview.h`/`.cpp` (**NEW**, ≤ 90 / ≤ 200, paint-only over canned data, the `PhaseAlignPreview` precedent); modify `app/CMakeLists.txt` and `tools/snapshot.cpp`.

| # | case | acceptance |
|---|---|---|
| D1 | the pane name falls back and says so | `resolvePaneView("spl").view == PaneView::Spl` and `fellBack == false`; an older build reading the same session already returns `Rta` with `fellBack == true` (`PaneRegistry.h:34`), so **no schema bump** (`SessionCodec.h:30` stays 3) |
| D2 | the strip model is numbers in, positions out | `SplStrip` takes `std::span<const Block>` and a rect and returns coordinates; a grep proves it holds no colour and no JUCE type. Decimation and colour happen at draw (§4) |
| D3 | readouts obey CLAUDE.md | frequency whole (`formatHz`), dB one decimal (`formatTrim`), 0..1 two decimals (`formatAgreement`) — **the three that exist**, `Readouts.h:72,79,87`. No fourth formatter |
| D4 | the specimen renders | `rtatool_snapshot shots 1100 760` writes a readable `shots/preview-spl.png`; exit code 0. Rendered offscreen, never screen-captured (CLAUDE.md "Seeing the GUI") |

- [ ] **Accept:** OFF and ON ctest rise; GLOBS **+1** — only `SplStrip.h` is added (`PaneRegistry.h` is already listed, `test_spl_strip.cpp` is a `.cpp` and is not globbed, and the ON files must never be added). **Commit:** `feat(app): an SPL pane -- a headless strip model, the three existing formatters, and an offscreen specimen`

### W2-E — the wiring nobody was assigned (amendment 2026-09-25, orchestrator; found by PR #27's verifier)

**Why this task exists.** This plan builds every SPL part and tests each one
alone, but it assigns **no task** to connect those parts to the running app.
Four facts, each measured at `main` 8f8df27:

1. `grep -rn enableSplLogging app/src` finds only the declaration and the
   definition. **Nothing calls them.** So `Snapshot::spl` is `nullopt` in
   every running session.
2. `AnalysisPublish.cpp:229` says `alarms`, `dosePercent`, `doseProjected` and
   `lnDb` "stay EMPTY/ABSENT through Wave 0". No later task in this plan names
   the publish fold, so they stay empty for good.
3. `SplHistory`, `SplAlarms` and `SplLogWriter` (W2-A..C) have no caller in
   `app/src`.
4. Nothing sets `BlockFlag::CalibrationInvalid`, and nothing applies
   `CalibrationSession::referenceOffsetDb()` to a live `SplConfig`. W3-A A3 is
   therefore true only in its unit test.

`docs/HANDOFF.md` said "the caller is Wave 2", but W2-D's file list never
named `MainComponent`. A duty that sits in a handoff sentence and in no
task's file list is a duty nobody holds.

**Split in two, along the real-time line.**

#### W2-E1 — the analysis thread owns the per-channel SPL state and publishes it (app, OFF where the state is pure)

- For each logged channel, the SPL session state holds its `SplHistory`,
  `SplAlarms`, `LevelHistogram` and the two `Dose` accumulators. **All of it
  is allocated at `enable` time and never grows afterwards.** Prove that with
  the shared `AllocationProbe`: after `enable`, pushing 10× the history
  capacity allocates 0 bytes.
- The state is updated **once per closed block**, on the analysis thread, from
  the same `Block` the session just closed. It is never updated from a
  snapshot.
- The publish fold fills `alarms`, `dosePercent`, `doseProjected` and `lnDb`
  from that state. Any value that has no result yet stays **absent**, never
  0.0. Headroom and the `Filling` state follow record §15 A6.
- Dose follows W1-D's preset table. The Ln slots follow W1-A's absent-outside-
  span rule.
- Tests (OFF where the state object is JUCE-free): a closed-form Leq and a
  closed-form dose over a synthetic block stream, both reaching the published
  view. Plus an absence case for every slot.

#### W2-E2 — the log thread, the composition root, calibration applied (app, ON)

- **The log is never written from the analysis thread and never from the
  message thread.** The analysis thread pushes each closed `Block` into a
  fixed-capacity single-producer/single-consumer queue. A dedicated writer
  thread drains that queue into `SplLogWriter`.
  - When the queue is full, the block is **counted as dropped-from-log and the
    count is published**. The writer never blocks the producer.
  - Why not the message thread: `Snapshot::spl` carries only the *latest*
    block, so a UI stall longer than one block loses blocks without anyone
    seeing it.
- `MainComponent` calls `enableSplLogging` with `SplConfig`'s defaults for the
  measurement channel(s) when a device opens, and `disableSplLogging` when it
  closes. There is no preferences store (SPL-R11), so it uses defaults.
- When calibration ends, the new `referenceOffsetDb` is applied **by starting
  a new log** (W2-C C4: settings never change mid-log).
- When drift > 0.5 dB, `CalibrationInvalid` is set on the blocks of the span
  the pair brackets, and the log is kept (W3-A A3).
- `SplView` shows the live `Snapshot::spl`.
- The calibration capture timeout moves to a monotonic clock (PR #27
  round-2 verifier, LOW).
- The specimen still renders. `MainComponent.cpp` stays under 400 lines, or
  its SPL wiring moves to a new `MainComponentSpl.cpp`.

---

## Wave 3 — the calibration flow (app + core, OFF; record §8, §13 Q2) — **SCOPE DEFAULT: BUILD**

*Removable whole. Waves 0–2 take the offset as data, so deleting this wave deletes three tasks and rewrites none.*

### W3-A — `CalibrationSession`: the start/end pair and the 0.5 dB verdict (app, OFF)

**Files.** Create `app/src/measure/CalibrationSession.h`/`.cpp` (**NEW**, ≤ 140 / ≤ 190), `app/tests/test_calibration.cpp` (**NEW**, ≤ 260); modify `app/tests/CMakeLists.txt`.

| # | case | acceptance |
|---|---|---|
| A1 | **the closed form, no microphone** (§8) | a 1 kHz sine of amplitude `A` through the **Z** path: `L_meas == 20*log10(A) − 3.0102999566398120` to 1e-12, so `offset == L_cal − 20*log10(A) + 3.0102999566398120`; applying it makes the same signal read `L_cal` exactly. An identity, not a golden vector |
| A2 | the pair, and the clause it is compared against | `startCheck` and `endCheck` both recorded with their values and times; `drift == \|end − start\|`; `verdict == Pass` iff `drift <= 0.5`. **ISO 1996-2:2017 clause 5.2**, cited by number in the code comment: a class 1 IEC 60942 calibrator, checked at the beginning **and** at the end, ≤ 0.5 dB between two consecutive checks with no adjustment between them |
| A3 | **drift does not silently invalidate the log** | `drift > 0.5` sets `BlockFlag::CalibrationInvalid` on the session and the report prints both readings, the drift and the clause — and the log is **kept**. An instrument that throws away eight hours of evidence on its own authority is worse than one that says why the evidence is doubtful (§8) |
| A4 | the nominal levels are IEC 60942's | 94.0 and 114.0 dB at 1 kHz are offered; any other value is accepted but recorded as **operator-supplied**, never silently normalised |
| A5 | **no invented refusal band** | a grep proves there is no shipped `±1.5 dB` factory-calibration refusal. That is 10EaZy's number against **their** factory reference, which this project does not have; §8 names it as the shape of check only a flow can perform, not as a constant to copy |

- [ ] **Accept:** OFF ctest rises; GLOBS +2. **Commit:** `feat(app): calibration as a flow -- a start/end pair, a drift, and ISO 1996-2 cl. 5.2's 0.5 dB as the only published criterion`

### W3-B — the flow's session state and its UI (app, ON)

**Files.** Modify `app/src/MainComponent.h`/`.cpp` (the composition root; budgets ≤ 200 / ≤ 400 — `MainComponent.cpp` is 319 today, so this is the file most at risk of the cap. If it breaches, the seam is calibration-versus-everything-else and the wiring moves to a new `MainComponentCalibration.cpp`, exactly as `MainComponentDelay.cpp` already split once).

- [ ] **Accept:** ON ctest rises; the specimen still renders; `MainComponent.cpp` under 400 lines **or** split.
- [ ] **Commit:** `feat(app): the calibration flow in the composition root -- settle, measure, set, and measure again at the end`

### W3-C — calibration in the report (app, OFF)

**Files.** Extend `app/src/export/SplReport.cpp` and `app/tests/test_spl_report.cpp`. Report §9 item 3: pre-check value and time, post-check value and time, the drift, the calibrator's nominal level, and the ISO 1996-2 clause the drift was compared against. **With Wave 3 cut, this section prints "calibration check not performed" and the rest of the report is unchanged** — which is the flip's whole cost.

---

## Wave 4a — the report: one HTML document, frozen payload (app, OFF; record §9, §11) — **not gated on Q8**

### W4a-A — `SplReport`: the self-contained renderer (app, OFF)

**Files.** Create `app/src/export/SplReport.h` (**NEW**, ≤ 150), `app/src/export/SplReport.cpp` (**NEW**, ≤ 320), `app/src/export/SplReportStyle.h` (**NEW**, ≤ 260 — the CSS as `inline constexpr std::string_view`), `app/src/export/SplReportScript.h` (**NEW**, ≤ 300 — the JS the viewer shares; **inert in the frozen report**), `app/tests/test_spl_report.cpp` (**NEW**, ≤ 340); modify `app/tests/CMakeLists.txt` (source list + GLOBS, four entries). **Split across three headers before writing, not after** — a single template file will not fit under 400 lines.

| # | case | acceptance |
|---|---|---|
| A1 | **self-contained, provably** | a structural test over the rendered string. **Load-time fetches:** no `<script src=`, no `<link rel="stylesheet"`, no `src="http`, no `@import`, no `url(http`. **And run-time fetches, which the load-time greps cannot see** (defect 9): no `fetch(`, no `XMLHttpRequest`, no `navigator.sendBeacon`, no `EventSource`, no `WebSocket`, no dynamic `import(`. `SplReportScript.h` is shared with the viewer and is only *asserted* to be inert in the frozen report — an inlined `fetch('/api/v1/spl')` passes all five load-time greps and still reaches for the network when the archived report is opened in twenty years. The seam that makes inertness real: the frozen report defines `window.__SPL_PAYLOAD__` and the script takes the fetch branch **only** when that symbol is absent, and A1b asserts the symbol is present |
| **A1b** | inertness is tested, not asserted | the rendered report contains `window.__SPL_PAYLOAD__ =` exactly once, and the viewer shell (`renderViewerShell`) contains it **zero** times. That one symbol is the whole difference between the two transports (§9: "the report embeds a frozen payload; the web viewer serves the same document with the payload fetched") |
| A2 | the nine sections are all present | identification, instrument, calibration, settings, per-metric results, dose, time history, validity, integrity — §9's list, each asserted by a stable `id` attribute rather than by prose matching |
| A3 | **the honesty sentence, and no Class claim** (§11) | the report prints: analytic weighting within 0.05 dB of **IEC 61672-1:2013 Table 3** (**not Table 2**), the digital filter error as published, **no class claim**, and — new here — that the instrument's linear operating range, overload and under-range behaviour have not been verified against IEC 61672-1 clause 3.28's validity definition. Asserted as a substring |
| A4 | it says what it does not claim | the report states in its own words that its content list is assembled from market practice and is **not** claimed conformant with ISO 1996-2:2017 clause 13, whose body is paywalled (§13 Q9) |
| A5 | **the integrity hash** (§13 Q6 default) | a hash over the log segments is printed; re-rendering the same payload gives the same hash **bitwise**; altering one byte of one segment changes it. 10EaZy's idea, copied wholesale, and the reason is that the artefact's whole purpose is to be believed by somebody who was not there |
| A6 | Ln labels carry all four parts | `L_AF90,15min`, never `L90` — weighting, detector, N and interval, ISO 1996-1 cl. 3.1.3's own notation. A grep for a bare `L90` / `L10` / `L50` in the rendered output returns **nothing** |
| A7 | **rounding matches the desktop exactly** | every dB in the report is `formatTrim`'s output, every Hz is `formatHz`'s, every 0..1 is `formatAgreement`'s — the **C++** functions, called directly, not reimplemented in the template. That is what makes the desktop half of L-API §12 constraint 2 true here by construction rather than by discipline |
| A8 | **no CSS class trips the honesty guard** (SPL-R10) | a grep over `SplReportStyle.h` and `SplReportScript.h` for `[Cc][Ll][Aa][Ss][Ss][ \t_-]*[01]` returns nothing. Task G shows the guard red on a deliberately-added `.class-1` and green after removal |

- [ ] **Accept:** OFF ctest rises; GLOBS +4; every file under 400. **Commit:** `feat(app): the SPL report as one self-contained HTML document -- nine sections, the honesty sentence, and an integrity hash`

---

## Wave 4b — the viewer: the same document over L-API (**Q8 DEFAULT: ships, LAST, gated**)

**CUT by the owner, 2026-09-25 (in the orchestrator's chat).** Not a gate
failure — Gate 2 (the Chrome LNA test below) was never run, and the owner's
decision made running it moot rather than answering it. The fallback this
section already named is what happened: the record §12 constraint-2 obligation
is recorded as **UNTESTED FOR THE VIEWER**, and that sentence is in
`docs/reports/009-spl-pro.md`. Wave 4a (the report, above) is untouched — it
never depended on this gate. The rest of this section is kept as written,
because it is the record of what Wave 4b would have needed had it not been cut.

**Two gates, both outside this lane's control. If either is unmet when Wave 4a finishes, Wave 4b is CUT, the record §12 constraint-2 obligation is recorded as UNTESTED FOR THE VIEWER, and that sentence goes in the report (§9's own fallback).**

**Gate 1 — L-API station 4 has landed, AND the static-asset mount has been agreed. Two things, not one.** `ApiServer`, its `Host` allowlist and its rate limiter arrive with that lane's **Task I** (the server, now **OFF**-config and socket-tested on three OSes) and **Task J** (composition-root wiring, that lane's only ON task) — `docs/plans/2026-09-17-remote-api-impl-plan.md`, merged as PR #14 at `a02fb29`. **The static-asset mount is a different matter: that plan puts it under *What this lane does NOT include*.** So serving the viewer's HTML from the same origin is an addition this lane proposes to L-API's files and cannot assume; it is item 6 of "Open, needs a human". This lane **builds no server** and must not build a parallel one: record §12 and L-API §12 forbid a second socket, port, bind default or auth model.

**Port: `4736`, not `4737`.** L-API's API-R14 moved it — `4737` is IANA `ipdr-sp` (IPDR/SP, registered 2005-08), while `4734`, `4735` and `4736` are absent from the registry. An earlier revision of this plan wrote `4737` in the procedure below, which was the record's number before that check was run.

**Gate 2 — the Chrome Local Network Access test, which nobody has run.**

> **THE EXACT MANUAL TEST THE OWNER RUNS, AND WHAT THEY SHOULD SEE.**
> On a machine with **Chrome 142 or newer** (`chrome://version` — write the exact build down), start `rtatool` with the API enabled on `127.0.0.1:4736`. Open `http://127.0.0.1:4736/viewer` — the page L-API serves from the same origin. **What to look for, in this order:**
> 1. **No permission prompt.** Chrome's Local Network Access prompt reads roughly *"<site> wants to access devices on your local network"* with **Allow** / **Block**. If it appears, **that is the failing result** and it is worth just as much as a pass — write down the exact wording.
> 2. The live numbers update — a dB figure changing at the poll rate, not a blank panel.
> 3. Open DevTools → **Console**: there must be **no** error mentioning `local network access`, `private network`, or `ERR_BLOCKED_BY_PRIVATE_NETWORK_ACCESS_CHECKS`.
> 4. DevTools → **Network**: the `GET /api/v1/spl` request is status **200**, not `(blocked:...)`.
> 5. Repeat once with `http://localhost:4736/viewer` — the hostname, not the literal IP — because `Host`-header handling and LNA's address-space classification are **not** the same check, and the two can disagree.
>
> **Report back four things:** the Chrome version string, whether a prompt appeared (and its exact words), the Network tab's status for `/api/v1/spl`, and any console line naming local or private network access. This follows from LNA's same-address-space model and **was not found stated verbatim** anywhere (PR #11's own station-1 UNVERIFIED item 7, and record §14's last row). It is an afternoon; it gates the viewer and nothing else.

### W4b-A — the SPL endpoints on L-API's server (app, OFF for the serialiser / ON for the route)

**Files.** Modify **L-API's own files, which station 4 of THAT lane creates** — `app/src/api/` exists on no branch today, so each is marked **NEW-BY-L-API** rather than NEW or existing, which is how the header's existence-check rule is satisfied without pretending they are here: `app/src/api/ApiSerialise.h` (**NEW-BY-L-API**; add `serialiseSpl`, `serialiseSplHistory`, `serialiseSplAlarms`), a new `app/src/api/ApiSerialiseSpl.cpp` (**NEW**, ≤ 300 — its own translation unit, because L-API's Tasks D and E already split `ApiSerialise.cpp` from `ApiSerialiseSpatial.cpp` at the 400-line cap and a third block belongs in a third file), `app/src/api/ApiServer.cpp` (**NEW-BY-L-API**; three routes), and the `available` list in `serialiseStatus` (add `"spl"`, which L-API's own D4 asserts is **absent** today).

| # | case | acceptance |
|---|---|---|
| B1 | **`"spl"` appears only now** | `/api/v1/status`'s `available` gains `"spl"`, and a test asserts it is **absent** when `snapshot->spl` is `nullopt`. This is the entry L-API §12.1 said "appears when the Meters track lands it, and L6a is blocked on that" — Wave 0 is what unblocked it |
| B2 | **no second listener, and it is structural** | `git diff main --stat` shows **no** new file matching `*Server*`, `*Socket*`, `*Listen*`; a grep over this lane's whole diff for `httplib`, `bind(`, `listen(`, `createListener` returns nothing outside `ApiServer.cpp`. `no_server_library_outside_api` (L-API's Task J guard) stays green with its `ALLOW` unchanged |
| B3 | history is a separate endpoint from the snapshot | `/api/v1/spl` is the live block; `/api/v1/spl/history` is the strip. §D2: 10EaZy's sluggish 5 s update is the cost of re-sending the whole history every tick |
| B4 | **`leqBufferFill` and `state` both travel** | the two fields §9 names as the ones a naive implementation drops. `state` is **server-computed** and the page never re-derives it |
| B5 | gated on logging, following Smaart | the routes return **404** when no logging session is running. "A remote surface that exists when nothing is being recorded is attack surface with no user" (§9) |

### W4b-B — the viewer's JavaScript half of the rounding constraint (OFF; record §9, L-API §11 item 13, §12 constraint 2)

**Files.** Extend `app/src/export/SplReportScript.h`; create `app/tests/test_spl_viewer_rounding.cpp` (**NEW**, ≤ 200).

| # | case | acceptance |
|---|---|---|
| B6 | **the JS reimplements the three, and is asserted against the same values the C++ is** | for the literal set `{1000.4, 999.6, 0.0}` → `"1000 Hz"`; `{-3.0, 3.0, 0.0, -3.2145123}` → `"-3.0 dB"`, `"3.0 dB"`, `"0.0 dB"`, `"-3.2 dB"`; `{0.7071, 1.0, 0.0, 0.9731445}` → `"0.71"`, `"1.00"`, `"0.00"`, `"0.97"` — three of four in each set are **the literals `app/tests/test_readouts.cpp:100-116` already pins**; `-3.2145123` and `0.9731445` come from L-API's own golden and its Task G (`magnitudeDb[0]` and `coherence[0]`), and both round as stated. The **unit suffix is inside the string**. A C++ test cannot execute JavaScript, so the assertion is over the JS source's own embedded expected-value table, and the test's name says **exactly what it does and does not prove**. If this task is cut with Wave 4b, record §12 constraint 2 is "untested for the viewer" and the report says so |
| B7 | no fourth formatter, on either side | a grep over `SplReportScript.h` for `toFixed(2)` on a dB quantity, or `toFixed(1)` on a hertz quantity, returns nothing; and `formatDb` / `formatCoherence` — the two names an implementer would guess and which **do not exist** — appear nowhere in this lane's diff (record §9's own correction, PR #12 defect E1) |

- [ ] **Commit:** `feat(app): the SPL web viewer as a client of L-API's one surface -- three GET routes, no second socket`

---

## Task G — prove the guards still GUARD, and fix the one that is wrong (both configs; record §11, §13 Q10)

*Runs last. A guard that has silently stopped guarding is worse than no guard, and every one of these is reachable from this lane's diff.*

### G1 — the guards, each shown red in the shape that actually trips it

| guard | how this lane reaches it | made red by |
|---|---|---|
| `core_has_no_framework_deps` | **eight** new `core/meter` files **and five** new `core/tests/*.cpp` — its default globs are `include/*.h|hpp`, `src/*.h|cpp` **and `tests/*.cpp|h|hpp`** (`check_no_framework_deps.cmake:40-47`), so the scanned count rises by **13** | adding `#include <juce_core/juce_core.h>` to `Block.h`; revert |
| **`core_makes_no_class_1_claim`** | `include/rta/meter/*.h` and `src/meter/*.cpp` are globbed, so the **eight** new core files are covered for free (`check_no_conformance_claim.cmake:38-39`); the **five** new core test files are **not**, and SPL-R9 adds them by name | writing `IEC 61672-1 Class 1` into `Dose.h`, and again into `test_dose_tables.cpp` — **the second one is red only after SPL-R9's test-file extension lands**, which is the point of showing it. Scanned count rises by **8 + 5 = 13**, read from `OK (N files scanned)`, never predicted |
| **`report_makes_no_class_1_claim`** (**NEW**, SPL-R9) | the report and export templates | (a) `Class 1` in `SplReport.cpp`; (b) **the false positive**: a CSS rule `.class-1 { }` in `SplReportStyle.h`, which the regex matches and which is why SPL-R10 exists. Both shown red, both reverted, and the second is what makes the naming constraint a test |
| `measure_has_no_framework_deps` | **Three numbers, because two conventions are in play and an earlier revision printed a fourth that is neither.** Per wave: W0-B0 **+1** *(automatic — `AllocationProbe.h` is caught by the trailing `*.h` glob; `AllocationProbe.cpp` is a `.cpp` in `app/tests/` and is never globbed)*, W0-B **+3**, W0-C **+0**, W0-D **+0** *(both `AnalysisThread` files use JUCE and must never be listed)*, W0-E **+0**, W1-A–D **+0** *(core only)*, W1-E **+1**, W2-A **+2**, W2-B **+2**, W2-C **+2**, W2-D **+1**, W3-A **+2**, W3-B/C **+0**, W4a **+4**. So: **hand-appended through Wave 4a = 17**; **total scanned rise through Wave 4a = 18** (the 17 plus W0-B0's automatic one); **and 18 / 19 if Wave 4b ships**, which adds `ApiSerialiseSpl.cpp` by hand. The earlier "19" was the Wave-4b-inclusive total printed as if it were the Wave-4a figure, and its own list summed to 18 — defect 7's shape reappearing inside the fix for defect 7. **Read the figure from the guard's own `OK (N files scanned)` line and recompute the list at `main`; never trust a total in a plan** | misspelling one entry — the failure mode `memory/core-must-not-include-frameworks.md` records, where coverage shrinks **silently**. The proof it did not is the risen `OK (N files scanned)` count, read from the output |
| `no_std_atomic_over_shared_ptr` | every new `.h`/`.cpp` under `core/` and `app/` is scanned | its own sentinel; count rises. Note the macro→function fix at `3d4eca1` is landed — do not re-report it |
| `test_names_are_ascii` | every new `TEST_CASE` | a non-ASCII test name; revert |
| `audioio_callback_has_no_rt_hazards`, `audioio_scoped_no_denormals_is_first`, `output_render_has_no_rt_hazards` | **not reached** — proven by `git diff main --stat -- platform/` being empty | still run, still green |

### G2 — the "Table 2" fix in the guard (record §13 Q10) — **code, so it belongs to station 4 and not to this PR**

**Files.** Modify `core/tests/check_no_conformance_claim.cmake` **only**, two places:

- `:18` — the comment reads "the full **Table 2** tolerance envelope is paywalled". **IEC 61672-1:2013's Table 2 is "Acceptance limits for deviations of directional response from the design goal"; the frequency-weighting tolerances are Table 3, "Frequency weightings and acceptance limits".** Change to Table 3.
- `:61` — the same wrong number inside the `FATAL_ERROR` message a contributor actually reads. Change to Table 3.

The docs half was already fixed on PR #12 (`docs/dsp/2026-08-27-weighting-and-meters.md:41`). **A purchaser acting on the guard's wording would buy against the wrong reference**, which is why this is not cosmetic.

- [ ] **Accept:** the guard still passes with the same scanned count; a grep for `Table 2` over `core/tests/` returns nothing.
- [ ] **Commit:** `fix(core): the weighting tolerances are IEC 61672-1:2013 Table 3, not Table 2 -- the guard's own comment and error message`

---

## Numbers the builder must measure, not copy

*Every figure below is a prediction to falsify. Measure the baselines **before** W0-A on the branch point; if a measurement disagrees, the plan is wrong and the orchestrator hears about it.*

| quantity | how |
|---|---|
| ctest OFF baseline `base_off` | `--clean-first` OFF build in `build-l6a`. **Do not copy 649** — L-API's lane may have landed since. Read it from ctest |
| ctest ON baseline `base_on` | `--clean-first` ON build in `build-l6a-on`. Do not copy 717 |
| OFF/ON after every task | re-read from ctest each time a `TEST_CASE` group lands. Never predicted here |
| **`sizeof(rta::meter::Block)`** | `static_assert`, and **read the compiler's answer on MSVC, GCC and Clang**. 40 is the prediction; §4's whole ring table is sized on it |
| the ring's measured allocation at 1 h / 8 h / 24 h / 7 d | the counting allocator in W2-A2, against 144 000 / 1 152 000 / 3 456 000 / 24 192 000 B |
| every guard's `OK (N files scanned)` | `core_has_no_framework_deps`, `core_makes_no_class_1_claim`, **`report_makes_no_class_1_claim`**, `measure_has_no_framework_deps`, `no_std_atomic_over_shared_ptr` — all five rise; read each from its own line |
| W1-A2's worst Ln residual | printed beside the `0.05 dB` bound, over every fixture distribution |
| W1-D2b's worst `100·r/T_exact` margin | printed per row for all 51 G-16a rows and all 50 Table 1-1 rows; the 99 dBA row's `0.4472 %` against its `0.0882 %` bound is printed **as the excluded erratum**, not hidden |
| W0-B2's Fast-detector residual | printed beside the derived `1 − exp(−0.05/0.125)` tolerance |
| the rendered report's byte size | a number; if a "self-contained" document is 40 MB, the SVG history strip needs decimation and the plan did not say so |
| **the round-off floor of a 48 000-sample naive sum** | measured on this branch at `a = 0.1` → `2.636e-12` and `a = 0.3` → `2.874e-12`, which is why W0-A A2 states **1e-9** and not 1e-12. Re-measure on GCC and Clang: a different libm changes the figure, not the conclusion |
| **`sizeof(Block)` after `droppedSamples` takes the padding** | still **40** is the prediction. If a compiler says 48, §4's whole ring table moves and the plan is wrong |
| **T12's bytes after W0-B0 moves the probe** | `bytes(8) − bytes(4)` against `4·sizeof(PositionSummary) + 4096`. Measure both sides; the absolute figures are fixture-specific and must not be copied |
| the block-rate cost on the analysis thread | the added wall time per drain at 48 kHz, measured. It must not move `kMinPublishIntervalMs`'s behaviour (`AnalysisThread.cpp:21`) |
| MSVC `/W4` warnings, and the CI matrix | grep the build log for `warning C` → 0. CI green on ubuntu **and** macos **and** windows — **or, while Actions is billing-blocked, the OFF tally measured here and pasted, and re-measured by the verifier** |
| **the Chrome LNA result** | the four things named in Wave 4b Gate 2, from the owner. It is the only figure in this plan a machine in this repo cannot produce |

---

## Build sequence and acceptance gate

1. **W0-A** (block + energy sum, core OFF) — everything downstream consumes it.
2. **W0-B0** (the shared allocation probe, app OFF — W0-B4 and W2-A1 both need it and neither can bring its own) → **W0-B** (the chain, app OFF) → **W0-C** (`Snapshot::spl`, app OFF) → **W0-D** (drain feed, ON) → **W0-E** (the seam, OFF).
3. **W1-A** ∥ **W1-C** ∥ **W1-D** — three independent core files; **W1-B** extends W0-A's test file so it follows W0-A; **W1-E** needs W1-D.
4. **W2-A** (ring) → **W2-B** (alarms, needs W1-C) → **W2-C** (log) → **W2-D** (pane + specimen).
5. **W3-A** → **W3-B** → **W3-C** *(cut whole if Q2 flips)*.
6. **W4a-A** (report, needs W2-C and W3-C).
7. **W4b-A** → **W4b-B** *(cut whole if Q8 flips or either gate is unmet)*.
8. **Task G** last, in both configs.

**W0-A, W0-B, W0-C, W0-E and all of Wave 1, Wave 2 (except the ON specimen), Wave 3A/3C and Wave 4a are OFF** — which is deliberate: if the lane stopped after Wave 2, the block clock, the energy sum, the histogram bound, both dose fixtures, the headroom identity, the ring's bound and the log's round trip would already be proven on three operating systems with no sound card and no socket.

**Acceptance gate.** OFF and ON ctest both green at the measured counts; **0 `warning C`** in both; the forced-fallback OFF config (`-DRTA_FORCE_ATOMIC_SHARED_PTR_FALLBACK=ON`) green; every guard in Task G green with its scanned count risen and each shown red once **in the shape that actually trips it** (including the `report_makes_no_class_1_claim` false positive); `git diff main --stat -- platform/ ui/` **empty**; every new file **< 400 lines**; `rtatool_snapshot` writes a readable `shots/preview-spl.png`; the structural greps return no match — no second listener, no peak in the dose, no invented hysteresis, **no run-time network call in the frozen report** (W4a-A1's six run-time patterns), and **exactly one `operator new` definition under `app/tests/`** (W0-B0); and **`rta::meter::Leq` still has no `app/` caller** (SPL-R3) — grep it and paste the count.

---

## What this lane does NOT include (deferred; record §12)

- **A second HTTP surface of any kind.** The viewer rides L-API's server, port, `Host` check, rate limit and token. Record §12 and L-API §12.
- **Repairing `rta::meter::Leq`** — its unbounded `noexcept` `history_.push_back` (`Leq.cpp:49,64`) and its double sort (`:22,105-110`). SPL-R3: nothing in this lane calls it, so nothing in this lane ships that defect. Fixing it is a separate, testable change.
- **A second read cursor on `rta::dsp::RingBuffer`**, which is what would make the routed SPL path independent of the reference (SPL-R1). That is a `platform/`+`core/` change and it needs its own record.
- **True peak.** `peakDb` is sampled peak; the 4× polyphase meter the weighting record deferred never came, and the readout says `sampled` (§12).
- **Ln over intervals shorter than the session.** The histogram is cumulative; a per-interval Ln needs a histogram per interval or a re-scan, and no source read in station 1 says which instruments do (§12).
- **A signed report.** §13 Q6's default is a hash; a signature needs a key and a story about where the key lives.
- **Jurisdiction packs** (VLAREM, DIN 15905-5, V-NISSG) and which dose preset ships enabled in a market build — a productization question (L9), not a DSP one (§12).
- **Persisting the alarm, dose and log-span settings.** There is no preferences store in `app/src` (SPL-R11); they are constructed by the composition root from `SplConfig`'s defaults.
- **Moving `rta::trace::LevelUnit` into `measure/`** so the app has one enum for the dBFS/dBSPL fact (SPL-R5). Better, and out of this lane unless the orchestrator says otherwise.
- **Anything about IEC 61672-1 clauses 5.11 / 5.12 / 5.17 / 5.18 bodies**, which are paywalled. Overload and under-range are *recorded* here as flags; conformance to their specifications is not claimed (§12).
- **Any Class 1 or Class 2 claim, anywhere, in any artefact** — now enforced in `app/` as well as `core/` (SPL-R9).

---

## Open, needs a human

1. **The three scope defaults above.** Q1 (the seam), Q2 (the calibration flow), Q8 (the viewer). Each is taken as the record's own proposal, and each flip cost is stated. **Q1 is the one worth a sentence before W0-E commits**, because flipping it afterwards is a schema bump and a golden regeneration rather than a constant edit.
2. **The Chrome LNA test** (Wave 4b Gate 2). Nobody has run it; the procedure and the expected observations are written above. It is an afternoon, and it decides whether Wave 4b starts.
3. **Q4's second half — tables or formulas?** The default taken is formulas, and W1-D2's own fixture already leans that way because the derived bound *detects* the 99 dBA erratum. If the owner wants the published tables reproduced **including** the errata, W1-D2c inverts: the fixture asserts 1139 s and the formula becomes the thing under tolerance. Different product, same afternoon.
4. **Q7's numbers.** Default log span 8 h, segment one hour of blocks, never delete. VLAREM requires ≥ 1 month of registered data and the Swiss cantonal summaries say 6 months (UNVERIFIED — the federal source 502'd), so a monitoring install would want a different default from a show. One sentence changes two constants.
5. **Q3's Ln percentages.** Six slots, defaulting L1/L5/L10/L50/L90/L95. Smaart's report prints three. If three is what the owner wants printed, the other three still accumulate for free — the histogram costs the same.
6. **Whether L6a may modify L-API's `ApiSerialise` / `ApiServer` files directly** (Wave 4b-A), or whether those edits belong to L-API's own next pass with L6a supplying the serialiser — **and who adds the static-asset mount**, which L-API's plan explicitly puts under *What this lane does NOT include*. Either works; doing it twice does not, and assuming it exists is worse. A sequencing call, not a design one.
7. **Which `BlockFlag`s exclude a block from `combineBlocks`** (defect 14). The default taken is **`CalibrationInvalid` alone**, because it is the only one with a published normative criterion behind it (ISO 1996-2 cl. 5.2's discard rule, §8), while excluding `Overload` would delete the loudest moment of the show from the compliance number and excluding `UnderRange` would bias it up. **This changes every published Leq during a loud show**, so it wants one sentence even though a default is taken and five fixtures pin it (W0-A A9–A13). It is carried as **Q11** in `docs/HUMAN-QA-QUEUE.md`. The governing references — IEC 61672-1 cl. 3.28's validity definition and ISO 1996-2 cl. 10.3 "incomplete or corrupted data" — are **paywalled and unread**, so this project cannot claim a standard basis either way and does not.

---

## For the station-4 builder, first read

1. **The record `docs/dsp/2026-09-16-spl-pro-l6a.md` is binding.** This plan implements its §2–§11 and nothing past them, and flags **SPL-R1..R12**, which the orchestrator amends in the record **first**. Do not silently re-decide any of them in code.
2. **Read the two verifier threads on PR #12** (`gh pr view 12 --comments`). Ten defects were confirmed and fixed in round 1 and five more in round 2. Two matter to a builder specifically: **E1** — the record once named `formatDb` and `formatCoherence`, and **neither exists**; the real names are `formatHz` / **`formatTrim`** / **`formatAgreement`** at `app/src/view/Readouts.h:72,79,87`. And **D1–D3** — §7 was once argued from a six-row CDC summary that is not NIOSH's table; a builder working from a cached memory of the record will rebuild the impossible `D = 100 %` fixture.
3. **Read `docs/plans/2026-09-17-remote-api-impl-plan.md`** (merged as PR #14 at `a02fb29`) before Wave 4b. Its **Task G** is the desktop half of the rounding constraint and it hands this lane the JavaScript half by name; its **Tasks I and J** are half of Wave 4b's Gate 1 — the other half, the static-asset mount, that lane explicitly does **not** build. Its port is **4736** (API-R14), not the record's 4737.
4. **Order is fixed by dependency**, and it is in "Build sequence" above. One commit per task. Everything up to Wave 2 needs no socket and no JUCE.
5. **The failing test first, seen to fail** (the missing `#include`, the missing member), then the header, then the body. Paste the command and its output for every "done": a green build proves it compiles, not that the numbers are right (CLAUDE.md, "Verification standard").
6. **`memory/` files this lane actually consumes**, not a reading list: `a-placeholder-for-an-absent-result-erases-its-state.md` (why W1-A5, W0-C1 and W1-C3 assert absence), `a-threshold-read-off-a-grid-is-that-grids-floor.md` (why no amber margin and no widened dose bound ship), `a-fixture-can-be-too-well-behaved-to-fail.md` (why W0-B3 puts the overload run **across** the boundary), `core-must-not-include-frameworks.md` (why the GLOBS count is read and not predicted), `a-default-must-be-run-through-the-gate-it-feeds.md` (why W1-A7 exists at all — the histogram base is the default this lane nearly shipped without running through its own gate), `a-cap-checked-on-the-drain-path-is-unchecked-on-the-publish-path.md` (why W0-D's tap sits **above** `AnalysisThread.cpp:256` and W0-D1b tests route position 8), `a-misconfigured-build-goes-99-percent-of-the-way.md` (Visual Studio generator, never Ninja), `mutation-testing-needs-the-exe-deleted-first.md` (a header mutation is not recompiled unless a dependent `.cpp` is touched — finish with a full rebuild and hash-check against HEAD).
7. **If you find yourself constructing a `rta::meter::Leq` in `app/`, stop.** SPL-R3 is why: it would ship an unbounded `push_back` inside a `noexcept` function into a session that runs for eight hours.
8. **Every count in this plan is a prediction you are expected to falsify if it is wrong.**
