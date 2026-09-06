# L6b — multichannel: spatial averaging, routing, sequencing, presets

> **For agentic workers:** REQUIRED SUB-SKILL `superpowers:subagent-driven-development` or `superpowers:executing-plans`. Steps are checkboxes. Station 5 (adversarial verify, reviewer with **no Write tools**) is not optional.

*2026-09-06, lane L6b, station 3. Written from `docs/dsp/2026-09-06-multichannel-l6b.md` (THE RECORD — every task names the § it implements) after reading the real files, not the record's description of them. Worktree `.claude\worktrees\continue-pending-work-f8aa84`, HEAD `959a197`.*

**Split.** **L6b-a** is `core/` only and builds and passes with `RTA_BUILD_APP=OFF`. **L6b-b** is `app/` + `platform/types` + `ui/`, and depends on L6b-a.

## Global constraints

- SPDX header on every new file. `core/` never includes JUCE, Qt or a device API. Comments explain *why a formula is that formula*.
- **Hard cap 400 lines, aim 300**, headers too. Every file is budgeted below. Never assert a value the implementation produced: closed form, standard, or golden — a regression lock only if labelled one.
- **`core/src/dsp/DualFftEngine.cpp` (321), `core/include/rta/dsp/DualFftEngine.h` and `core/tests/test_dualfft.cpp` (400) MUST NOT be touched.** Prove it at the end of each half: `git diff --stat main -- core/src/dsp/DualFftEngine.cpp core/include/rta/dsp/DualFftEngine.h core/tests/test_dualfft.cpp` is empty.
- Frequency reads a whole number of hertz; dB one decimal; coherence **and `phaseAgreement`** two decimals; contributor count a bare integer.
- Venv at the **main checkout**, not this worktree: `D:\DEV CAVE EP3\PRJ010-RTA-TOOL\.venv\Scripts\python.exe` — numpy 2.5.2, scipy 1.18.1, both verified 2026-09-06. Do not build a second one.
- Build dirs **`build-l6b`** (OFF) / **`build-l6b-on`** (ON), Visual Studio generator, never Ninja. **Do not touch `build-l6b-base-*`** — another session owns those. Ignore IDE clang diagnostics; MSVC is the truth.

```
cmake -S . -B build-l6b -G "Visual Studio 18 2026" -A x64
cmake --build build-l6b --config Release --parallel
ctest --test-dir build-l6b -C Release --output-on-failure
cmake -S . -B build-l6b-on -G "Visual Studio 18 2026" -A x64 -DRTA_BUILD_APP=ON -DRTA_JUCE_PATH="D:/DEV CAVE EP3/PROJECT005-AZ-handsfree/external/JUCE"
```

### Naming that cannot trip the coherence guard

`check_coherence_gate.cmake:44-47` greps every `core/src/*.cpp` and `core/include/*.h` except `TransferEstimator.cpp`, case-sensitively, for `(\.|->)coherence\s*=`, `coherence\s*=\s*std::vector`, `coherence\s*\.\s*emplace`, and `&\s*ident\s*=[^;]*(\.|->)coherence` (whose `[^;]*` spans lines). Therefore:

- Result fields are **`weightedCoherence`** and **`phaseAgreement`**: the capital `C` defeats all four patterns, and no `.` precedes `coherence` inside the identifier either — two independent reasons, both kept on purpose.
- Read only as `positions[i].coherence.has_value()` and `(*positions[i].coherence)[k]`, the `MtwEngine.cpp:81-92` idiom whose comment says why. **Never** `const auto& c = snap.coherence;` **nor** `const auto& g = *snap.coherence;` — pattern 4 catches both. Never name a local, parameter or lambda capture `coherence`.
- `core/tests/*.cpp` is **outside** the glob, so a fixture may write `snap.coherence = std::vector<float>(bins, 0.9f);` freely. That is how every hand-built gate-passed fixture below is made, and why task A5's deliberate RED must go in a file under `core/src/`.

## Record ambiguities found while planning

Take these to the orchestrator; do not resolve them silently in code.

**R1 — §11 T10's guard counts are for one file trio, not this lane.** T10 says core 96→99, gate 62→64, polynomial 113→116, i.e. `+1 .h +1 .cpp +1 test`. L6b-a adds 3 headers, 3 sources, 4 test sources and 1 `.py`. Derived from the real globs (enumerated 2026-09-06, reproducing today's 96 / 62 / 113 exactly): **core → 106, gate → 68, polynomial → 124.** *Proposed:* replace the three figures with the arithmetic, and say the count is read from the guard's own output.

**R2 — §11 T8's "two-term float tolerance" is the wrong shape here.** `memory/float32-fft-precision.md` prescribes `|expected|·1e-5 + peak·1e-6` for values reached *through* `RealFft`. The golden's combine (A4) is fed from golden **doubles**; nothing float32 touches it but the result's own storage. *Proposed:* the two-term form wherever a real engine is in the path (A2), and `epsilon(1e-6)` / `margin(1e-6)` — about 8× float32's 1.19e-7 relative — for a combine checked against golden inputs.

**R3 — §2 and T2 say phase is absent "where `R` is below float resolution" and name no number.** *Proposed:* `R <= contributors * DBL_EPSILON`, the rounding floor of a sum of `contributors` unit vectors — derived, not read off a grid (`memory/a-threshold-read-off-a-grid-is-that-grids-floor.md`). `R` is on screen beside the phase either way, so the choice is a floor, not a judgement.

