# G22b -- transfer function from TWO WAV files: offset, clock drift, resampling

*2026-11-10. Station 1-2 for the leftover of G22
(`docs/dsp/2026-11-10-g22-offline-wav-transfer.md` §4 B, deferred there). Code
read at `77e722d`. §6 is written after the code (CLAUDE.md rule 6). Unmeasured
on hardware.*

## 1. The problem, and what G22 already guarantees

G22 v1 takes two channels of ONE file. They share a clock, so one integer bulk
delay plus `Cumulative` averaging is exact up to the fractional-delay phase
slope. G22b takes two files:

- **Per-track recorders.** Waves Tracks Live, Reaper and some console SD
  recorders write one mono file per track. These files share a clock, but the
  start may differ.
- **Two devices.** For example a console recorder for the reference and a
  handheld recorder on the mic. The start offset is seconds, and the clocks
  differ by tens to a few hundred ppm.

Without correction the cost is severe. A total drift Delta (samples) spread
over the averaged range costs `20 log10 |sinc(f Delta / fs)|`:

- <= 0.5 dB at 20 kHz / 48 kHz needs Delta <= 0.44 samples.
- <= 0.1 dB needs Delta <= 0.20 samples.
- At 50 ppm (2.4 samples/s at 48 kHz) the 0.5 dB budget is gone after 0.18 s.

So a two-device pair is unusable without resampling. Detecting the drift alone
is not enough.

## 2. Station 1 (literature and practice)

**Sampling-rate-offset (SRO) estimation** for unsynchronised acoustic sensor
networks:

