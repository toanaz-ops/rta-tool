# FIR export (lane L7, sub-lane L7-FIR, gap G10): design method, phase modes, file format, proof strategy

*Decision record, station 2 of the pipeline in `docs/reports/README.md`.
Written 2026-09-06 on `4b05049` from the station-1 report preserved in
`docs/research/2026-09-06-l7-fir-export-station1-research.md`. That report
flagged itself UNVERIFIED throughout — it was written with no scipy and no web
access — so this record re-checked every external claim it leans on before
using it, and §12 is the ledger of what was verified, how, and what was not.
Every number below is closed-form, quoted from a source that was actually
read this session, or computed from a function that already ships in `core/`.
Two of the report's premises were wrong and are corrected in §1 so nobody
builds on them.*

## 0. What G10 is for, in one paragraph

An operator has a correction — from a target curve today, from the auto-EQ
solver when L7 builds it — and a convolver somewhere downstream that will
apply it: a plug-in, CamillaDSP, a hardware DSP with a fixed tap budget. G10
turns that correction into a coefficient file the convolver can load without
anyone guessing the sample rate, the tap count, the phase type or the gain
convention. The design mathematics is `core/` (span in, coefficients out);
writing the file is `app/`. No playback is needed, which is why this is the
first L7 sub-lane to move.

## 1. Provenance: what station 1 got right, and two premises it got wrong

Right, and confirmed against the real files: `RealFft` has an exact inverse
(`RealFft.h:40-47`, matching `numpy.fft.irfft`'s treatment of the DC/Nyquist
imaginary parts); `Window` is periodic and its correction factors are
asserted to 1e-12 (`Window.h:14-21`); `TransferSnapshot::kMagnitudeFloorDb =
-120.0f` exists (`TransferEstimator.h:70`); `core/` links no linear-algebra
library and contains no solver of any kind (grep for `toeplitz|levinson|
cholesky|solve(` over `core/` returns nothing); `Fft` and `RealFft` are float32
throughout and power-of-two only (`Fft.h:12,23`); the golden helper is
`core/tests/support/Golden.h` and every golden is a flat file under
`core/tests/golden/`; `tools/gen_mtw.py` is the argparse precedent.

**Corrected premise 1 — the least-squares system is not Toeplitz.** The
report proposed a Levinson-type recursion for the `firls`-shaped normal
equations. Read from `scipy/signal/_fir_filter_design.py` at `main` this
session: `Q1 = toeplitz(q[:M+1]); Q2 = hankel(q[:M+1], q[M:]); Q = Q1 + Q2`,
then `solve(Q, b, assume_a="pos")` — a Cholesky solve of a
**Toeplitz-plus-Hankel** matrix — with `lstsq(Q, b, lapack_driver='gelsy')`
as the fallback when an `Ill-conditioned matrix` warning or `LinAlgError` is
raised. The algorithm is Selesnick's (scipy cites "Ivan Selesnick,
Linear-Phase Fir Filter Design By Least Squares. OpenStax CNX. Aug 9, 2005").
Levinson-Durbin solves pure Toeplitz systems; it does not apply here as
written. The cost estimate in §2 uses the dense solve scipy actually does.

**Corrected premise 2 — a WAV header does not make the sample rate safe.**
The report's export argument rested on "WAV carries SR for free — the one
format where a mismatch is detectable." CamillaDSP's README, read this
session, says of the `Wav` coefficient type: **"The sample rate of the file is
ignored."** The most widely used open convolver reads the taps and discards
the header field. §5 is designed around this.

One more thing the report recalled correctly but incompletely: scipy's
`minimum_phase` **halves the log-magnitude by default** (`half=True`, `h_temp
*= 0.5`), so its default output has the *square root* of the input's
magnitude at half the tap count. The "same magnitude as the linear-phase
sibling" identity the report proposed as the key test holds only for the
`half=False` configuration. §4 fixes which one this project means.

## 2. Decision: v1 ships frequency sampling; weighted least-squares is decided in principle, sized honestly, and built when a measurement says the window's smear costs a real correction

