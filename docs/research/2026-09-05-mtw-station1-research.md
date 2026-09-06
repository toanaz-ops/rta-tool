# Lane L3 — station-1 research and the decisions it produced

*2026-09-05. Three read-only research agents ran in parallel: literature and
commercial practice, open-source code, and this repo's plug-in surface. Their
findings are preserved here because the decision record
(`docs/dsp/2026-09-05-mtw-l3.md`) cites them but a citation is not the finding.
The last section maps every decision to the evidence that produced it, the
alternative it rejected, and what the verifiers later corrected.*

---

## Part A — Literature and commercial practice

### Smaart (Rational Acoustics)

| Claim | Source |
|---|---|
| MTW "uses a series of sample rate decimations and varying FFT sizes to produce a measurement with different time and frequency resolutions in different frequency ranges" | Smaart v7 User Guide, p.82 |
| "The individual FFT sizes used in this case are not user selectable" — never published | Smaart v8 User Guide, p.68 |
| ~840 frequency data points (v7); "about 800 … or fewer" (v8, p.118); "nearly 20 times fewer" than a 32K FFT | v7, v8 guides |
| Longest window "a little over one second" | v8 guide p.44; LE v9.1 p.52 |
| Better than 1 Hz at the lowest frequencies at 48 kHz (32K FFT: ~1.5 Hz) | v7 guide |
| Window: MTW = Hann, MTW+ = Hamming | support article "What is (Legacy) MTW+?" |
| Standard real-time FFTs run 24×/s (~42 ms); HF windows shorter than that left "time gaps", MTW+ closes them | *What's New in Smaart 9.0*, pp.21–22 |
| v9 "always calculate BOTH an MTW and a single FFT version" of every TF; "still have some issues to solve regarding how we select and control which data set to display"; "Use MTW Coh" option | *What's New in v9*, p.21 |
| v9 replaced 2/4/8/16 FIFO + 1–10 s + Fast/Slow with a bare 1–10 depth because "the actual integration time varies by frequency" | *What's New in v9*, p.19 |
| One delay line per TF engine, applied to the reference; nothing published about before/after decimation or fractional samples | LE v9.1 p.71 |
| Coherence drops first at HF with delay error — "stair step" pattern | LE v9.1 p.68; v8 Fig. 112 |
| No MTW patent found (Rational Acoustics, Berkow, Anderson searched) | Google Patents |

### FPPO / Meyer SIM lineage

- Henderson, *FFT Fundamentals in SmaartLive* (SIA): FPPO = "variable analysis
  frame lengths … equal density per octave, in this case 24 points per octave".
- McCarthy: SIM's display is "a patchwork of 8 or so linear computations …
  a sequence of DIFFERENT time record lengths"; HF time record ≈ 5 ms; coherence
  blanking 90 % at 2 averages, 20 % at 16; tonal < 6 wavelengths, spatial < 24,
  echo beyond. Book page citations could not be verified online.

### SysTune TFC (AFMG manual 1.3, p.101)

"For each doubling of the frequency the window length is cut in half"; Tukey
50 % window; example 10 ms at 1 kHz → 80 ms at 125 Hz, 1.25 ms at 8 kHz.
Explicitly positioned against band-based schemes: "not based on octave or
1/3rd octave bands … a continuous weighting function … without discontinuities
or gaps". Applied to the impulse response. Documented side effect: LF level up,
HF level down in dense reflection fields. Smaart's analogue is FTW smoothing
(complex-domain, linear-bandwidth kernel, 20 ms window ≈ 100 Hz resolution).

### Practitioners (Merlijn van Veen)

- "eight [parallel time windows] for 10 audible octaves" — the only window count
  found for Smaart, from a practitioner post, not Rational Acoustics.
- A reflection is negligible when ≥ 10 dB down or leading/lagging by ≥ 40 % of
  the time record.
- Perceptual bounds: ~140 PPO ceiling, 12 PPO floor for tonal coloration.

### Academic multi-resolution