**R4 — §3's muted member versus the contributor count.** §3 says `u_i = 0` is "excluded everywhere, exactly as if ungated" (so **not** a contributor) and also defines `noWeight` as "contributors exist and every one carries `W = 0`". Both hold only if a muted member never raises `contributors`. *Proposed:* state it in the header — an all-muted group reports `noContributor`, not `noWeight`.

**R5 — §11 T12's "within 2× of the N = 1 figure" is a fudge factor, not the property.** What §6 buys is publish cost **O(1) in N**; once N summaries are counted, 2× is unfalsifiable. *Proposed:* assert `bytes(8) - bytes(4) <= 4*sizeof(PositionSummary) + 4096`, which four full per-position publishes (≈ 2.21 MB each, research Part C) cannot satisfy. Keep 2× as a report headline only.

**R6 — §11 T11 wants two `Analyser`s on "identical reference hops"; §6 lets each TF name its own reference.** Not a contradiction, but the assertion only means something when both name the same channel. *Resolved here:* T11 asserts equal hop counters for two TFs sharing a reference **and** independent counters for two TFs that do not.

---

# L6b-a — `core/`

OFF baseline **390** on `959a197`. A1 +8, A2 +2, A3 +2, A4 +1, A5 +0 → **403** OFF, **455** ON (`442 + 13`).

## Task A1 — `spatialAverage`, the weighted combine (record §2–§5)

**Files.** Create `core/include/rta/dsp/SpatialAverage.h` (≤ 170), `core/src/dsp/SpatialAverage.cpp` (≤ 240), `core/tests/test_spatial_average.cpp` (≤ 330); modify `core/CMakeLists.txt`, `core/tests/CMakeLists.txt`.

```cpp
enum class SpatialMode { Db, Power };                    // §2: dB is the default
enum class SpatialAbsence : std::uint8_t { Present, NoContributor, NoWeight };
struct SpatialBinState {
    SpatialAbsence absence = SpatialAbsence::NoContributor;
    bool phasePresent = false;       // R above the R3 floor
    std::uint16_t contributors = 0;  // gate-passed AND u > 0 (R4)
};
struct SpatialAverageResult {
    SpatialMode mode = SpatialMode::Db;
    double sampleRate = 0.0, binWidthHz = 0.0;
    std::vector<float> magnitudeDb, phaseRadians;
    std::vector<float> phaseAgreement;     // R -- NOT a coherence estimate
    std::vector<float> weightedCoherence;  // sum(u*g2)/sum(u) -- NOT one either
    std::vector<SpatialBinState> bins;
};
[[nodiscard]] std::optional<SpatialAverageResult>
spatialAverage(std::span<const TransferSnapshot> positions,
               std::span<const double> u, SpatialMode mode = SpatialMode::Db);
```

**Rules the body encodes, each with its reason in a comment.** (1) `throw std::invalid_argument` on empty `positions`, `u.size() != positions.size()`, any `u_i` negative or non-finite, or differing `binWidthHz` / `sampleRate` / `h.size()` — exact `!=` on the doubles, because every engine comes from one shared `Config` (§5), so a difference is a programming error, not an operator situation. (2) Contributor at a bin iff `coherence.has_value()` **and** `u_i > 0` (R4); `W_i(k) = u_i * (*positions[i].coherence)[k]`. (3) `L = Σ W_i·20log10|h_i| / ΣW` computed from **`h[k]`**, never from `magnitudeDb[k]`, which is already floored at −120 — averaging floored values biases silent bins; floor the **result** once at `kMagnitudeFloorDb`. (4) `|h_i| == 0` needs no special branch: `estimateH1/H2/Hv` return `{0,0}` exactly under the conditions that make `magnitudeSquaredCoherence` return `0.0` (`TransferEstimator.cpp:9-48`), so `W_i = 0` there and no `log10(0)` or `h/|h|` is ever formed — skip on `W_i == 0.0` anyway, so a future estimator cannot reintroduce a NaN. (5) `ΣW == 0` leaves magnitude and phase untouched with `absence = contributors == 0 ? NoContributor : NoWeight`; `ΣW == 0` at **every** bin returns `std::nullopt` (`memory/a-fixed-defect-returns-through-the-silent-fallback.md`). (6) `weightedCoherence = Σ u_i γ²_i / Σ u_i` over contributors, present even where the magnitude is absent, because a bin whose every γ² is 0 is a measured 0.

**RED first.** `core/tests/test_spatial_average.cpp`, 8 `TEST_CASE`s written against a header that does not exist, so the build fails at the `#include`. Paste that failure.