**Decision.** G10 v1 designs by **frequency sampling**: the target magnitude
(and, for linear phase, zero phase) is sampled on an `M`-point linear grid
(`M` a power of two, `M ≥ 8N`), inverted with `RealFft::inverse`, circularly
shifted so the zero-phase response is centred, windowed with a **periodic**
`Window` of length `N`, and truncated to `N` taps. Weighted least-squares in
the Selesnick/`firls` form is **adopted as the second method** — Type I
(odd `N`), Toeplitz-plus-Hankel normal equations, Cholesky in double with a
conditioning fallback — but is **not built in v1**. It is built when the
measurement in the last paragraph of this section shows the window's smear
exceeding the operator's stated tolerance on a real correction curve.

**Argument against the obvious — frequency sampling as the only method,
forever.** The window smears the target uniformly in *linear* frequency. A
periodic Hann of length `N` has a null-to-null mainlobe of `4·fs/N`; at
`fs = 48 kHz`:

| N | fs/N (bin) | Hann mainlobe 4fs/N | at 40 Hz that is | at 4 kHz that is | linear-phase latency (N−1)/(2fs) |
|---|---|---|---|---|---|
| 1024 | 46.9 Hz | 187.5 Hz | > 2 octaves | 1/15 oct | 10.66 ms |
| 4096 | 11.7 Hz | 46.9 Hz | ~1.1 octave | 1/60 oct | 42.66 ms |
| 6144 | 7.8 Hz | 31.3 Hz | ~0.8 octave | 1/90 oct | 63.99 ms |
| 8192 | 5.9 Hz | 23.4 Hz | ~0.6 octave | 1/120 oct | 85.32 ms |
| 65536 | 0.73 Hz | 2.9 Hz | 1/10 oct | 1/1000 oct | 682.65 ms |

Below ~8k taps — the regime every hardware convolver lives in — the window
resolves a 40 Hz feature to no better than half an octave while
over-resolving 4 kHz a hundredfold. A weight `W(f)` in a least-squares
design is the only knob that spends the fixed `N` degrees of freedom where a
log-frequency ear wants them: it lets the response carry detail *narrower
than fs/N* at LF at the price of ripple at HF, which a window cannot do at
any setting. That is what least-squares buys and frequency sampling cannot.
It is a real cost of shipping frequency sampling alone, and it is why the
method is decided now and not left as "maybe later".

**Argument against the obvious in the other direction — least-squares is not
free, and at LF-correction tap counts it is not feasible as scipy does it.**
`Q` is `(M+1)×(M+1)` with `M = (N−1)/2`, dense, in double:

| N | M+1 | Q storage | Cholesky ≈ (M+1)³/3 flops |
|---|---|---|---|
| 1023 | 512 | 2.1 MB | 4.5e7 |
| 4095 | 2048 | 33.6 MB | 2.9e9 |
| 8191 | 4096 | 134 MB | 2.3e10 |
| 65535 | 32768 | 8.6 GB | 1.2e13 |

So least-squares is practical exactly where it matters most (≤ 8k taps,
hardware convolvers) and impractical exactly where the LF resolution problem
disappears anyway (65k taps, where fs/N is 0.73 Hz and the smear is a tenth of
an octave at 40 Hz). A fast Toeplitz-plus-Hankel solver would change the
flop column, not the storage column, and is new numerical code with no
precedent in this repo. This is why v1 does not wait for it.

**What v1 does instead of weighting.** Two things a smoothed target already
gives us: the target arrives banded (`OctaveBands` / `BandWeights`), so it
has no detail the window cannot represent above a few hundred hertz; and
"do not correct where you do not trust" is applied **to the target** — the
correction is tapered to 0 dB above the operator's chosen upper limit — not
to an error weight. Neither fixes the LF smear. They make it the *only*
residual problem, which is what the measurement below then quantifies.