Brown 1991 / Brown & Puckette 1992 (CQT); Schörkhuber & Klapuri 2010 (octave-wise
decimation, one kernel per octave); Dressler DAFx-06 MRFFT (sizes not
retrieved); Gaborator (Gaussian CQ filters at power-of-two rates, synchronised
sample instants); Heinzel, Rüdiger & Schilling *GH_FFT* — the reference for
cross-resolution normalisation: PSD = PS / ENBW, ENBW ∝ 1/N, so a PSD-normalised
spectrum is invariant to N.

### Where sources disagree

1. Banded vs continuous: AFMG says band schemes have gaps; Rational Acoustics
   never concedes one; nobody publishes a measured seam error.
2. Window count: McCarthy "8 or so" (SIM), van Veen "eight" (Smaart), Rational
   Acoustics silent.
3. "v9 = FPPO" appeared in a search summary; no Rational Acoustics document says
   it. Treated as wrong.
4. "Better than 1/48 octave from 60 Hz up" — in this project's spec, not found
   in any Smaart document; verified Smaart figures are only ~840 points, ~1 Hz,
   ~1 s.
5. Extra windowing on top of MTW: van Veen against; Smaart (FTW) and AFMG (TFC)
   both ship it.

### What nobody documents

The band table; the stitching rule (hard cut vs cross-fade); phase continuity at
seams; magnitude normalisation across resolutions (the GH_FFT answer exists in
metrology, not in audio-measurement sources); delay compensation in a multirate
path; anti-alias group delay per stage; **any verification of an MTW output
against a reference**.

---

## Part B — Open-source code

### Open Sound Meter — the only open MTW analogue (`src/math/fouriertransform.cpp`, `prepareLog()`)

```cpp
const int ppo = 24, octaves = 11;
unsigned int startWindow = pow(2, 16), startOffset = 1'344'000 / sampleRate();
float wFactor = powf(10.f, 1.f / (-octaves * ppo / 2.5));
// per band i of 264:  N = startWindow * pow(wFactor, i);  frequency = offset / N;
```

Not stitched FFTs: 264 log-spaced bands, each a windowed single-bin DFT against
a ring buffer with its own window length, `N_i · f_i = 1'344'000` constant →
**~28 cycles per window at 48 kHz**, 24 PPO, no seam because no band boundary.
`Align {Right, Center}` fixes a common time origin across window lengths.
`Norm::Sqrt` divides by band `N`. Modes `FFT10..FFT16` + `LFT`; averaging
Off/LPF/FIFO; coherence ring depth 21; delay finder every 25 cycles. Basis
recomputed on sample-rate change. Issue tracker: no "LTW" hits.

### Friture (`friture/filter.py`, `filter_design.py`, `signal/decimate.py`)

9-octave decimated bank; decimation lowpass `iirdesign(0.48, 0.50, 0.05, 70,
ftype='ellip')`; band filters `ellip(2, 0.5, 50, bandpass)`; factor hard-coded 2;
causal `lfilter` with carried state. **No group-delay compensation between
octaves** — invisible because Friture shows levels, never phase.

### librosa CQT (`constantq.py`)

Early downsample by powers of two (`soxr_hq`), `fft_basis *= sqrt(sr/my_sr)` so
octaves line up in level, hop forced divisible by 2^octaves so frame grids
coincide, `__trim_stack` to the minimum frame count. Offline luxuries.

### Others

Gaborator: power-of-two rates, synchronised sample instants, constants not
public. OpenOptimize (JUCE, FFTW, r8brain): abandoned, constants not found.
`scipy.signal.decimate`: order-8 Chebyshev I default, `zero_phase=True` via
`filtfilt` — unavailable in real time. pyfar `reconstructing_fractional_octave_bands`:
a stitching *criterion* (summed output equals input), single-rate.

### Design choices that differ

| | resolution | seam | level normalisation | time origin |
|---|---|---|---|---|
| OSM LTW | per-band sliding DFT, 264 bands | none (continuous) | divide by band N | `Align` per band |
| Friture | decimation cascade | none, magnitude only | filter design | ignored |
| librosa | resample per octave + STFT | trim to common grid | `sqrt(sr/my_sr)` | hop divisibility |
| Gaborator | Gaussian CQ at 2^-k rates | synchronised | not found | by construction |

