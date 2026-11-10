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

## 6. Shipped (amendments against the code)

*Commits `fd16f35`..`57fe477`, fix rounds `1ae0016` and `0d5a108`. Fable review
rounds: (1) 2 HIGH, 2 MEDIUM, 6 LOW; (2) 0 HIGH, 1 MEDIUM, 7 LOW; (3) 0 HIGH, 0 MEDIUM, 7 LOW.
Each item below supersedes the sentence of §2-§5 it names.*

- **E2, the prior's sign.** The mapping t(n) = offset + n x ratio takes a reference
  index to a measurement index, so the offset is the measurement index of
  reference sample 0.
  - bext TimeReference is the wall-clock sample count of a file's FIRST sample,
    so prior = T_ref - T_meas.
  - The first build used T_meas - T_ref. The 14 s fixture had the same
    inversion, so the test passed against physics-reversed files (review 1
    HIGH 1).
  - Now tested both ways: the measurement starts 14 s earlier, and 14 s later.
- **E2, retry.** If the coarse window at the range start is refused, one retry
  runs at the middle of the part of the range that can map into the measurement
  file (prior and half-width taken into account). A measurement span shorter
  than the 2^17 coarse window gets its own sentence (`CoarseSpanTooShort`).
- **E3, fraction.** The fraction is RELATIVE to the highest-trust (anchor)
  window: weighted LS slope of unwrap(arg(G_i conj(G_a))), with weights
  1 / ((1 - g_i)/g_i + (1 - g_a)/g_a), both coherences >= 0.5, and the relative
  fraction within 1 sample.
  - The system's phase cancels exactly for any two integer alignments, because
    an integer shift is pure linear phase.
  - The absolute per-window fit of the first build dropped EVERY window on a
    system with an LR4 crossover (100 Hz or 1500 Hz) -> NoCorrelation (review 1
    HIGH 2). §2's "lands in the fit's intercept, never in the slope" is true
    only for phase linear in f.
  - The anchor's own fraction is its absolute fit when that fit is within one
    sample, else 0. That constant offset is absorbed by G22's integer
    re-alignment, and the sub-sample remainder stays in the stored phase,
    exactly as on the one-file path (LR4 at 1500 Hz, shared clock, stores
    "delay -1 samples").
- **E3, band.** Coarse and pass 1 use GCC-PHAT and the phase-slope fit over
  100 Hz..3 kHz. Pass 2 (on the corrected pair) uses the full band, with the fit
  up to fs/4.
  - Reason: drift smears each window, and the smear decorrelates the high bins
    first, while PHAT weights every bin equally.
  - The full-band first build had a clean ceiling of 100 ppm. Band-limited, it
    is 400 ppm.
- **E3, lag bound, coverage, length.**
  - Lag bound: ±(maxPpm x 1e-6 x range + 64).
  - Accepted windows must span >= 60 % of the range.
  - Minimum range 2^17 samples (2.731 s at 48 kHz).
  - `RangeTooLong` when maxPpm x 1e-6 x range > 2^15/4 (7.1 min at
    48 kHz / 400 ppm).
- **E4.** The ceiling is `maxPpm` = 400, MEASURED on the pink + biquad fixture
  (9/9 windows clean at 400; at 600 the 2^17 coarse window smears 79 samples).
  §4's 1000 ppm is not reachable.
- **E6, uncertainty gate (added).**
  - `ClockUncertain` when u = r2 x sqrt(12/K2) x range / spread2 > 0.1 sample
    (LS slope standard error carried over the range). It uses pass 2's r2, K2
    and spread.
  - The limit on r2 depends on K2: 0.025 at the 2^17 minimum, 0.046 at 6.5 s,
    and the gate is inert from K = 14 (range >= 917 504 samples, 19.1 s at
    48 kHz), where E6's 0.1 binds first.
  - u under-predicts the Theil-Sen error by ~16 % at K = 4.
  - On a 3 s flat fixture it flips at ~5 dB SNR. Whether real reverberant rooms
    trip it at short ranges is unmeasured until hardware (HW-175).
- **E7.** Pass 2 always runs, full band, so the integer is settled.
  - "Shared clock" requires pass 1's |eps| x range <= 0.05 sample AND pass 2's
    |slope| x range <= 0.05. Pass 1's residual RMS is not gated.
  - The pass-2 guard has no RED mutant: no fixture makes pass 1 say shared
    while pass 2 disagrees.
- **Overlap (added).**
  - Before pass 1, the range is checked against the measurement's extent
    mapped through the coarse offset, with margin ceil(maxPpm x 1e-6 x length)
    + 32 taps.
  - A TYPED length that runs past it is refused as `RangeOutsideFile`, with the
    overlap in seconds (three decimals).
  - In pair mode LENGTH empty means "to the end of the overlap", so the UI
    leaves LENGTH empty in pair mode. One-file behaviour is unchanged.
  - The empty-LENGTH range is CLIPPED, not refused. Its margin is recomputed
    from the clipped length (clip, re-margin, clip again; converges within one
    sample).
  - A clipped remainder of 1..2^17-1 samples after START is refused as
    `RangeTooShort` ("Only x.xxx s of the reference after the start are
    covered by both files...").
  - A remainder of 0 is refused as `RangeOutsideFile`.
- **Relative delay, sample rate.** `relativeFractionalDelay` uses the window's
  own sample rate and refuses (nullopt) when window and anchor rates differ.
- **E9.** ppm `%+.2f`, offset `%.3f` s, all seconds in sentences three
  decimals. `deviceName = "WAV <meas file> ch<M> vs <ref file> ch<R>"`, and
  one-file output is byte-identical.
- **Fixture independence (§5).** The measurement is pink noise through the
  system, resampled by the test's own ±256-tap Blackman-Harris sinc (checked on
  a sinusoid, 9.5e-8 against a 1e-4 bound), never by the production
  interpolator. The 1500 ppm refusal of §5 became 480 and 1000 ppm cases.

**Round-3 LOWs -- all four fixed in `d1b6430`** (a swallowed first clip now
re-margins and reports `RangeTooShort`; a sub-millisecond remainder prints in
samples; the default band edge uses the window's sample rate; the LR4 comment
names PHAT's band-limited lag). As found:

- `WavPairClock.cpp:198-206`: when the first clip already leaves no range, the
  refusal is `RangeOutsideFile` quoting the unclipped end, in a band about one
  margin wide. Both answers send START earlier.
- `test_wav_pair_lr4.cpp:65-70`: the comment blames the absolute fit for a
  14.8-sample miss that is PHAT's band-limited integer lag (LR4 at 1500 Hz has
  ~15 samples of group delay).
- `ClockDrift.cpp:58-62`: the default band edge still reads
  `options.sampleRate`.
- `WavPairText.cpp:61-62`: a 1..23-sample remainder prints "Only 0.000 s".