**The measurement that triggers building least-squares.** In `gen_fir.py`
(main-checkout venv), for a real correction curve captured by L5/L6b at
`N ∈ {4095, 6143, 8191}`: the target-minus-response residual in 1/6-octave
bands below 200 Hz for (a) frequency sampling with periodic Hann and (b)
`scipy.signal.firls` with `W(f) ∝ 1/f`. If (a)−(b) exceeds 1 dB in any band
the operator is likely to correct, the second method is scheduled. The
number is recorded in this file as an amendment with the curve it came from,
per `memory/a-threshold-read-off-a-grid-is-that-grids-floor.md` — 1 dB is
this record's proposal, not a measured floor.

**Cost of the rejected alternative, stated.** Building least-squares in v1
would have cost a dense double-precision Cholesky with a fallback, a
conditioning story for steep narrow boosts (the failure mode is large
spurious taps, not an exception — scipy's fallback to `gelsy` is a symptom of
this), odd-`N`-only (`firls`: "numtaps must be odd"), and a golden whose
tolerance depends on `cond(Q)` (§7). Deferring it costs the LF smear above,
measured rather than assumed.

## 3. Decision: Parks–McClellan is not built for G10 and is parked for G18

**Decision.** Equiripple (Remez exchange) design is rejected for correction
curves and reserved for the crossover surface (G18, same L7 row of
`docs/plans/MASTER-EXECUTION-PLAN.md:125`).

**Why the reasoning holds without any unverified claim.** Verified from the
`scipy.signal.remez` documentation: `bands` is "a monotonic sequence
containing the band edges" and `desired` is "a sequence half the size of
bands containing the desired gain **in each** of the specified bands" — one
gain per band, i.e. piecewise-constant. A measured correction is a continuous
many-featured curve; coercing it into bands either loses it (few bands) or
hands the exchange algorithm hundreds of one-bin bands, which is the
brickwall/crossover problem shape wearing the wrong clothes. The error norm
is also wrong for the ear: equal ripple in *linear* frequency, where a
least-squares weight can be log-spaced directly. Those two arguments are
closed-form and sufficient. **The report's third argument — that Remez
convergence "gets unreliable at high order" — stays UNVERIFIED**: scipy's
docs say only that `maxiter` defaults to 25 and give no high-order warning;
the issue-tracker discussion the report recalled was not found this session.
It is not load-bearing and is not cited as fact.

## 4. Decision: two phase modes, no hidden default; minimum phase is homomorphic, log not halved, with the magnitude identity as the proof

**Decision.** The export offers **linear phase** and **minimum phase** as
parallel choices. There is no default the UI pre-selects silently: the
operator picks, and the UI prints, from `N` and `fs` alone, the two exact
numbers each choice costs — `latency = (N−1)/(2·fs)` for linear phase (this
is the whole filter length: 683 ms at 65536 taps, 48 kHz, unusable at a
show), and "latency: only what the magnitude implies; **the room's measured
phase is not corrected**" for minimum phase. Both are true statements of a
trade the operator owns (studio/offline vs live); this codebase does not make
it for them. rePhase's mixed "excess-phase" blend is a real third design
point and is **not** in v1 (§11).

**Linear phase, by construction.** The zero-phase design is made symmetric
**explicitly** — compute `taps[0..(N−1)/2]`, mirror — so `taps[n] ==
taps[N−1−n]` is bitwise, not to rounding, and the phase identity below is
then algebra, not a hope. Type I (odd `N`, integer group delay `(N−1)/2`) is
the default; even `N` is accepted (a power-of-two tap budget is common) and
documented as a half-sample group delay, which a convolver does not care
about and a test must (§7, item 2).

**Minimum phase, derived.** Homomorphic (real-cepstrum) method, as verified
line-by-line from scipy's `minimum_phase` source this session and as
Oppenheim & Schafer derive it (book not opened; the scipy source is the
verified reference):

1. `X = FFT(h_lin, n_fft)`; `A = |X|`, floored (see below); `c = IFFT(log A)`
   — the real cepstrum. **The log is not halved.** scipy's default `half=True`
   applies `h_temp *= 0.5` and returns `(len(h)+1)//2` taps with the *square
   root* of the magnitude; this project uses the `half=False` configuration
   so that `|H_min| = |H_lin|`, which is what "the same correction, without
   pre-ringing" means.