### Traps a real-time C++ engine hits that offline tools avoid

Zero-phase filtering unavailable; per-octave cascade delay is frequency-dependent
near cutoff; different window lengths have different ENBW and coherent gain;
averaging depth in frames means different seconds per band; the 2^16 low band
costs 1.4 s of fill; the band table depends on sample rate; basis construction
must happen off the audio thread; 264 sliding bands cost O(ΣN) per update.

---

## Part C — This repo's plug-in surface (as of `7e10eb6`)

- `DualFftEngine::Config`: `fftSize` (power of two ≥ 4, no upper bound),
  `hopSize` 1..fftSize, `window`, `TransferAveraging {Fifo, Exponential}`,
  `fifoDepth` (≥ 1, no cap), `timeConstantSeconds` → `alpha = 1 − exp(−hop/(fs·τ))`,
  `referenceDelaySamples` as an integer stream skip before the transform,
  `minimumEffectiveAverages = 8`. `process(reference, measurement)` accepts any
  equal block length and rings internally; no allocation after construction;
  ring capacity `bit_ceil(4·fftSize + skip)`; FIFO storage allocated
  unconditionally.
- `TransferEstimator`: H1/H2/Hv from `Sxx/Syy/Sxy`; `γ² = |Sxy|²/(Sxx·Syy)`;
  `TransferSnapshot` with ONE `binWidthHz`; `makeSnapshot()` is the only place
  coherence is assigned, enforced by `coherence_gate_is_not_bypassed` — a grep
  for literal tokens over every `core/` file except `TransferEstimator.cpp`.
- `AverageCount`: `overlapCorrelation(window, lag)` = Σw[n]w[n+lag]/Σw², no
  wraparound, 0 for lag ≥ N; `fifoEffectiveAverages`, `exponentialEffectiveAverages`
  — both apply the overlap penalty `D = 1 + 2Σ c(m·hop)²`, which the header's
  `(2−a)/a` ceiling does not.
- `measure::Snapshot`: one `fftSize`, no frequency vector; `Trace` derives its
  axis from `fftSize` and refuses anything else. `Analyser` owns one
  `DualFftEngine`; `AnalysisThread` hands it one hop at a time, publishes ≤ 20 Hz
  by atomic shared_ptr swap; allocation sanctioned on the analysis thread only.
- `docs/dsp/2026-08-27-filterbank.md`: single-rate bank, decimation cascade
  removed, "decimation returns only with a profiler result in hand".
- `filter_design_has_no_polynomial_form` forbids `output='ba'`, `zpk2tf`,
  `tf2sos`, `signal.lfilter`, `freqz(b` in `core/` and `tools/*.py`.
- Line counts: `DualFftEngine.cpp` 321, `test_dualfft.cpp` 400 (cap) — neither
  may grow.

---

## Part D — Decisions, their evidence, and what the verifiers changed

