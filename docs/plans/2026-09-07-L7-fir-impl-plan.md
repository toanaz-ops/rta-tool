# L7-FIR (G10) — FIR coefficient export: frequency-sampling MVP, two phase modes, text + WAV

> **For agentic workers:** REQUIRED SUB-SKILLS `superpowers:test-driven-development` and `superpowers:executing-plans`. Every step is a checkbox; the failing test is written and *seen to fail* (the missing `#include`, then the wrong number) before the code. Station 5 (adversarial verify, a reviewer with **no Write tools**) is not optional.

*2026-09-07, lane L7, sub-lane L7-FIR, station 3. Written from the decision record `docs/dsp/2026-09-06-l7-fir-export.md` and the built Wave 0 (`core/include/rta/dsp/MinimumPhase.h`, `core/include/rta/eq/FilterSpec.h`), after reading the real `core/`, `app/` and `tools/` files, not their descriptions. Worktree `.claude\worktrees\continue-pending-work-f8aa84`, HEAD `6aa7034`. Every correctness acceptance below is a closed-form identity; ONE golden vector (`fir.txt`) is added as a scipy cross-author check per record §7.7, and the cepstral oversampling factor is **measured** by `gen_fir.py` before any C++ constant is fixed.*

## 0. What L7-FIR v1 is, and what it reuses