2. Fold: `win = [1, 2, 2, …, 2, 1 + (n_fft % 2)]` over `[0, n_fft/2]`, zero
   above; `c_min = c · win`. This converts the even (zero-phase) cepstrum into
   the causal complex cepstrum of the minimum-phase system with the same
   magnitude. Exact, not an approximation, given no cepstral aliasing.
3. `h_min = Re(IFFT(exp(FFT(c_min))))`, then truncated to `N` taps.

**The floor.** `log A` is undefined at zero. scipy adds `1e-7 · min(A[A>0])`.
This project instead floors `A` at `10^(kMagnitudeFloorDb/20)` relative to
unity — the same **−120 dB** the transfer snapshot already uses
(`TransferEstimator.h:70`) — because a correction filter's `|H|` is in the
same unit (dimensionless, re unity) and a second floor constant for the same
physical reason is the drift `memory/` exists to prevent. A bin at the floor
carries no energy; its phase is whatever the floor implies, and that is
acceptable exactly because it carries no energy.

**The oversampling factor is measured, not copied.** scipy's default is
`n_fft = 2^ceil(log2(2·(N−1)/0.01))` — about 200× the filter length, which
the docs describe only as giving "reasonable results". For `N = 65536` that
is `2^24 = 16.8 M` points: 128 MB per complex-float buffer through a float32
`Fft`. This record does **not** hard-code 0.01. `gen_fir.py` (double
precision) sweeps `n_fft/N ∈ {4, 8, 16, 32, 64, 128, 256}` on the §7 fixtures
and records the magnitude-identity residual against each; the smallest factor
whose residual is below the float32 floor of §7 is the one the C++ uses, and
the table goes into this file as an amendment. The mechanism being controlled
is cepstral aliasing — `c[n]` of a zero near the unit circle decays like
`1/n`, and the −120 dB floor bounds how near the circle a zero can sit, which
is why a finite factor works at all.

**Verification that does not depend on the derivation being right.** Before
truncation, `|FFT(h_min, n_fft)| == |FFT(h_lin, n_fft)|` bin for bin — a
closed-form identity, float32-shaped (§7, item 3). Truncation to `N` taps
then loses a stated energy fraction `Σ_{n≥N} h_min[n]² / Σ h_min[n]²`, which
the result carries as a number (§6) rather than pretending the identity
survives truncation exactly.

## 5. Decision: the sample rate is written in three places on purpose, the text file is self-describing, the WAV is 32-bit float only, and the gain convention is stated, never implied

**Decision.** `app/` writes two formats from one `core/` result:

1. **Text, one coefficient per line, preceded by a `#` header** in the
   `key=value` line convention `SessionCodec` already uses:
   `sample_rate_hz`, `taps`, `phase` (`linear`|`minimum`), `method`
   (`frequency_sampling`|`least_squares`), `window`, `group_delay_samples`
   (linear only), `normalization` (`as_designed`|`peak_0dbfs`),
   `applied_trim_db`, `peak_gain_db` (max `20·log10|H|`), `coefficient_peak`
   (max `|h[n]|`), `truncation_loss_db` (minimum only), `generator`,
   `generated_utc`, and a final `# --- coefficients follow ---` line. A
   `--bare` option that drops the header is **refused**: it recreates the one
   bug this format exists to prevent.
2. **WAV, mono, 32-bit IEEE float**, with the true sample rate in the header.
   Never an integer WAV: a boost puts `|h[n]| > 1`, which float carries and
   int clips. REW's own export dialog says the same — "32-bit Float is
   recommended … particularly if the response is not normalised" (verified).

**The filename carries the facts too**:
`<name>_<fs>Hz_<N>taps_<lin|min>.{txt,wav}`. Three places — filename, header,
WAV field — because station 1's survey found that no convention puts the
sample rate in more than one place, and this session found the sharpest
consequence: **CamillaDSP ignores the WAV sample-rate field** (§1). A
mismatch will not be caught by the convolver; it will be caught by a human
reading a filename, or not at all. Redundancy is the design, not sloppiness.