- The double-cross-correlation processor (Chinaev, Thüne, Enzner, ICASSP 2019;
  TASLP 2021, "Double-Cross-Correlation Processing for Blind Sampling-Rate and
  Time-Offset Estimation").
- Coherence-drift methods (Schmalenstroeer & Haeb-Umbach, MMSP 2017,
  EUSIPCO 2018).
- What these methods have in common: they estimate the inter-channel delay
  trend over time, its slope is the SRO, and an open-loop estimate is biased,
  so they iterate (estimate, resample, re-estimate). Station 1 did not retrieve
  their ppm-vs-length tables, so no figure from them is quoted here.

**Sub-sample delay.**

- A 3-point parabola on a GCC-PHAT peak is biased by an amount that depends on
  the true fraction. The bias is zero at 0 and ±0.5.
- Weighted least squares on the cross-spectrum phase after integer alignment
  has no interpolation bias. Weighting each bin by gamma^2 / (1 - gamma^2) is
  the inverse-variance choice, because the variance of the phase estimate is
  proportional to (1 - gamma^2) / gamma^2.
- The time-invariant part of the system's phase is the same in every window.
  It lands in the fit's intercept, never in the slope.

**Resampling.** Bandlimited interpolation with a windowed sinc (J.O. Smith,
"Digital Audio Resampling Home Page", CCRMA). Kaiser window design (Kaiser
1974, ISCAS; also in Oppenheim & Schafer):

- beta = 0.1102 (A - 8.7) for A > 50.
- N ~ (A - 8) / (2.285 dw).
- For A = 100 dB and a transition 0.45..0.55 fs (dw = 0.2 pi): beta = 10.06 and
  N = 64, i.e. ±32 taps.
- libsamplerate and soxr exist and would meet this. They are not needed: the
  kernel is 64 MACs per output sample.

**Start alignment.** Broadcast WAV `bext` TimeReference (EBU Tech 3285, samples
since midnight) is exposed by JUCE 9 as the reader metadata
`"bwav time reference"` (`juce_WavAudioFormat.cpp:52, 289-293, 1357-1369`).
Which recorders write it was not verified. It can only be a prior, never the
answer.

**Competitors.** REW's clock adjustment needs REW's own sweep with timing
markers at both ends (G22 §1). Station 1 found no generic two-file
transfer-function tool. Audacity's "latency correction" is a constant offset.

## 3. Candidates

**A. Global linear clock model + one resample, two-pass (chosen).**

- Measure the delay at K windows over the range, then fit a line to it.
- Resample the measurement onto the reference clock and fit again on the
  corrected pair.
- Compose the two fits, check the residual, and run G22's single-clock path
  unchanged on the corrected pair.
- For: one ratio and one offset, deterministic and testable against an analytic
  fixture. The second pass removes the open-loop bias the literature warns
  about.
- Against: a non-linear drift (crystal warm-up, a temperature change on the
  acoustic path) is not followed. It is measured as residual and refused.

**B. Piecewise / time-varying delay (track the delay, resample with a varying
ratio).**

- Follows warm-up and the acoustic path.
- Against: it also "corrects" real acoustic changes into the reference, so the
  transfer function stops being of one system. It needs a smoothing choice that
  nobody can check without hardware.
- Rejected for v1. If hardware shows real crystal warm-up over song-length
  ranges, B is the follow-up.

**C. Detect and refuse (no resampler).**

- Cheap.
- Against: §1 shows any realistic two-device pair would be refused, so the
  feature would exist only for per-track recorders.
- Kept as a branch inside A: a pair whose drift is negligible is not resampled.

**The obvious option, argued against: trust bext TimeReference for the offset
and skip the search.**

- TimeReference says when each recorder thinks it started. Its accuracy is the
  recorders' jam-sync, which is unknown and possibly absent.
- It also says nothing about drift.
- It is used only as the centre of the offset search.

## 4. Decisions

**E1. Input.**

- A reference file + channel and a measurement file + channel.
- WAV or AIFF only, as in G22 D1.
- Both files must have the same NOMINAL sample rate. If they differ, the
  operator gets a refusal naming both rates (an integer-ratio conversion is a
  later lane).
- The range (START, LENGTH) is on the reference file's timeline.

**E2. Coarse offset.**

- Prior P: the bext difference (meas minus ref, in samples) when BOTH files
  carry one, else 0.
- Search: one GCC-PHAT on the reference's first W_c = 2^17 samples of the
  range, zero-padded, against the measurement over [P - L, P + L + W_c), with
  L = round(10 s x fs).
- The highest-trust peak must be `Accepted`, else `NoCoarseAlignment` with the
  measured trust and floor.
- A recording started more than 10 s from the prior is refused, and the
  sentence says so.

**E3. Pass 1.**

- K = clamp(floor(rangeLength / 2^15), 3, 32) windows of W1 = 2^15 samples,
  evenly spaced over the range.
- In each window: an integer GCC-PHAT lag within ±(round(1e-3 x rangeLength) +
  64) of the coarse offset, then a fractional refinement by coherence-weighted
  phase-slope over bins with gamma^2 >= 0.5 and f <= fs/4.
- Only `Accepted` windows are kept. At least 3 are needed, else
  `NoCorrelation`.
- The line is fitted with Theil-Sen (median of pairwise slopes, median
  intercept). That is robust to up to ~29 % outlier windows, such as
  reverberant or silent ones.

**E4. Ratio limit.** |slope| > 1000 ppm is refused as `DriftTooLarge`, because
it is not a clock error: it is a mislabelled sample rate or a varispeed file.

**E5. Resample.**

- Measurement position for reference sample n: t(n) = b + n (1 + eps).
- Kaiser windowed sinc, ±32 taps, beta = 10.06.
- Guaranteed band f <= 0.45 fs (20 kHz at 44.1 kHz is 0.4535 fs, just outside
  it; at 48 kHz it is 0.417 fs).
- The interpolator is `core/` (pure DSP, closed-form tests). The pair source
  is `app/`.

**E6. Pass 2.**

- The same windows run on the corrected pair with W2 = 2^16. The result is a
  residual line (b2, eps2) and its residual RMS r.
- Compose: t(n) = b + b2 (1 + eps) + n (1 + eps)(1 + eps2).
- If r > 0.1 sample, the operator gets `DriftNotLinear`, which prints r and the
  peak residual: "the delay does not follow a straight line: a clock wandered
  or the acoustic path changed -- pick a shorter range".
- The 0.1-sample bound: a residual uniformly spread over ±0.17 samples (RMS
  0.1, total spread 0.35) costs 0.29 dB at 20 kHz / 48 kHz by §1's formula
  (sinc(0.417 x 0.35) = 0.967).
- r includes the per-window estimator noise, so it overstates the true
  non-linearity. The gate errs toward refusing.
- A tighter 0.05 would cost 0.07 dB, but estimator noise of 0.01-0.05 sample
  per window (station 1, inference) would then refuse clean pairs.

**E7. Shared-clock branch.** If |eps| x rangeLength <= 0.05 sample after
pass 1, there is no resampling and no pass 2. Only the integer offset is
applied (G22's engine path). The readout says "shared clock".

**E8. Analysis.**

- G22's `analyseWavTransfer`, unchanged, runs on a two-channel
  `ResampledPairSource`: channel 0 is the reference, read directly; channel 1
  is the measurement, mapped and resampled.
- Its delay windows and DelayMoved check then act as an independent check that
  the correction worked.

**E9. Output.**

- The trace is as G22 D6, with
  `deviceName = "WAV <meas file> ch<M> vs <ref file> ch<R>"`.
- The readout adds `clock <+x.xx> ppm corrected` (or `shared clock`) and
  `offset <s.sss> s`. ppm uses two decimals because a 0.01 ppm step is what
  the pass-2 fit resolves at song length. Seconds use three decimals.
- No schema change.

**E10. Cancel and threads.** As G22 D7: on the worker, with cancel polled
between windows and blocks.

## 5. Proof shape (the plan's tests)

- **Interpolator.**
  - Integer positions with step 1 reproduce the input exactly, because the
    sinc is a delta at the integers whatever the window.
  - A sinusoid at f <= 0.45 fs, resampled at step 1 + eps from a fractional
    start, matches sin(omega (t0 + k step) + phi) within a bound derived from
    the Kaiser design plus float32 input rounding.
- **Theil-Sen.**
  - An exact line is returned exactly.
  - A line plus 25 % gross outliers still returns the line.
  - Residual RMS is checked against a hand-computed set.
- **Pair, analytic fixture (independent of the resampler).**
  - ref[n] = sum of a_k sin(omega_k n + phi_k), with tones on the reference
    16 384-FFT bin centres.
  - meas[n] = sum of a_k |H(omega_k)| sin(omega_k ((n - b) / (1 + eps)) +
    phi_k + arg H(omega_k)): the same signal, filtered and sampled by a clock
    eps fast, starting b samples later.
  - Cases: eps = +80 ppm with b = 3.2 s; eps = -150 ppm; and eps = 0 with
    b = 1.7 s (the shared-clock branch).
  - Assert |eps_hat - eps| and the magnitude/phase at the tone bins within
    derived bounds.
- **Refusals:** rate mismatch, offset > 10 s, 1500 ppm, a delay that bends
  mid-range (DriftNotLinear), no correlation.
- **JUCE:** two WAV files written in the test, one pair carrying bext time
  references whose difference is the true offset beyond 10 s (found only
  through the prior).