| # | case | expected, derived |
|---|---|---|
| T1 | N copies average to the input | `L == in.magnitudeDb[k]`, phase equal, `R == 1.0` (`margin(1e-6)`), `contributors == N`, `Present`, for N ∈ {1,2,5} |
| T2 | `H` and `−H` keep the dB mean, lose the phase | equal \|H\| and γ² → `L == 20log10\|H\|`; `R == 0` (`margin(3e-16)`, R3's floor at N=2); `phasePresent == false`; a vector mean would have read −120 |
| T3 | the weight is trim × gated coherence | A: γ²=1 at `a`; B: γ²=`c` at `a+Δ` → `L = a + cΔ/(1+c)`. At `a=−6, Δ=4, c=0.5` → **−4.666666666666667**. `u_B=0` → `L = −6` exactly, `contributors == 1` (R4). `c=1`, equal `u` → `L = a + Δ/2 =` **−4.0** |
| T4 | Power differs by the SMPTE clause's own numbers | `L_pow = 10log10((10^{a/10}+c·10^{(a+Δ)/10})/(1+c))`. At `a=0, c=1`: Δ=2 → 1.1141260713035854, Δ=3 → 1.7540486677250415, Δ=4 → **2.4451046744531246**, i.e. **0.4451046744531246 dB** over the dB mean — §2's table row and T4's "0.445". At `a=−6, c=0.5, Δ=4` → −4.2276309521366855 |
| T5 | an ungated position is excluded | one of three `nullopt` → `contributors == 2`; all three `nullopt` → `!has_value()` |
| T6 | zero weight is a different absence | two gate-passed positions, γ² exactly 0.0 at bin 7 and 0.9 elsewhere → bin 7 `NoWeight` with `contributors == 2`, all others `Present`, `std::isfinite` over every array. All `u = 0` → `!has_value()` |
| T7 | mismatched grids are refused | six `REQUIRE_THROWS_AS(..., std::invalid_argument)`: `binWidthHz`, `sampleRate`, `h.size()`, `u.size()`, `u_i = −1`, empty span |
| T8 | trust and agreement are what they say | `u = {1, 2, 0.5}`, γ² = {0.9, 0.4, 0.81} → `weightedCoherence =` **0.6014285714285714**. Three unit vectors at 0/120/240°, equal weights → `R == 0` (`margin(1e-15)`; the exact sum is 1.05e-16) |

- [ ] **Accept:** `ctest --test-dir build-l6b -C Release` → **398/398**.
- [ ] **Mutation:** set `W_i = u_i` (drop γ²) → T3 and T8 must fail; revert. Then report `NoContributor` in the `ΣW == 0` branch → T6 must fail; revert.
- [ ] **Commit:** `git add core/include/rta/dsp/SpatialAverage.h core/src/dsp/SpatialAverage.cpp core/tests/test_spatial_average.cpp core/CMakeLists.txt core/tests/CMakeLists.txt` → `feat(core): the spatial average, its two absences, and the trust it reports`

## Task A2 — the MTW variant, band by band (record §5)

**Files.** Create `core/include/rta/dsp/SpatialMtw.h` (≤ 90), `core/src/dsp/SpatialMtw.cpp` (≤ 150), `core/tests/test_spatial_mtw.cpp` (≤ 230); modify both `CMakeLists.txt`.

`spatialAverageMtw(std::span<const MtwResult>, std::span<const double> u, SpatialMode)` → `std::optional<SpatialMtwResult>` holding per-band `SpatialAverageResult`s plus the stitched `frequencyHz / magnitudeDb / phaseRadians / phaseAgreement / weightedCoherence / bins`. It calls `spatialAverage` once per band over `MtwResult::bandSnapshots[b]`, then stitches with the **existing** `mtwBands()` / `mtwFrequencies()` — no second stitch and no flat coherence array in `core/`, which is exactly why `MtwResult` has none (`MtwResult.h:15-23`). Refuses `MtwConfig`s differing in `topFftSize`, `octaveCount` or `sampleRate`.

**RED first — 2 `TEST_CASE`s,** both on the **default** table (`N0=1024, K=6`, 7 bands, ≈ 47 MB per engine) with `n = 327680` samples, the L3 length that puts every band at ≥ 16 frames and therefore at `effectiveAverages == 8.5866271` — quoted from the shipped function's own literal at **`core/tests/test_mtw_layout.cpp:213`**, never from a header formula (`memory/a-header-ceiling-is-not-the-reachable-average-count.md`).

1. *Two identical positions average to either input.* The same pure-delay pair into both engines, both compensating `D`. `frequencyHz.size() == 1281` (§11 T7); the stitched magnitude equals either input within the two-term float32 tolerance `|expected|·1e-5 + bandPeak·1e-6` taken **per band** (R2); per-band `R == 1.0` within `margin(1e-6)`; `contributors == 2` everywhere.
2. *One sample of spacing gives the top band a `cos(πf/fs)` agreement.* A's measurement delayed `D`, B's delayed `D+1`, **both** engines compensating `D` — two mics at slightly different distances with one delay-finder result applied. Then `H_B/H_A = e^{−j2πf/fs}` and `R = |cos(πf/fs)|` exactly. Top band, `f = bin·48000/1024`: bin 128 → 6000 Hz → **0.9238795325112867** (`cos π/8`); bin 256 → 12000 Hz → **0.7071067811865476** (`cos π/4`); bin 384 → 18000 Hz → **0.38268343236508984**; bin 512 → 24000 Hz → **0** (`margin(1e-6)`; the closed form is 6.12e-17). `epsilon(1e-4)` elsewhere. The averaged phase is `−πf/fs`, midway between the two.

- [ ] **Accept:** **400/400** OFF. **Mutation:** average the *stitched* vectors instead of the bands — case 2's per-band `R` goes wrong at every seam; revert.
- [ ] **Commit:** `git add core/include/rta/dsp/SpatialMtw.h core/src/dsp/SpatialMtw.cpp core/tests/test_spatial_mtw.cpp core/CMakeLists.txt core/tests/CMakeLists.txt` → `feat(core): the MTW spatial average, one band at a time, the old stitch`

## Task A3 — the overload detector (record §8.1)

**Files.** Create `core/include/rta/dsp/OverloadDetector.h` (≤ 70), `core/src/dsp/OverloadDetector.cpp` (≤ 60), `core/tests/test_overload.cpp` (≤ 140); modify both `CMakeLists.txt`.

`inline constexpr float kFullScaleThreshold = 1.0f - 0x1p-15f;` and `bool hasOverload(std::span<const float> x, int runLength = 3, float threshold = kFullScaleThreshold) noexcept` — pure, no allocation, no state, run on the analysis thread over each hop and never in the callback. Smaart LE v9.1 p.78: three or more consecutive samples with `|x| >= threshold`. The threshold is the coarsest integer full scale a converter can deliver, so it is a statement in the vocabulary of the thing measured rather than a grid floor: int16 `1−2⁻¹⁵` = **0.999969482421875** (equal), int24 `1−2⁻²³` = 0.9999998807907104, int32 `1−2⁻³¹` = 0.9999999995343387 **which rounds to exactly `1.0f`**, float 1.0 — all at or above it. `1−2⁻¹⁵` and `1−2⁻²³` are exactly representable in float32 (15 and 23 mantissa bits against 24 available), so the comparison is exact and promotion to double changes nothing; verified 2026-09-06.

**RED first — 2 `TEST_CASE`s.** (a) three samples at `kFullScaleThreshold` flag; **two do not**; three at `1−2⁻¹⁴` = 0.99993896484375 do not; negatives flag (`|x|`); a run split across the span end does not carry over. (b) int16 / int24 / int32 / float full-scale runs all flag; a sine peaking at `1−2⁻¹⁴` does not, at any phase; an empty span does not; `runLength = 1` flags a single sample.

- [ ] **Accept:** **402/402** OFF. **Mutation:** change `>=` to `>` → (b)'s int16 row must fail; revert.
- [ ] **Commit:** `git add core/include/rta/dsp/OverloadDetector.h core/src/dsp/OverloadDetector.cpp core/tests/test_overload.cpp core/CMakeLists.txt core/tests/CMakeLists.txt` → `feat(core): the overload criterion, in the vocabulary of the converter`

## Task A4 — `tools/gen_spatial.py` and the golden (record §11 T8)

**Files.** Create `tools/gen_spatial.py` (≤ 190), `core/tests/golden/spatial.txt` (generated, committed, ≈ 110 KB), `core/tests/test_spatial_golden.cpp` (≤ 190); modify `core/tests/CMakeLists.txt`.

**The generator must take an argument:** `argparse`, `--out PATH` with a default, `--check` (regenerate to a temp path and diff), and `--help` exiting **before any write** — `memory/a-gen-script-runs-the-moment-you-invoke-it.md`. `tools/gen_mtw.py` is the only existing script that already does this; copy its shape. No `signal.lfilter`, no `output='ba'`: `check_no_polynomial_form.cmake` scans `tools/*.py`.

**The fixture.** `fs = 48000`, `N = 1024`, `hop = N/4 = 256`, `fifoDepth = 16`, so `n = 1024 + 15·256 = 4864` gives exactly `1 + (4864−1024)/256 = 16` frames — the whole file *is* the FIFO's window, so scipy's plain mean and the engine's FIFO mean cover the same 16 frames. 513 bins. Three positions against one reference: gains `{1.0, 0.5, 2.0}`, integer delays `{0, 3, 7}`, independent Gaussian noise at SNR `{40, 20, 6}` dB, `u = {1.0, 2.0, 0.5}`. Measured over that grid on 2026-09-06: γ² spans 0.328…1.000 and `R` spans 0.219…1.000, so the weights and the agreement both do real work instead of sitting at 1.

**One case, no time-domain rows.** The golden carries each position's `h_real`, `h_imag`, `g2` and the combined `l_db`, `phase`, `r`, `weighted_coherence`. *Argued against shipping the samples too:* the per-position `TransferSnapshot` already has two goldens with a second author (`test_transfer_golden.cpp`, `test_mtw_golden.cpp`), so re-proving `scipy.signal.csd` against `DualFftEngine` would commit ~390 KB of noise over ground L2 and L3 already hold. What has never had a second author is the **combine**, and a golden fed from spectra isolates exactly that. A real engine is in the path in A2 case 1, where it belongs.

**RED first — 1 `TEST_CASE`,** *the spatial golden from scipy matches L, phase, R and the weighted trust*: build three `TransferSnapshot`s by hand from the golden's own `h_*` / `g2` rows (a test file may assign `.coherence` — see the guard note), call `spatialAverage(..., u, SpatialMode::Db)` and compare all four combined rows at `epsilon(1e-6)` / `margin(1e-6)` (R2 — nothing float32 is in this path but the result's own storage). `REQUIRE(checked == 513)` so an empty case list fails rather than passes.