**Compatibility of the header, verified where it could be.** CamillaDSP's
`Raw` type with `format: TEXT` reads "a simple text file with one value per
row" and has `skip_bytes_lines` — "Number of bytes (for raw files) or lines
(for text) to skip at the beginning of the file" — so the header is skipped
by giving its line count, which the header keeps **fixed** for that reason.
Whether rePhase's or REW's text *importers* tolerate `#` lines was **not
verified** (rephase.org and the miniDSP rePhase page returned 403; REW's help
lists TXT import for *measurements*, not for filter coefficients). A tool
that cannot skip lines takes the WAV.

**Normalisation.** Default `as_designed`: the coefficients realise the
correction at the gain it was designed at, and the header states
`peak_gain_db` and `coefficient_peak` so the operator knows the headroom the
convolver must have. Optional `peak_0dbfs`: the taps are scaled so
`max|h[n]| = 1`, and the header states the `applied_trim_db` that undoes it.
Peak-normalising *silently* — which REW offers as a checkbox (verified: "to
select whether or not to normalise the response so that the peak value is
unity (0 dBFS)") — changes the level by the largest boost and tells nobody;
here it is a stated choice with its number attached.

**Argument against the obvious ("just write what rePhase writes").** rePhase
and REW export headerless data whose meaning lives in the operator's memory
and the DSP's config screen (REW's filter export is a fixed 131 072 samples
with the rate chosen in a dialog — verified — and nothing in the file says
which). That works inside one person's workflow and fails the moment a file
is handed on. The hardware constraint the report recalled is real in outline:
miniDSP forum posts (the product page returned 403) put the OpenDRC at 6144
taps per channel at 48 kHz and half that at 96 kHz. A file that does not say
its own rate and length cannot even be checked against such a budget.

## 6. Decision: the core/app boundary is `FirDesign::Result` in, two thin writers out

**Decision.** `rta::dsp::FirDesign` in `core/include/rta/dsp/` and
`core/src/dsp/`, on the `ButterworthDesign::Result` precedent
(`ButterworthDesign.h:28-34`): a static design function takes the target as
`(frequencyHz, dB)` breakpoints (interpolated **linearly in log-frequency and
linearly in dB** onto the design grid — the rule a banded curve implies, and
a closed-form-testable one), `sampleRate`, `taps`, `PhaseType`, `WindowType`,
and returns

```
struct Result {
    std::vector<float> taps;           // as designed; normalisation is the writer's job
    double   sampleRate;
    PhaseType phase; Method method; WindowType window;
    std::size_t groupDelaySamples;     // (N-1)/2 for linear; 0 reported for minimum
    double   peakGainDb, coefficientPeak;
    std::optional<double> truncationLossDb;   // minimum phase only
    std::size_t designFftSize;         // M, or n_fft for minimum phase
};
```

Core never writes a file, never sees a filename. `app/` gets two writers,
text and WAV, beside `app/src/trace/SessionCodec*` where persistence already
lives. The WAV writer uses JUCE's `WavAudioFormat`: `app/` links
`juce::juce_audio_utils`, whose module header declares `dependencies:
juce_audio_processors, juce_audio_formats, juce_audio_devices` (read from the
JUCE 9.0.1 checkout at `PROJECT005-AZ-handsfree/external/JUCE`), so no new
module is added. `core_has_no_framework_deps` is untouched; the FFT-heavy
minimum-phase path stays in `core/` as pure arithmetic on spans.

**Precision split.** Everything that goes through `RealFft`/`Fft` is float32
by the primitives' nature (`memory/float32-fft-precision.md`). Everything that
does not — grid interpolation, window sums, normalisation scalars, the future
least-squares solve — is done in `double` and cast to float at the end. The
tolerances in §7 follow that split, one shape per path.

## 7. How CI proves this with no sound card: identities first, a golden last, float32-aware in two shapes