G10 turns a correction (breakpoints today, an auto-EQ curve later) into a coefficient file a convolver loads without guessing sample rate, tap count, phase type or gain. v1 designs by **frequency sampling only** (record §2); weighted least-squares is decided in principle and **not built** (deferred until `gen_fir.py` measures the window's LF smear vs `firls` > 1 dB — record §2, §11). Parks–McClellan is parked for G18 (record §3).

The design math is `core/` (span in, coefficients + metadata out); writing the two files is `app/`. v1 reuses Wave 0 **`rta::dsp::minimumPhaseFromMagnitude`** verbatim for the minimum-phase mode (record §4; kernel: log NOT halved, fold `[1,2,…,2,1]`, −120 dB floor). It does **not** consume `rta::eq::FilterSpec`: FilterSpec is the biquad cookbook vocabulary; a FIR target is a magnitude curve, so FIR takes breakpoints (primary) or a per-bin magnitude grid (the §6 overload). `RealFft` + `Window` build the frequency-sampling MVP.

## Global constraints

- SPDX header `// SPDX-License-Identifier: AGPL-3.0-or-later` on every new file. `core/` never includes JUCE/Qt/a device API (guard `core_has_no_framework_deps`); the FIR design math (FFTs on spans) stays pure `core/`, the file writers are `app/`. Comments explain *why a formula is that formula* — a reader checks the DSP against a textbook from the comment.
- **Hard cap 400 lines, aim 300**, headers too. Every file is budgeted below; all sit well under 300.
- **`RealFft`, `Fft`, `Window`, `MinimumPhase.{h,cpp}`, `TransferEstimator.h` MUST NOT be touched.** L7-FIR *adds* a `FirDesign` that calls them. Prove it at the end: `git diff --stat main -- core/include/rta/dsp/MinimumPhase.h core/include/rta/dsp/RealFft.h core/include/rta/dsp/Window.h` is empty.
- **Never assert a value the implementation produced.** Closed form (record §7 items 1–6), or the `fir.txt` golden with scipy as the second author (item 7, a labelled cross-check). No value is asserted because "the code printed it".
- **Tolerance shapes.**
  - **Shape A — FFT-derived** (freq-sampling taps, min-phase taps, round-trip, magnitude identity, floor): the two-term float32 form of `memory/float32-fft-precision.md`, `tol_k = 1e-6·|X_k| + c_M·peak`, `c_M = 2e-7` for `M ≤ 2^20` as the **starting point**. The builder **prints the measured residual beside the tolerance** and tightens `c_M` if a fixture reads an order of magnitude under it (`memory/a-fixture-can-be-too-well-behaved-to-fail.md`).
  - **Trivial-target deltas** (record §7.4): absolute `1e-5` on the centre/zeroth tap and `≤ 1e-5` elsewhere — an inverse-FFT of a flat spectrum, not a bit-exact identity.
  - **Grid interpolation** (double, no FFT): `1e-12`.
  - `truncationLossDb`: a **regression lock** against the golden's field, labelled as such.
- Build dirs **`build-l7fir`** (OFF — core only) / **`build-l7fir-on`** (ON — app exporters + `tests_juce`), Visual Studio generator, never Ninja (`memory/a-misconfigured-build-goes-99-percent-of-the-way.md`). Ignore IDE clang diagnostics; MSVC `/W4` is the truth and must be **0 warnings**.

```
cmake -S . -B build-l7fir -G "Visual Studio 18 2026" -A x64
cmake --build build-l7fir --config Release --parallel
ctest --test-dir build-l7fir -C Release --output-on-failure
cmake -S . -B build-l7fir-on -G "Visual Studio 18 2026" -A x64 -DRTA_BUILD_APP=ON -DRTA_JUCE_PATH=D:/DEV CAVE EP3/PROJECT005-AZ-handsfree/external/JUCE
cmake --build build-l7fir-on --config Release --parallel
ctest --test-dir build-l7fir-on -C Release --output-on-failure
```

## Decisions to take to the orchestrator (do not silently re-decide in code)

- **D1 — namespace `rta::dsp::FirDesign`, header/impl in `core/include/rta/dsp/` + `core/src/dsp/`** (record §6). It sits beside `RealFft`/`Window`/`MinimumPhase` (all `rta::dsp`) and consumes them; it is not `rta::eq` (that area is the biquad cookbook). The API mirrors the `ButterworthDesign::Result` precedent — a static free function returning a `struct Result` of taps + metadata as DATA.
- **D2 — FIR does NOT take a `FilterSpec`.** Primary input is a breakpoint target `(frequencyHz[], gainDb[])`, interpolated **linearly in `log10(f)` and linearly in dB** (record §6). One overload accepts a per-bin magnitude half-grid directly (record §6 amendment, §11: the shape the auto-EQ may hand over). This is the whole of "how it relates to FilterSpec": it does not — Wave 0's shared piece here is `minimumPhaseFromMagnitude`, not `FilterSpec`.
- **D3 — the exporters live in a new `app/src/export/`, not `app/src/trace/`.** The record §6 says "beside `SessionCodec*`", meaning *beside persistence*; export is a sibling concern to session persistence, and a new `export/` folder keeps `trace/` about sessions. Minor deviation from the record's wording — flag it; if the orchestrator prefers `app/src/trace/`, only the paths move.
- **D4 — ONE golden (`core/tests/golden/fir.txt`) IS added for v1.** The record §7.7 specifies it (frequency-sampling taps + minimum-phase taps + the oversampling-sweep table, from scipy). All *correctness* lives in the closed-form identities F1–F3; the golden is a second-author cross-check (Shape A) and the carrier of the measured oversampling factor. `gen_fir.py` is non-optional regardless, because the min-phase `nFft` factor (record §4) is measured, not chosen. If the orchestrator wants zero committed golden, F6 still runs `gen_fir.py` to measure the factor and prints the sweep — but then the scipy cross-check is lost. Recommendation: keep the golden.
- **D5 — the −120 dB floor is applied to a MEASURED `|H|` here** (the target curve), reusing `kMinPhaseFloorDb`. This is Wave 0 **OQ-A** unresolved (`MinimumPhase.h:11-14`; record §4): −120 was chosen for a *designed* correction filter. Ship −120; **flag it to the owner**, do not re-derive a second floor (the drift `memory/` exists to prevent).
- **D6 — the cepstral oversampling factor is measured first (F6), then fixed as a named C++ constant** cited to the record §4 amendment. F2 cannot be finalised until F6's sweep names the factor.

---

## Task F1 — `FirDesign` frequency sampling, linear phase (core; record §2, §7.1–7.2, §7.4)

**Files.** Create `core/include/rta/dsp/FirDesign.h` (≤ 90), `core/src/dsp/FirDesign.cpp` (≤ 200), `core/tests/test_fir_design.cpp` (≤ 260); modify `core/CMakeLists.txt`, `core/tests/CMakeLists.txt`.

```cpp
// core/include/rta/dsp/FirDesign.h  --  namespace rta::dsp
#include "rta/dsp/Window.h"
#include <complex> #include <cstddef> #include <optional> #include <span> #include <vector>

enum class FirPhase  { Linear, Minimum };
enum class FirMethod { FrequencySampling };   // LeastSquares reserved (record §2), NOT built v1

struct FirTarget {                            // breakpoints, ascending fHz
    std::vector<double> frequencyHz;          // interpolated linearly in log10(f) and in dB
    std::vector<double> gainDb;
};

struct FirResult {
    std::vector<float> taps;                  // as designed; normalisation is the writer's job
    double      sampleRate = 0.0;
    FirPhase    phase  = FirPhase::Linear;
    FirMethod   method = FirMethod::FrequencySampling;
    WindowType  window = WindowType::Hann;
    std::size_t groupDelaySamples = 0;        // (N-1)/2 for linear; 0 reported for minimum
    double      peakGainDb = 0.0;             // max 20*log10|H| over the design grid
    double      coefficientPeak = 0.0;        // max |taps[n]|
    std::optional<double> truncationLossDb;   // minimum phase only
    std::size_t designFftSize = 0;            // M (freq sampling); nFft (minimum phase)
};

// throws std::invalid_argument: sampleRate<=0; taps<8 or > M/2 (record §10); target
// frequency axis empty or non-monotonic; per-bin grid size not a power-of-two + 1.
[[nodiscard]] FirResult designFir(const FirTarget&, double sampleRate, std::size_t taps,
                                  FirPhase, WindowType = WindowType::Hann);
[[nodiscard]] FirResult designFir(std::span<const float> magnitudeHalfGrid,  // D2 overload
                                  double sampleRate, std::size_t taps,
                                  FirPhase, WindowType = WindowType::Hann);
```

Body: pick `M` = smallest power of two `≥ 8·taps`; sample the target magnitude on `M/2+1` bins (`bin k` at `k·fs/M`); for linear phase set zero phase, `RealFft::inverse` → length-`M` impulse; circular-shift so the zero-phase response centres at `(N-1)/2`; window with a **periodic** `Window(window, N)`; truncate to `N`. Build the symmetric half explicitly (`taps[0..(N-1)/2]`, mirror) so `taps[n]==taps[N-1-n]` is bitwise (record §4).

**RED first.** `test_fir_design.cpp` opens with `#include "rta/dsp/FirDesign.h"`; build fails at the include — paste it. **First failing assertion** once the header exists:

| # | case | closed-form acceptance (source) |
|---|---|---|
| **T1 (first)** | flat 0 dB, linear | target `≡ 0 dB` over `[20,20000]`, `N=1023`, `fs=48000` → `taps[(N-1)/2]==1.0` within `1e-5`, all others `≤1e-5`; `peakGainDb≈0`, `coefficientPeak≈1` (record §7.4) |
| T2 | flat +6.0206 dB, linear | centre tap `==2.0±1e-5`; `peakGainDb==6.0206±1e-4`, `coefficientPeak==2.0±1e-5` |
| T3 | round trip (sampling step) | `RealFft::forward(RealFft::inverse(X_sampled))==X_sampled` bin-for-bin, Shape A — proves the sample/invert step, not the design (record §7.1) |
| T4 | symmetry then phase | `taps[n]==taps[N-1-n]` **bitwise** (construction); then `Im(H(e^{jω_k})·e^{+jω_k(N-1)/2}) == 0` within Shape A at every bin, for one **odd** (`N=1023`) and one **even** (`N=1024`) tap count (record §7.2). Even `N`: `groupDelaySamples` reported as `N/2`, half-sample delay documented |

- [ ] **Accept:** OFF ctest **`base+4`/`base+4`** (base = measured HEAD count; §Numbers). Zero `/W4`.
- [ ] **Mutation:** drop the circular shift → T4's phase-linearity fails at every bin; revert.
- [ ] **Commit:** `feat(core): FIR frequency-sampling design, linear phase, symmetry bitwise not rounded`

## Task F2 — minimum-phase mode, wrapping the Wave 0 kernel (core; record §4, §7.3)

Depends on F1 (needs `h_lin`) and Wave 0 `minimumPhaseFromMagnitude`. **Blocked on F6's measured factor (D6).**

**Files.** Extend `FirDesign.{h,cpp}` and `test_fir_design.cpp` (min-phase branch of `designFir`). No new files.

Body: design `h_lin` by F1; compute `A = |FFT(h_lin, nFft)|` on the full `nFft`-grid (`nFft` = the **measured** factor × `N`, rounded up to a power of two — the constant from F6, cited to the record §4 amendment); call `minimumPhaseFromMagnitude(A)` → `H_min`; `Re(IFFT(H_min))`; truncate to `N`; report `truncationLossDb = 10·log10( Σ_{n≥N} h_min[n]² / Σ_all h_min[n]² )·(−1)`… (state the exact sign/definition in the header and pin it in the golden — a lossy fraction, reported not hidden). `groupDelaySamples=0`.

| # | case | closed-form acceptance (source) |
|---|---|---|
| **T5 (first)** | flat 0 dB, minimum | `taps[0]==1.0±1e-5`, rest `≤1e-5` — a flat magnitude's min phase is `δ[0]` (record §7.4) |
| T6 | magnitude identity | before truncation, `\|FFT(h_min,nFft)\| == \|FFT(h_lin,nFft)\|` bin-for-bin within Shape A — the check that does not depend on the derivation (record §4, §7.3) |
| T7 | known min-phase impulse | target `= \|1 − 0.5 e^{−jω}\|` → `Re(IFFT) ≈ {1, −0.5, 0, …}` within Shape A (ties the wrap to Wave 0's own T3/T4) |
| T8 | floor (**D5, flag OQ**) | target with a `−200 dB` notch → no NaN/Inf; `20·log10\|H\|` at the notch bin `== −120` within Shape A (record §7.6) |
| T9 | truncation loss (**labelled lock**) | `truncationLossDb` for the `N=1023` boost-and-cut fixture equals the value F6's golden records, and only that value |

- [ ] **Accept:** OFF **`base+9`** (T5–T9 add cases). Zero `/W4`.
- [ ] **Mutation:** feed the kernel `A²` instead of `A` (i.e. forget `A=|FFT|` and pass power) → T6 fails; revert.
- [ ] **Commit:** `feat(core): FIR minimum-phase mode via the shared cepstral kernel, magnitude identity proven`

## Task F3 — grid interpolation, metadata, refusals, the per-bin overload (core; record §6, §7.5, §10)

**Files.** Same three; the interpolation helper + the second `designFir` overload + refusals.

| # | case | closed-form acceptance (source) |
|---|---|---|
| **T10 (first)** | log-f / dB interpolation | breakpoints `(100,0),(1000,+6)` → the sampled grid reads `+3.0 dB` at `316.227… Hz` (`√10·100`) within `1e-12`, in double (record §7.5) |
| T11 | metadata | `groupDelaySamples==(N-1)/2` (linear) / `0` (minimum); `designFftSize` a power of two `≥8N` (freq sampling); `sampleRate/phase/method/window` echo the inputs |
| T12 | refusals | `sampleRate≤0`; `taps>M/2`; non-monotonic `frequencyHz`; empty target; per-bin grid whose size is not `2^k+1` → `REQUIRE_THROWS_AS(…, std::invalid_argument)` (record §10) |
| T13 | overload agrees | the per-bin `designFir(magnitudeHalfGrid,…)` given the *same* sampled magnitude the breakpoint path produces returns taps equal within Shape A — the two entry points are one design |

- [ ] **Accept:** OFF **`base+13`**. Zero `/W4`. All new files `< 400` lines (`wc -l`).
- [ ] **Mutation:** interpolate linearly in `f` (not `log10 f`) → T10 reads ≠ +3 dB; revert.
- [ ] **Commit:** `feat(core): FIR target interpolation in log-f, the per-bin overload, and the four refusals`

## Task F4 — the text writer (app, JUCE-free; record §5)

**Files.** Create `app/src/export/FirExport.h` (≤ 70), `app/src/export/FirTextWriter.cpp` (≤ 140), `app/tests/test_fir_text.cpp` (≤ 200); modify `app/tests/CMakeLists.txt` (add sources to `rtatool_analysis_tests` **and** the `measure_has_no_framework_deps` GLOBS list so the JUCE-free property is guarded).

The writer takes the core `FirResult` + a `Normalization {AsDesigned, Peak0dBFS}` and applies the trim (record §6: normalisation is the writer's job). Fixed `#` `key=value` header in `SessionCodec`'s line convention, ending `# --- coefficients follow ---`, then one coefficient per line.

| # | assertion (record §5) |
|---|---|
| **T1 (first)** | every header key present: `sample_rate_hz`, `taps`, `phase`, `method`, `window`, `group_delay_samples` (linear only), `normalization`, `applied_trim_db`, `peak_gain_db`, `coefficient_peak`, `truncation_loss_db` (minimum only), `generator`, `generated_utc`; parse each back to its `FirResult` value |
| T2 | **sample rate in the file**: `sample_rate_hz=<fs>` present and equal to `Result.sampleRate` |
| T3 | coefficient count on disk `== taps`; each parses as a float and round-trips within `1e-6` (text is `%.9g`) |
| T4 | **bare refused**: a `writeFirText(..., bare=true)` request throws / returns an error — the header is not optional (record §5, §10) |
| T5 | `Peak0dBFS`: `max\|tap\|` after scaling `== 1.0±1e-6`; `applied_trim_db == −20·log10(coefficient_peak)` within `1e-6`; `AsDesigned` writes taps unchanged and `applied_trim_db=0` |

- [ ] **Accept:** ON ctest, `rtatool_analysis_tests` **`app_base+5`**; `measure_has_no_framework_deps` **green with the two new files scanned**. Zero `/W4`.
- [ ] **Commit:** `feat(app): the FIR text export, self-describing header, bare mode refused`

## Task F5 — the WAV writer (app, JUCE `WavAudioFormat`; record §5, §6)

**Files.** Create `app/src/export/FirWavWriter.cpp` (≤ 120), `app/tests_juce/test_fir_wav.cpp` (≤ 160); add a `rtatool_export_tests` target to `app/tests_juce/CMakeLists.txt` linking `rta::core` + `juce::juce_audio_formats` (already transitively present via `juce_audio_utils` — record §6, verified `juce_audio_utils.h:54`). Writes mono, **32-bit IEEE float**, true `fs` in the header.

| # | assertion (record §5) |
|---|---|
| **T1 (first)** | round-trip: write, then read back with `juce::WavAudioFormat::createReaderFor` → `sampleRate == fs`, `numChannels==1`, `lengthInSamples==taps`, samples `== taps` within `1e-7` |
| T2 | **32-bit float, not int**: the format chunk reports IEEE float, 32 bits (`reader->usesFloatingPointData == true`); an integer-format request is **refused** (record §5) |
| T3 | filename contract `<name>_<fs>Hz_<N>taps_<lin\|min>.wav` — the helper that builds it is unit-tested for all three fields |

- [ ] **Accept:** ON ctest, `rtatool_export_tests` all green. Zero `/W4`. No window / desktop peer / message loop created (headless, the `tests_juce` convention).
- [ ] **Commit:** `feat(app): the 32-bit-float WAV export, sample rate in the header and the filename`

## Task F6 — `gen_fir.py`, the oversampling sweep, and the `fir.txt` golden (tools + core; record §4, §7.7, §8)

**Files.** Create `tools/gen_fir.py` (≤ 200), `core/tests/golden/fir.txt` (generated), `core/tests/test_fir_golden.cpp` (≤ 160); modify `core/tests/CMakeLists.txt` (add `test_fir_golden.cpp`). **Runs in the MAIN-checkout venv, not this worktree** (`memory/build-toolchain`); the builder confirms that venv's scipy has `minimum_phase(half=…, n_fft=…)` before writing (added scipy ≥ 1.7 — **re-check, do not assume**).

`gen_fir.py` (copy `gen_mtw.py:165-190`'s argparse exactly — `--out`, `--check`, `--help` exits before any write; `memory/a-gen-script-runs-the-moment-you-invoke-it`):
1. **Oversampling sweep (D6, record §4).** For each fixture `N`, sweep `nFft/N ∈ {4,8,16,32,64,128,256}`; record the magnitude-identity residual of `minimum_phase(h, method='homomorphic', half=False, n_fft=…)` in double. The **smallest factor whose residual is below the Shape-A float32 floor** is the one the C++ constant uses. Write the table + chosen factor into `fir.txt` **and** amend `docs/dsp/2026-09-06-l7-fir-export.md` §4 with it (a decision without its number is a decision the next session reverses).
2. **Golden taps.** Boost-and-cut target at `N=1023` and `N=4095`: frequency-sampling taps (`numpy.fft.irfft` + `scipy.signal.get_window(win, N, fftbins=True)` — **`fftbins=True` is periodic, matching `Window`; `fftbins=False` is the trap**, record §7); minimum-phase taps (`minimum_phase(h, 'homomorphic', half=False, n_fft=<factor>)`); and `truncation_loss_db` per fixture.
3. **Pin every scale-variant convention in the file** (record §7, `docs/HANDOFF.md`): window form, `irfft`'s `1/N`, the circular-shift convention, the floor, `half=False`, `normalization=as_designed`. **FIR taps are absolute numbers, not ratios** — the Sweep golden's fade/peak asymmetries survived only because its field was scale-invariant; a tap is not, so a convention mismatch shows up directly.
4. **Polynomial guard (record §8):** name the array `taps` (never `b`); apply with `numpy.convolve` (never `signal.lfilter`); no `np.poly(`. `firls`/`minimum_phase`/`get_window`/`irfft` match nothing.

`test_fir_golden.cpp`: C++ `designFir` taps vs golden, **Shape A on both** paths (both are FFTs; the log/exp pair maps a relative magnitude error to the same relative error — record §7.7). Residual printed beside the tolerance.

- [ ] **Accept:** `python tools/gen_fir.py --check` byte-identical; OFF ctest **`base+14`** (adds `test_fir_golden`). Record §4 amended with the factor. Zero `/W4`.
- [ ] **Commit (staged paths, docs+golden+test):** `test(core): the FIR golden, scipy as the second author, and the measured oversampling factor`

## Task F7 — prove the guards still GUARD (record §7.8, §8)

No product files; a procedure whose output goes in the report. Every count is **read from the guard's own `(N files scanned)` line**, never from this plan.

- [ ] **RED, framework deps.** Add `#include <juce_core/juce_core.h>` atop `core/include/rta/dsp/FirDesign.h`; reconfigure; `ctest -R core_has_no_framework_deps` → **paste the failure naming FirDesign.h**; remove the line.
- [ ] **RED, polynomial form.** Add a line `taps = None; b = taps; # freqz(b` inside `tools/gen_fir.py` (the guard scans `tools/*.py`); `ctest -R filter_design_has_no_polynomial_form` → paste the failure naming `gen_fir.py`; remove it. Proves `gen_fir.py` is inside the guard's scan set (record §8).
- [ ] **GREEN, both guards, new files scanned.** Re-run each; paste the `(N files scanned)` line. Prediction to falsify: `filter_design_has_no_polynomial_form` count rises by exactly 1 (`gen_fir.py`; no new core `.py`) and `core_has_no_framework_deps` by the new core headers/cpp. A count *below* prediction means a file fell outside a glob — stop and report.
- [ ] **GREEN, `measure_has_no_framework_deps`** with `FirExport.h` + `FirTextWriter.cpp` added to its GLOBS (F4) — the text writer stays JUCE-free.
- [ ] **Frozen files.** `git diff --stat main -- core/include/rta/dsp/MinimumPhase.h core/include/rta/dsp/RealFft.h core/include/rta/dsp/Window.h` is empty.
- [ ] **Accept:** a `--clean-first` OFF build → **`base+14`/`base+14`**; ON build → app targets green; 0 `/W4`. **Commit:** `test(core): the L7-FIR guards, made red once before they were believed`

---

## Numbers the builder must measure, not copy

| quantity | plan says | how |
|---|---|---|
| ctest OFF baseline | **`base`** (measure) | `--clean-first` build of `6aa7034`, `build-l7fir` |
| OFF after F1 / F2 / F3 / F6 | `base+4` / `base+9` / `base+13` / `base+14` | derived from the `TEST_CASE` lists; re-derive if a list changes |
| ON `rtatool_analysis_tests` after F4 | `app_base+5` | measure `app_base` first |
| cepstral oversampling factor `nFft/N` | **measured by F6**, not chosen | sweep `{4…256}`, smallest under the Shape-A floor; amend record §4 |
| `filter_design_has_no_polynomial_form` scanned | +1 (`gen_fir.py`) | the guard's own count line |
| `core_has_no_framework_deps` / `measure_has_no_framework_deps` | rise, green | as above |
| MSVC `/W4` warnings | **0** | grep the build log |

## What L7-FIR v1 does NOT include

- **Weighted least-squares** (`firls`-shaped) design — decided in principle, gated on F6's LF-smear measurement > 1 dB (record §2, §11; the 1 dB is a proposal, not a floor). Its golden is Shape **B** (`cond(Q)`-scaled) and is deferred with it.
- **Parks–McClellan / Remez** — parked for the crossover surface, G18 (record §3).
- A mixed / excess-phase blend (rePhase's third mode); a default tap-count ceiling or convolver profile (record §11).
- A double-precision `Fft` in `core/` (record §11).

## Open questions / UNVERIFIED claims the builder must re-check (record §12)

- **D5 / OQ-A — the −120 dB floor on a *measured* `\|H\|`.** Reused from a designed correction filter; ship, **flag to the owner**. Do not add a second floor constant.
- **The oversampling factor is not a fact yet** — F6 measures it; F2 is blocked until it does. Do not hardcode scipy's `0.01` default (~200×) — record §4 rejects copying it.
- **scipy availability** in the main-checkout venv: confirm `minimum_phase(half=False, n_fft=…)` and `get_window(fftbins=True)` exist and behave as the record read them (record §12: verified from source, but the builder runs the actual venv). `pyrato`/`firls` not needed for v1.
- **Not load-bearing, do not cite as fact:** Remez "unreliable at high order" (UNVERIFIED, and G18's problem anyway); rePhase's method is IFFT+window (UNVERIFIED); whether rePhase/REW text importers tolerate `#` header lines (UNVERIFIED — "a tool that cannot skip lines takes the WAV", record §5). None affect v1's code.

## For the station-4 builder, first read

1. The record `docs/dsp/2026-09-06-l7-fir-export.md` is binding; this plan implements its **v1** (frequency sampling + two phase modes + two writers) and flags D1–D6, which the orchestrator amends first.
2. Order is fixed by dependency: **F1 → F6 (measure the factor) → F2 → F3 → F4 → F5 → F7**. F2 needs F6's number; F5 needs F4's `FirResult`→writer shape.
3. The failing test first, seen to fail (the missing `#include`, then the wrong number), then the header, then the body. Paste the command and its output for every "done" — a green build proves it compiles, not that the numbers are right (`CLAUDE.md`).
4. Wave 0 is reused, not re-created: `minimumPhaseFromMagnitude` is called, `MinimumPhase.{h,cpp}` is frozen (prove with the diff). FilterSpec is *not* an FIR input.
5. UTF-8 explicitly on `gen_fir.py`'s write (`encoding="utf-8"`, as `gen_mtw.py` does).
6. Build in `build-l7fir` / `build-l7fir-on`, Visual Studio, never Ninja. Every count here is a prediction you are expected to falsify if it is wrong.