- [ ] Write the test first against a file that does not exist — `loadGolden` throws, which is the point. Then `"D:/DEV CAVE EP3/PRJ010-RTA-TOOL/.venv/Scripts/python.exe" tools/gen_spatial.py --out core/tests/golden/spatial.txt`, then again with `--check`; record byte-identity in the report.
- [ ] **Accept:** **403/403** OFF; `filter_design_has_no_polynomial_form` prints **124** and passes.
- [ ] **Mutation:** perturb one `phase` value in the committed file by `1e-3` and watch the case fail at that index; revert. Rename `spatial.txt` and confirm the test throws.
- [ ] **Commit:** `git add tools/gen_spatial.py core/tests/golden/spatial.txt core/tests/test_spatial_golden.cpp core/tests/CMakeLists.txt` → `test(core): a scipy golden for the combine, not for the transform`

## Task A5 — prove the guards are watching (record §11 T10)

No files; a procedure, and its output goes in the report. Every count is **read from the guard's own output**, never from this plan — a mis-globbed guard passes while watching nothing.

- [ ] **RED, coherence gate.** Insert `result.coherence = std::vector<float>(bins);` into `core/src/dsp/SpatialAverage.cpp`; reconfigure; run `ctest --test-dir build-l6b -C Release -R coherence_gate_is_not_bypassed`; **paste the failure naming SpatialAverage.cpp**; remove the line.
- [ ] **GREEN.** Re-run; paste `coherence-gate guard OK (68 files scanned)`. **68**, read from the output — if it still says 62, the new files fell outside the glob.
- [ ] **RED, framework deps.** `#include <juce_core/juce_core.h>` atop `SpatialAverage.h`; confirm the failure names it; remove. Count **106**. **RED, polynomial.** `signal.lfilter` inside a comment in `gen_spatial.py`; confirm; remove. Count **124**.
- [ ] **Lengths.** `wc -l` over every new core file — all under 400 — then the frozen-file diff from Global constraints.
- [ ] **Accept:** a `--clean-first` OFF build → **403/403**. **L6b-a delta +13.** **Commit:** `git add docs/reports` → `test(core): the L6b guards, made red once before they were believed`