Core tests in `core/tests/test_fir_design.cpp`; golden
`core/tests/golden/fir.txt` from `tools/gen_fir.py` (argparse, `--out`,
`--check`, `--help` exits before any write — copying `gen_mtw.py:165-172`,
not the seven generators that run on import).

**Shape A — FFT-derived quantities** (the two-term form of
`memory/float32-fft-precision.md`, whose 1e-7 peak term was measured on a
1024-point transform): `tol_k = 1e-6·|X_k| + c_M·peak`, with `c_M = 2e-7` for
`M ≤ 2^20` as the starting point. The builder **prints the measured residual
beside the tolerance** in the test output and tightens `c_M` if the fixture
reads an order of magnitude under it — a fixture that cannot fail is not a
test (`memory/a-fixture-can-be-too-well-behaved-to-fail.md`).

**Shape B — solve-derived quantities** (least-squares, when built): both the
C++ solve and `firls` run in double, so the disagreement is conditioning, not
transform precision: `|tap_core[n] − tap_golden[n]| ≤ cond(Q) · 1e-15 ·
max|tap_golden| + 1e-12`, where `cond(Q)` is **computed by `gen_fir.py` and
written into the golden as a field**. The report's `atol 1e-6 / rtol 1e-4`
were placeholders and are not adopted; a double-vs-double comparison that
needs 1e-4 is hiding a bug.

1. **Round trip.** The `M`-point IDFT of the sampled target, forward-FFT'd
   again before windowing, reproduces the target bin for bin (Shape A). This
   is `forward(inverse(X)) == X` and proves the sampling step, not the design.
2. **Linear-phase symmetry, then phase.** `taps[n] == taps[N−1−n]` bitwise
   (construction). Then `Im( H(e^{jω_k}) · e^{+jω_k(N−1)/2} )` is within
   Shape A of zero at every bin — the algebraic consequence of Type I/II
   symmetry, independent of the tap values. Run for one odd and one even `N`.
3. **Magnitude identity.** `|FFT(h_min, n_fft)|` equals `|FFT(h_lin, n_fft)|`
   within Shape A **before truncation**; after truncation to `N`,
   `truncationLossDb` is reported and asserted only against the value the
   golden records for that fixture (a regression lock, labelled as such).
4. **Trivial targets, closed-form.** Flat 0 dB: linear phase gives a delta at
   the centre tap (`1.0 ± 1e-5`, all others `≤ 1e-5`), minimum phase gives a
   delta at tap 0. Flat +6.0206 dB: the same with amplitude 2. `peakGainDb`
   and `coefficientPeak` read those values.
5. **Grid interpolation.** Breakpoints `(100 Hz, 0 dB), (1000 Hz, +6 dB)`
   interpolate to `+3 dB` at `316.2 Hz` (`√10 · 100`), exactly, in double.
6. **Floor.** A target with a −200 dB notch produces a minimum-phase result
   with no NaN and no Inf, and its magnitude at the notch bin reads
   `kMagnitudeFloorDb` within Shape A.
7. **Golden.** `gen_fir.py` produces, for a boost-and-cut target at `N = 1023`
   and `N = 4095`: the frequency-sampling taps (`numpy.fft.irfft` +
   `scipy.signal.get_window(..., fftbins=True)`), the minimum-phase taps
   (`scipy.signal.minimum_phase(h, method='homomorphic', half=False,
   n_fft=<the §4 factor>)`), and the oversampling-sweep table. Comparison to
   C++ is Shape A on both (both paths are FFTs; the log/exp pair maps a
   relative magnitude error to the same relative error, so the shape holds).
   When least-squares is built: `firls` taps plus `cond(Q)`, compared with
   Shape B, on one well-conditioned and one deliberately ill-conditioned
   (narrow steep boost) fixture so the fallback path runs.
8. **Guards.** `core_has_no_framework_deps` stays green with the new core
   files; `filter_design_has_no_polynomial_form` stays green with
   `gen_fir.py` scanned — see §8 for why that needs care.