| # | Decision (record §) | Evidence from research | Alternative rejected, and its cost | Verifier correction |
|---|---|---|---|---|
| 1 | **No decimation**: K+1 `DualFftEngine`s at full rate, `N_k = N_0·2^k`, hop `N_k/4` (§2) | A: Smaart decimates but publishes no table; C: filterbank record's rule; B: every offline tool leans on zero-phase filtering unavailable in real time | Decimation cascade (roadmap's assumption): anti-alias design + golden, aliasing margins per stage, fractional delays at decimated rates, ~40 ms inter-band transient skew — to save ~44 MFLOP/s | Flops 400→910/sample (44 MFLOP/s); memory "1 MB"→ rings 4 MB + FIFO 2.08 MB per frame of depth; aliasing does **not** cancel (strengthens the decision) |
| 2 | Band `k` owns `[fs/(8·2^k), fs/(4·2^k))`; owned bins `N_0/8..N_0/4−1 = 128..255` in every band; 1281 points (§3) | Spec target ≥ 48 PPO from 60 Hz → `N_0 ≥ 554`; Smaart ~840 points / ~1 s / ~1 Hz | OSM's 28 cycles / 24 PPO: fails the 48-PPO target; a second cross-spectrum/coherence implementation; O(ΣN) per update | Formula written `N_k/8` → `N_0/8`; 1282 → 1281 (bottom band 256 bins; 257 duplicated 187.5 Hz at a seam) |
| 3 | `K = 6` default (1.365 s longest window), `K = 7` optional (§3) | Smaart "a little over one second"; "beat it at the bottom octave" = 0.37 Hz at 2.73 s | K = 7 default: 2.73 s fill and 10.9 s coherence fill below 94 Hz | — |
| 4 | Hard cut at seams, seams exported and drawn, no cross-fade (§4) | A: AFMG's "gaps" critique vs Rational Acoustics' silence, nobody measures the seam; the reflection-admission identity `H = 1 + a·r_w(τ)e^{−jωτ}` shows the step is physics | Cross-fade: an unpublished weight law baked into the golden, hides a real room difference | Identity confirmed exact in expectation; `r_w(1440)` for the 2048 band = 0.0169, so band 1 admits a 1.7 % ripple |
| 5 | **Frames-uniform averaging**, seconds reported per band; depth 16 default, cap 32; `fifoDepth = 1` to bands in Exponential mode (§5) | A: Smaart v9 dropped seconds because integration time varies by frequency; C: `exponentialEffectiveAverages` includes the overlap penalty | **Seconds-uniform (the record's first draft)**: at 0.5 s three bands never reach the gate of 8 (Neff 2.06/3.55/6.58); smallest passing value 3.5 s, top band at 682 looks; FIFO in seconds cannot open the bottom band under a 4 s cap. Per-band τ: seven knobs. Lower gate: publishes what the gate suppresses | The planner's computation forced the reversal; `fifoEffectiveAverages(hann, N/4, 16) = 8.5866`, gate first clears at frame 15 |
| 6 | Reference delay at full rate, integer samples, carried by each band engine (§2) | C: `referenceDelaySamples` is an upstream integer skip; `DelayFinder::subSample` is a documented gap with no consumer | Shared upstream skip: would need logic extracted from `DualFftEngine.cpp`, which may not grow | T2 exactness confirmed bit-for-bit for any integer D |
| 7 | `MtwResult` copies **no** coherence; per-band `TransferSnapshot`s + `(band, bin)` index; flat copy only in `app/` (§5) | C: the guard is a literal-token grep | Stitched `coherence` member in `core/`: trips the guard or evades it | Found by the record verifier; `DualFftEngine` is not movable → `unique_ptr` per band (builder) |
| 8 | Concurrent with the fixed engine; `Snapshot::mtw` with explicit frequency vector; view draws via `xForHz`, seams on all panes, source toggle default MTW; **integration seconds shown** (§6) | A: Smaart v9 dual-engine + "Use MTW Coh"; C: `Trace` cannot take a frequency vector without an L5 amendment | Resampling MTW to a uniform grid: destroys the point of MTW | L3b verifier: seconds were not rendered and no negative seam test existed → fixed in `55d7314`; coherence pane draws seams too |
| 9 | Verification by identities and golden (§7 T1–T7) | A: nobody publishes an MTW verification — the gap this project fills; C: two-term float32 tolerance per band | Regression locks on the code's own output | Builder found the plan's `firstIndex` rotation mutation invisible to pure-delay tests, and the compensated-delay fixture unable to catch a missing degrees conversion |

Not decided, on purpose (record §8): cross-fade pending a room probe; MTW trace
storage (L5 amendment); FTW/TFC-style IR post-windowing (new gap entry); MTW
RTA banding and coherence weighting (L6b); `K = 7` as default.

## Outcome

Built and merged to `main` at `a1a9ebf`, 2026-09-06: 436/436 (ON), 390/390
(OFF), 0 warnings, guards 96/62/113/30 — `docs/reports/005-mtw-engine.md`.