---

# L6b-b — `app/`, `platform/types`, `ui/`

OFF: B1 +2, B2 +3, B3 +4, B4 +2, B5 +4, B6 +3, B7 +2 → **423**. ON adds B2's 1 and B7's 2 ON-only cases → **479** (`423 + 52 + 4`, where 52 is today's `442 − 390` ON-only surplus).

## Task B0 — split the three files at the cap FIRST (record §6, §9)

No behaviour change, no new `TEST_CASE`. Each seam is named because a later task adds lines to that file.

- `app/src/view/TransferView.cpp` **388** → move the anonymous namespace at `:54-141` (`drawPhaseLabels`, `drawNoReferenceState`, `drawStoredPhaseHiddenState`, `UnwrappedPhase` + `computeUnwrappedPhase`) into new `app/src/view/TransferViewDraw.{h,cpp}` (≈ 40 / ≈ 110), leaving ≈ 300. Uses JUCE — **do not** add it to the `measure_has_no_framework_deps` list.
- `app/src/measure/Analyser.cpp` **287** → move `publish()`'s two block builders at `:201-269` into new `app/src/measure/AnalyserPublish.{h,cpp}` (≈ 35 / ≈ 95) as free `makeTransferBlock(const TransferSnapshot&, int)` and `makeMtwBlock(const MtwResult&, int)`, leaving ≈ 225. JUCE-free — **add both to the guard list**.
- `app/src/trace/SessionCodec.cpp` **287** → split encode from decode: `decodeIndex` (`:158-285`) into new `app/src/trace/SessionDecode.cpp` (≈ 145) and the anonymous helpers (`:12-113`) into new `app/src/trace/SessionCodecDetail.h` (≈ 105), leaving ≈ 100. JUCE-free — **add both**.

- [ ] Modify `app/tests/CMakeLists.txt`: add `SessionDecode.cpp` and `AnalyserPublish.cpp` to `rtatool_analysis_tests`, and the four JUCE-free paths to the `measure_has_no_framework_deps` GLOBS at `:68` — that list is **explicit**, so an omitted file is silently unguarded (research Part C's trap). Modify `app/CMakeLists.txt` for `TransferViewDraw.cpp`.
- [ ] **Accept:** OFF **403/403** (a pure move); ON **455/455**; `measure_has_no_framework_deps` prints **34**.
- [ ] **Mutation:** `#include <juce_core/juce_core.h>` in `AnalyserPublish.h` — the guard must now fail naming it, which is the proof the new file is watched; remove.
- [ ] **Commit:** `git add app/src/view/TransferViewDraw.h app/src/view/TransferViewDraw.cpp app/src/view/TransferView.cpp app/src/measure/AnalyserPublish.h app/src/measure/AnalyserPublish.cpp app/src/measure/Analyser.cpp app/src/trace/SessionCodecDetail.h app/src/trace/SessionDecode.cpp app/src/trace/SessionCodec.cpp app/tests/CMakeLists.txt app/CMakeLists.txt` → `refactor(app): three files split at their seams before L6b adds to them`

## Task B1 — the routing table (record §6)

**Files.** Modify `platform/types/include/rta/platform/ChannelConfig.h` (138 → ≈ 200) and `platform/tests/test_channel_config.cpp`.

Add `std::array<std::atomic<int>, kMaxChannels> tfIndex_` beside `roles_`, with the same **relaxed** stores and loads for the same reason (class comment `:35-43`); `setTransferFunction(int channel, int tf)` bumping `configEpoch_`; `transferFunction(int) const`; and `int channelsWithRole(ChannelRole, std::span<int> out) const noexcept` filling `out` in ascending channel order, bounds-clamped like `snapshot()`, returning the count. `firstChannelWithRole` stays — still right for a single-role drain.

**RED first — 2 `TEST_CASE`s.** (a) `channelsWithRole` returns every channel of a role in index order, is empty for an unassigned role, clamps to `out.size()`, and agrees with `firstChannelWithRole` on its first element. (b) a TF index round-trips, is refused outside `[0, kMaxChannels)`, defaults to 0, and every successful `setTransferFunction` bumps `configEpoch()`.

- [ ] **Accept:** OFF **405/405**; `platform_types_has_no_framework_deps` **5** (no new file). **Mutation:** drop the received-channel clamp from `channelsWithRole` → (a) fails.
- [ ] **Commit:** `git add platform/types/include/rta/platform/ChannelConfig.h platform/tests/test_channel_config.cpp` → `feat(platform): a per-channel transfer-function index and an all-of-role lookup`

## Task B2 — N transfer functions behind one drain (record §6, §11 T11)

**Files.** Create `app/src/measure/RoutingPlan.h` (≤ 130, JUCE-free, header-only), `app/tests/test_routing_plan.cpp` (≤ 200), `app/tests_juce/test_routing_live.cpp` (≤ 150); modify `app/src/measure/AnalysisThread.h` (111 → ≈ 130) and `.cpp` (203 → ≈ 260), `app/tests/CMakeLists.txt` (source list **and** guard list), `app/tests_juce/CMakeLists.txt`.

The decision is a **pure function**, so it is testable with `RTA_BUILD_APP=OFF` — `PairedDrain.h` is the precedent, extracted from this same class for this same reason:

```cpp
struct TransferRoute { int tfIndex, referenceChannel, measurementChannel; };
struct RoutingPlan {
    std::vector<TransferRoute> routes;    // ascending measurement channel
    std::vector<int> distinctReferences;  // ascending; each read ONCE per hop
    std::vector<int> unroutedMeasurements;
};
[[nodiscard]] RoutingPlan planRouting(const rta::platform::ChannelConfig&, int channelCount);
```

`AnalysisThread` then holds `std::vector<std::unique_ptr<Analyser>> analysers_` (non-movable engines — `MtwEngine.h:53-60`'s reason), **built once** in the constructor at `kMaxTransferFunctions` and never resized, plus one scratch per distinct reference. `drainPaired` peeks every distinct reference and every measurement and discards **nothing** until all peeks succeed, then pushes each pair.

**RED first — 3 OFF + 1 ON.** (a) two `Measurement` channels and one `Reference` → two routes over one distinct reference; (b) two measurements naming different references → two distinct references, two routes; (c) a measurement whose TF names no reference lands in `unroutedMeasurements`, never silently paired with channel 0. ON: two `Analyser`s driven through a real `AnalysisThread` over `SyntheticInput` report **equal** `framesAnalysed` when they share a reference and independent counters when they do not (R6).

- [ ] **Accept:** OFF **408/408**; ON **461/461**. **Mutation:** discard the first reference ring before peeking the second → the ON case's counters must diverge; that is the paired-drain defect one level up.
- [ ] **Commit:** `git add app/src/measure/RoutingPlan.h app/src/measure/AnalysisThread.h app/src/measure/AnalysisThread.cpp app/tests/test_routing_plan.cpp app/tests_juce/test_routing_live.cpp app/tests/CMakeLists.txt app/tests_juce/CMakeLists.txt` → `feat(app): N transfer functions, one drain, each reference read once`

## Task B3 — the average group, and publishing two things instead of N (record §6)

**Files.** Create `app/src/measure/AverageGroup.{h,cpp}` (≤ 120 / ≤ 200, JUCE-free), `app/tests/test_average_group.cpp` (≤ 280); modify `app/src/measure/Snapshot.h` (165 → ≈ 230), `AnalyserPublish.{h,cpp}`, `AnalysisThread.cpp`, `app/tests/CMakeLists.txt`.

`Snapshot` gains `std::optional<AverageBlock> average` (the stitched combine with per-point `phaseAgreement`, `weightedCoherence`, `contributors`, `absence`) and `std::vector<PositionSummary> positions` — a fixed-size POD per member (`tfIndex, name, levelDb, weightedCoherence, effectiveAverages, gatePassed, overloaded`), no arrays. `AverageGroup` owns the member TF indices, the trims `u_i` and the `SpatialMode`, and **refuses a member whose route names a different reference channel** (§6: two references are two groups).

**RED first — 4 `TEST_CASE`s.** (a) a group over two `Analyser`s publishes an `average` equal to `rta::dsp::spatialAverage` called directly on the same snapshots; (b) a member on a different reference is refused with a named reason and the group is unchanged; (c) publish carries the average plus exactly **one** soloed position's full blocks and a summary for every member, and with solo cleared, the average alone; (d) **T12, counting allocator** — a global `operator new`/`delete` counter around one publish asserts `bytes(8) - bytes(4) <= 4 * sizeof(PositionSummary) + 4096`. That is §6's O(1)-in-N claim stated exactly (R5); a per-position full publish is ≈ 2.21 MB (record §6), so four cannot hide in 4 KB. Report the 2× headline beside it; do not assert it.

- [ ] **Correct `AnalysisThread.cpp:196-198`**, which budgets "~8 KB, mostly spectrumDb": replace the figure with what (d) measures at `N = 1`, and say the cost is now O(1) in N and why.
- [ ] **Accept:** OFF **412/412**; ON **465/465**; `measure_has_no_framework_deps` **36**. **Mutation:** publish every position's full blocks → (d) fails by ≈ 4 × 2.21 MB.
- [ ] **Commit:** `git add app/src/measure/AverageGroup.h app/src/measure/AverageGroup.cpp app/src/measure/Snapshot.h app/src/measure/AnalyserPublish.h app/src/measure/AnalyserPublish.cpp app/src/measure/AnalysisThread.cpp app/tests/test_average_group.cpp app/tests/CMakeLists.txt` → `feat(app): the average is the published trace, plus one solo`

## Task B4 — level alignment (record §7)

**Files.** Create `app/src/measure/LevelAlign.h` (≤ 110, JUCE-free, pure), `app/tests/test_level_align.cpp` (≤ 160); modify `app/tests/CMakeLists.txt`.

`alignTrims(perPosition, double lowHz, double highHz, std::span<double> trimsOut)` sets each `u_i` so that position's `weightedCoherence`-weighted mean level over `[lowHz, highHz]` — **the span the operator is currently displaying**, never Smaart's baked-in 225 Hz–8.8 kHz — equals the group's, and returns the dB moves it made.

**RED first — 2 `TEST_CASE`s.** (a) three positions at `a`, `a+3`, `a−3` dB with equal coherence align to trims `{0, −3, +3}` dB exactly and re-running is a no-op; **plus** a fourth position at the right level whose trust is 0.1 over half the span, with its closed-form weighted answer — that is the case an unweighted mean must fail. (b) a position with zero trust across the whole span gets **no** trim and is reported, rather than a `0/0`; a span containing no bin returns no moves.

- [ ] **Accept:** OFF **414/414**. **Mutation:** drop the coherence weighting → (a)'s fourth position must fail.
- [ ] **Commit:** `git add app/src/measure/LevelAlign.h app/tests/test_level_align.cpp app/tests/CMakeLists.txt` → `feat(app): align levels over the span the operator can see`

## Task B5 — the capture sequencer (record §8)

**Files.** Create `app/src/measure/CaptureSequencer.{h,cpp}` (≤ 110 / ≤ 190, JUCE-free), `app/tests/test_capture_sequencer.cpp` (≤ 260); modify `app/tests/CMakeLists.txt`.

States `Idle → Armed → Waiting → Capturing → Stored → Armed`, plus `Refused`, with `enum class RefusalReason { None, Overload, GateNotCleared }` and **no third reason**: the coherence-trusted-fraction rule appears in no source and is a question for a real system, filed in `docs/HUMAN-QA-QUEUE.md` (§8). `hasOverload` (A3) runs over each drained hop and **latches for the whole capture window**; the gate check reads `coherence.has_value()` on every member at capture time. **No generator output** — §8: the callback clears every output (`AudioIo.cpp:134-137`) and changing that is a different lane with its own research pass, so the sequencer exposes `std::function<void(int stepIndex)> onStep` and nothing else, and a step prompts the operator to solo by hand.

**RED first — 4 `TEST_CASE`s.** (a) a three-member sequence walks arm→wait→capture→store→advance and ends `Idle` with three captures named after their members; (b) an overload during a window refuses **that step only**, with `RefusalReason::Overload`, and the sequence continues; (c) a member under the gate refuses with `GateNotCleared` and stores nothing degraded; (d) `onStep` fires once per step and the sequencer writes to **no** output buffer — asserted by an output scratch that must still be all zeros afterwards.

- [ ] **Accept:** OFF **418/418**; `measure_has_no_framework_deps` **38**. **Mutation:** clear the overload latch each hop instead of at the window start → (b) fails; a clip mid-window would otherwise be forgotten by the time the window closes.
- [ ] **Commit:** `git add app/src/measure/CaptureSequencer.h app/src/measure/CaptureSequencer.cpp app/tests/test_capture_sequencer.cpp app/tests/CMakeLists.txt` → `feat(app): a sequencer that refuses on two criteria and drives no output`

## Task B6 — presets, schema 3 (record §9)

**Files.** Modify `app/src/trace/SessionCodec.h` (66 → ≈ 110), `SessionCodec.cpp` (≈ 100 → ≈ 150), `SessionDecode.cpp` (≈ 145 → ≈ 230), `app/tests/test_session_codec.cpp` (340 → ≤ 400 — **if it would pass 400, move the new cases to `app/tests/test_session_presets.cpp`** and register it).

`kSchemaVersion = 3`. New sections `[tf]` (`name`, `measurementChannel`, `referenceChannel`, `delaySamples`, `trimDb`, `polarity`, `memberOfAverage`, `averagingMode` = `global`|`pinned`, `fifoDepth`) and `[average]` (`mode` = `db`|`power`, repeated `member=`). `CaptureMeta` is **not** touched — it is immutable history; a preset is editable configuration. Routing stores `deviceName` **and** `inputChannelCount`; on load, no current device matching **both** → the routing loads `bound = false` and visibly so, never onto whatever now answers to the name (OSM's `deviceIdByName` trap, research Part B).

**RED first — 3 `TEST_CASE`s (§11 T13).** (a) a schema-3 document with two `[tf]`s and one `[average]` round-trips every field, including a `trimDb` needing full `to_chars` precision and a name containing `=` and a newline; (b) a schema-2 document decodes `Ok` with zero transfer functions and zero average members — old sessions keep opening; (c) same `deviceName`, different `inputChannelCount` → `Ok` with `bound == false`, and matching both → `bound == true`. A schema-4 document still returns `NewerSchema`.

- [ ] **Accept:** OFF **421/421**; `measure_has_no_framework_deps` **38** (or **39** if the test split added a source — read it from the output). **Mutation:** compare only `deviceName` on load → (c) fails.
- [ ] **Commit:** `git add app/src/trace/SessionCodec.h app/src/trace/SessionCodec.cpp app/src/trace/SessionDecode.cpp app/tests/test_session_codec.cpp app/tests/CMakeLists.txt` → `feat(app): schema 3 -- transfer functions, the average, and unbound routing`

## Task B7 — seeing it (record §6, §7; CLAUDE.md's readout rules)

**Files.** Create `ui/az_ui/theme/GridPanel.h` (≤ 90) + `.cpp` (≤ 140), `ui/tests/test_grid_panel.cpp` (≤ 120), `app/src/view/RoutingMatrix.{h,cpp}` (≤ 90 / ≤ 260), `app/tests_juce/test_routing_matrix.cpp` (≤ 180); modify `app/src/view/Readouts.h`, `app/tests/test_readouts.cpp`, `TransferView.cpp`, `app/src/measure/SyntheticSnapshot.{h,cpp}`, `tools/snapshot.cpp`, `app/CMakeLists.txt`, `ui/tests/CMakeLists.txt`, `app/tests_juce/CMakeLists.txt`.

**The `ui/` line.** `GridPanel` is a generic row × column cell grid with a header row, hit-testing and a `std::function<void(int row, int col)>`. It contains **no** measurement vocabulary — no `reference`, `measurement`, `coherence` or `trim`. Those words live in `app/src/view/RoutingMatrix`, which composes it. This is the line that decides whether `az_ui` is reusable (project `CLAUDE.md`, Module boundaries).

**RED first — 2 OFF** in `test_readouts.cpp`, per `CLAUDE.md`'s readout rules: `formatHz(1000.4) == "1000 Hz"`, `formatTrim(-3.0) == "-3.0 dB"`, `formatAgreement(0.7071) == "0.71"`, `formatContributors(3, 4) == "3 of 4"` — **plus 1 in `ui/tests`** (grid hit-testing and header layout are pure geometry) and **1 in `app/tests_juce`** (clicking a matrix cell cycles the role and calls `setTransferFunction`).

**The fixture.** Add `makeSyntheticAverage(const rta::dsp::MtwConfig&, int positions)` beside `makeSyntheticMtw`, and have `tools/snapshot.cpp` set `snapshot->average` and `snapshot->positions` so the offscreen specimen shows the average trace, the contributor count and `R`. Render and read it — never screen-capture (`CLAUDE.md`, Seeing the GUI); invoke through `cmd //c` from Git Bash, since direct invocation returns 127:

```
cmake --build build-l6b-on --config Release --target rtatool_snapshot --parallel
build-l6b-on/app/rtatool_snapshot_artefacts/Release/rtatool_snapshot.exe shots 1100 760
```

- [ ] **Accept:** OFF **423/423**; ON **479/479**; `shots/specimen.png` read and described in the report. **Mutation:** put `coherence` in an `az_ui` identifier and confirm the module-boundary grep catches it (add that grep to the report if none exists); revert.
- [ ] **Commit:** `git add ui/az_ui/theme/GridPanel.h ui/az_ui/theme/GridPanel.cpp ui/tests/test_grid_panel.cpp ui/tests/CMakeLists.txt app/src/view/RoutingMatrix.h app/src/view/RoutingMatrix.cpp app/src/view/Readouts.h app/src/view/TransferView.cpp app/src/measure/SyntheticSnapshot.h app/src/measure/SyntheticSnapshot.cpp tools/snapshot.cpp app/tests/test_readouts.cpp app/tests_juce/test_routing_matrix.cpp app/CMakeLists.txt app/tests_juce/CMakeLists.txt` → `feat(app): the routing matrix, the trims, and what the average admits`

---

## Numbers the builder must measure, not copy

Every figure below that came from a *count* is a prediction. Measure it before task A1 and again where each Accept line names it; if a measurement disagrees, the plan is wrong and the orchestrator hears about it — do not bend the code to hit it.

| quantity | plan says | how |
|---|---|---|
| ctest OFF / ON baseline | **390 / 442** | `--clean-first` build of `959a197`, both dirs |
| ctest OFF after L6b-a / L6b-b | 403 / 423 | derived from the `TEST_CASE` lists above; re-derive if a list changes |
| ctest ON after L6b-a / L6b-b | 455 / 479 | as above, `+52` for today's ON-only surplus |
| `core_has_no_framework_deps` | 96 → **106** | the guard prints `(N files scanned)` |
| `coherence_gate_is_not_bypassed` | 62 → **68** | as above |
| `filter_design_has_no_polynomial_form` | 113 → **124** | as above |
| `measure_has_no_framework_deps` | 30 → **40** | as above; the list is explicit, so a new app file is unguarded until added by hand |
| `platform_types_has_no_framework_deps` | **5**, unchanged | as above |
| publish bytes at N = 1, 4, 8 | task B3(d) | the counting allocator; 2.21 MB per position is the record's figure, not a measurement |

## Things this plan does not decide

Record §12's list stands unchanged — the remote API (§10); the generator output path and lock-free solo/mute (§8; B5 ships the hook and asserts nothing is written to an output buffer, that is all); a numeric coherence-trusted-fraction refusal (`HUMAN-QA-QUEUE`); SysTune-style excursion down-weighting; a complex spatial mode and the sum-of-elements display (G11/L7); storing spatial averages in the library; the AFMG patent family's bearing on impulse-response averaging. Two more surfaced while planning:

- **Whether `phaseAgreement` should gate the view.** This plan reports `R` and marks phase absent only at the arithmetic floor (R3); a *display* threshold is another number read off a grid and belongs to whoever has a real system in front of them.
- **`kMaxTransferFunctions`.** B2 builds the `Analyser` vector once at a compile-time cap; 8 positions cost 424 MB resident (record §6). That is a memory policy, not a DSP decision — set it, state it in the header, and let the owner move it.

## For the station-4 builder, first read

1. The record is binding; this plan implements it and flags R1–R6, which the orchestrator should amend first.
2. **L6b-a** is `core/` with `RTA_BUILD_APP=OFF`; **L6b-b** is everything else. Do not interleave them.
3. Per task: the failing test first, then the header, then the body. One commit per task, with the `git add` paths given.
4. Never write a field named `coherence` outside `TransferEstimator.cpp`, and never bind a reference through one.
5. A5's deliberate RED is not optional — a guard nobody has seen fail is a guard nobody knows is watching.
6. `DualFftEngine.{h,cpp}` and `test_dualfft.cpp` are frozen; prove it with the diff.
7. Three files are split (B0) **before** anything is added to them.
8. Build in `build-l6b` / `build-l6b-on`, Visual Studio, never Ninja; never touch `build-l6b-base-*`.
9. Measure the baselines before A1; every count here is a prediction you are expected to falsify if it is wrong.
10. Paste the command and its output for every "done" — a green build proves it compiles, not that the numbers are right.