**The two generator asymmetries `docs/HANDOFF.md` recorded apply here with
force.** The Sweep golden survived a fade-clamp and a peak-amplitude
mismatch between Python and C++ only because its consumed field was a ratio.
**FIR taps are not scale-invariant** — a tap value is an absolute number —
so every convention must be pinned on both sides and printed in the golden:
the window form (scipy's `get_window` default `fftbins=True` is periodic,
matching `Window`; verified from the docs this session — `fftbins=False`
"for use in filter design" is the trap), the IDFT normalisation (`irfft`
includes `1/N`, as `RealFft::inverse` does), the circular-shift convention,
the floor, `half=False`, and `normalization=as_designed`.

## 8. The polynomial-form guard: what it means for FIR, and how `gen_fir.py` avoids tripping it

`core/tests/check_no_polynomial_form.cmake` forbids `zpk2tf`, `tf2sos`,
`np.poly(`, `signal.lfilter`, `freqz(b`, `output='ba'` under `core/` and
`tools/*.py`, because an IIR polynomial's denominator can have a root outside
the unit circle (`docs/dsp/2026-08-27-filterbank.md`: `7.4e+130`). **An FIR
has no denominator** (`a = [1]`), so the property the guard protects cannot be
violated by a tap vector; the guard's existence does not mean "core holds no
coefficient vectors". Record that here so it is not re-litigated.

The guard is a string match, not a semantic one (its own comment says so,
lines 23-26). Practical rules for `gen_fir.py`, each traced to a pattern:

- Do not name the tap array `b` — scipy's docs use `b` everywhere, and
  `freqz(b` is a pattern. Name it `taps`; `freqz(taps, 1, …)` is fine.
- Apply an FIR with `numpy.convolve`, never `scipy.signal.lfilter` —
  `signal\.lfilter` matches regardless of what `a` is.
- Do not call `np.poly(` to build anything; roots-to-polynomial is the
  operation the guard is about, even when harmless here.
- `firls`, `minimum_phase`, `get_window`, `irfft` match nothing.

## 9. Tap count and the honest limit, restated as two exact numbers

The operator sees `fs/N` (resolution, Hz) and `(N−1)/(2·fs)` (latency, ms,
linear phase only) computed from their inputs — both exact, neither a
"quality" slider. A tap-count ceiling for a specific convolver is a third
number the operator supplies; this record does not choose a default (§11),
and the export never silently truncates to fit one.

## 10. What v1 refuses

- A `--bare` text export (§5).
- An integer-format WAV (§5).
- A tap count that does not fit the design grid (`N > M/2` for frequency
  sampling: the window would be wider than the circularly shifted response
  is long) — `std::invalid_argument`, the `ButterworthDesign` convention.
- A target with a non-monotonic frequency axis, or a sample rate ≤ 0.

## 11. What this record does not decide

- Building weighted least-squares — decided in principle (§2), gated on the
  LF-smear measurement, whose threshold (1 dB) is a proposal.
- The cepstral oversampling factor — measured by `gen_fir.py`, amended here.
- A mixed / excess-phase blend (rePhase's third mode).
- A default tap-count ceiling or any convolver-specific profile.
- Whether the upstream auto-EQ hands over breakpoints or a per-bin curve;
  §6 accepts breakpoints and the auto-EQ record must either produce them or
  amend §6.
- A double-precision FFT primitive in `core/` — the 16.8 M-point float32
  transform of §4 is the case that would motivate one; not proposed here.
- Whether rePhase/REW text importers accept `#` header lines (unverified).

## 12. Verification ledger — every external claim, and how it was checked

| Claim | Status | How |
|---|---|---|
| `firls` is odd-`N`, Type I, weighted integrated squared error | **Verified** | scipy docs, read this session |
| `firls` normal equations are Toeplitz **+ Hankel**, solved by Cholesky (`assume_a="pos"`), `lstsq(gelsy)` fallback on ill-conditioning | **Verified** | scipy `_fir_filter_design.py` at `main`, raw source read |
| `firls` follows Selesnick 2005 (OpenStax CNX) | **Verified** | scipy docs reference [1] |
| Station 1's "Toeplitz / Levinson" premise | **Refuted** | same source (§1) |
| `minimum_phase`: homomorphic default; `n_fft = 2^ceil(log2(2(N−1)/0.01))`; fold window `[1,2,…,2,1+(n_fft%2)]`; `half=True` halves the log; floor `1e-7·min(A>0)`; symmetry warning | **Verified** | scipy source + docs |
| Station 1's "same magnitude as linear sibling" | **Verified only for `half=False`** | same source (§4) |
| `remez`: piecewise-constant `desired`, `maxiter` default 25 | **Verified** | scipy docs |
| Remez "unreliable convergence at high order" | **UNVERIFIED** — not in docs; issue not found; not load-bearing | — |
| `get_window` default `fftbins=True` = periodic | **Verified** | scipy docs |
| CamillaDSP Conv: `Raw`/`Wav`/`Values`/`Dummy`; `format: TEXT` one value per row; `skip_bytes_lines`; **"The sample rate of the file is ignored."** | **Verified** | CamillaDSP `README.md` at `master`, raw |
| REW: "Export filters impulse response as WAV" — 131 072 samples, rate/format chosen in dialog, normalise-to-0 dBFS option, 32-bit float recommended when not normalised; IR export offers measured / EQ'd / minimum-phase | **Verified** | roomeqwizard.com help, File menu |
| REW has a *text* export of filter coefficients | **Not found** in the File-menu help; treat as unverified | — |
| rePhase: both linear- and minimum-phase filters; multiple windows; iterative optimisation; 32-bit IEEE-754 WAV; centering options | **Partially verified** | rephase.org front page (read) and changelog (via search summary); tutorial PDF and miniDSP page not readable (403 / no renderer) |
| rePhase's method is IFFT + window | **UNVERIFIED** — the site names windows and optimisation, not the transform | — |
| miniDSP OpenDRC: 6144 taps/channel at 48 kHz, half at 96 kHz | **Verified against forum posts only** (product page 403) | minidsp.com community threads |
| Pre-ringing audibility threshold (Blauert & Laws) | **UNVERIFIED, dropped** — not cited anywhere above | — |
| Oppenheim & Schafer derivations (Type I/II symmetry, cepstrum) | Book not opened; the symmetry claim is algebra shown in §7, the cepstrum is verified through the scipy source | — |
| JUCE `juce_audio_utils` depends on `juce_audio_formats` | **Verified** | `juce_audio_utils.h:54` in the 9.0.1 checkout |
| Repo facts in §1 | **Verified** | files read and grep'd this session, `4b05049` |

## Sources

scipy `scipy/signal/_fir_filter_design.py` at `main` (raw, 2026-09-06);
scipy reference pages `signal.firls`, `signal.minimum_phase`, `signal.remez`,
`signal.get_window`; Selesnick, *Linear-Phase FIR Filter Design by Least
Squares*, OpenStax CNX 2005 (as cited by scipy; not opened). HEnquist
`camilladsp/README.md` at `master` (Conv filter section). REW help,
`file.html` (File menu). rephase.org front page and changelog. miniDSP
community forum threads "FIR taps allocation", "OpenDRC-DI @ 96 kHz". JUCE
9.0.1 `modules/juce_audio_utils/juce_audio_utils.h`. This repo:
`docs/research/2026-09-06-l7-fir-export-station1-research.md`;
`core/include/rta/dsp/{RealFft,Fft,Window,ButterworthDesign,TransferEstimator}.h`;
`core/tests/{test_fft.cpp,check_no_polynomial_form.cmake,support/Golden.h}`;
`tools/gen_mtw.py`; `docs/HANDOFF.md` (generator asymmetries);
`memory/float32-fft-precision.md`,
`memory/a-fixture-can-be-too-well-behaved-to-fail.md`,
`memory/a-gen-script-runs-the-moment-you-invoke-it.md`,
`memory/a-threshold-read-off-a-grid-is-that-grids-floor.md`,
`memory/the-parity-table-is-a-hypothesis.md`.
