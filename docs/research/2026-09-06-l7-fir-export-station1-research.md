# FIR export (G10) -- station-1 research, lane L7-FIR

*2026-09-06. Read-only research pass on HEAD before any L7-FIR code. Scope:
exporting a designed correction (from a future auto-EQ or a target curve) as
FIR coefficients a downstream convolver can load. No playback needed, which is
why this sub-lane of L7 was picked to go first. This document is the evidence
the eventual `docs/dsp/` decision record must cite -- write that record
separately; do not let this file become it.*

Sources consulted, all from memory and **not re-read from source in this
session** (the project venv with numpy/scipy lives in the main checkout only,
per `memory/build-toolchain-on-this-machine.md`, and this worktree has no
Python environment; no internet fetch was used): Oppenheim & Schafer,
*Discrete-Time Signal Processing* (FIR design chapters); Parks & McClellan
1972 and McClellan-Parks-Rabiner 1973 (the Remez-exchange papers);
`scipy.signal.firls`, `.remez`, `.minimum_phase`, `.freqz` behaviour; rePhase
and REW public documentation; DIY-convolution community practice (CamillaDSP
config docs, miniDSP/ADAU-class hardware tap limits). **Everything below not
visible in this repo's own files is marked UNVERIFIED or "from memory" and
must be re-checked against a live scipy/rePhase/REW instance before being
written into a decision record as fact**, matching how
`2026-09-05-mtw-station1-research.md` and `2026-09-06-l6b-station1-research.md`
flag their own unopened sources.

## What this repo already has (read from the actual files)

- `core/include/rta/dsp/RealFft.h`: forward and **inverse** real FFT already
  exist, promoted through `std::complex<float>`, exact inverse of forward
  (`RealFft.h:40-47`). A frequency-sampling FIR design needs nothing new here.
- `core/include/rta/dsp/Window.h`: periodic Hann/Hamming/BlackmanHarris/
  FlatTop/Tukey, with `amplitudeCorrection()` / `energyCorrection()` /
  `equivalentNoiseBandwidth()` already derived and asserted to 1e-12 per the
  class's own doc comment. Any apodization the FIR design needs reuses this
  class rather than inventing a second window enum.
- `core/include/rta/dsp/ButterworthDesign.h` + `core/tests/check_no_polynomial_form.cmake`:
  the repo already refuses transfer-function ("ba") polynomial form for
  **IIR** design, because `docs/dsp/2026-08-27-filterbank.md` measured a
  lowest-band Butterworth polynomial's impulse response diverging to
  `7.4e+130` in double precision from a root outside the unit circle. **This
  rationale is IIR-specific and does not transfer to FIR as written** -- see
  "Core/app boundary and the polynomial-form guard" below, it is still a real
  trap for the tooling.
- `TransferSnapshot::magnitudeDb` floors at -120 dB (`core/include/rta/dsp/TransferEstimator.h:60-71`,
  per the L6b research read of that struct). A minimum-phase cepstral
  computation needs exactly this kind of floor to keep `log|H|` finite -- see
  below. Reusing -120 dB rather than inventing a second floor constant is a
  free consistency win.
- `memory/float32-fft-precision.md`: `RealFft` is single precision throughout;
  ~1.3e-6 relative error on a 1024-point transform, and the **error floor is
  set by the largest bin, not the bin under test** -- `tolerance = binValue *
  1e-6 + peak * 1e-7`. Every tolerance below is shaped from this, not
  invented fresh.
- No linear-algebra library is linked into `rta_core` (`core/CMakeLists.txt`
  lists only this repo's own `.cpp` files, no Eigen, no LAPACK). A
  least-squares FIR design that needs to solve a Toeplitz normal-equations
  system has to bring its own solver (Levinson-Durbin-shaped recursion is the
  standard one) -- there is no precedent for this in the repo today, unlike
  the FFT and window primitives above.
- `core/tests/golden/` is flat, `tools/gen_*.py` convention: seven of eight
  existing generators have **no argparse** and run their whole pipeline the
  moment they are invoked, including `--help`
  (`memory/a-gen-script-runs-the-moment-you-invoke-it.md`); only
  `tools/gen_mtw.py` is safe to probe. A `tools/gen_fir.py` must copy
  `gen_mtw.py`'s argparse shape, not the other seven.

## Candidate FIR design methods

The instinctive first answer to "make an FIR from a target curve" is: sample
the target on a dense frequency grid, inverse-FFT it, window the result down
to N taps. That is frequency sampling. It is not wrong, but treating it as
the *only* or the *final* method undersells what a correction filter needs.

### Candidate 1 -- frequency sampling / windowed inverse-FFT (the obvious one)

Sample the desired complex response `H_d[k]` on a dense grid (`M` points,
`M` a power of two, `M >> N` taps wanted), run `RealFft::inverse` to get an
`M`-length impulse response, then window and truncate to the centre `N`
samples (this is what rePhase's documented workflow and REW's "Generate
filter" step both do, from memory of using both tools -- **not re-read this
session**, flag as informed-but-unverified).

**Argument against making this the only method.** The method guarantees the
result passes through the sampled points at the FFT bin frequencies (an
exact DFT/IDFT round-trip -- see the golden strategy below, this identity is
free) but it does not minimize any error norm *between* those points, and it
gives the designer no way to say "get low frequencies right even if it costs
accuracy above 5 kHz." The only knob is the window shape, which shapes the
impulse response uniformly in time, not selectively in frequency. This is a
real cost specifically at low frequencies: a fixed tap budget resolves LF
worst (see "tap count vs LF resolution" below), which is exactly where a
narrow, mis-corrected peak is most audible per-Hz. Frequency sampling has no
mechanism to spend the tap budget where it is needed most.

**Argument for keeping it anyway, as the first-shippable mode.** It is built
entirely from primitives this repo already has and has already tested to
1e-12 (`RealFft`, `Window`). No new numerical solver, no new dependency, no
new failure mode beyond what those two classes already carry. For a smooth,
already-smoothed target curve (this project's traces are already 1/N-octave
banded before a human ever asks for a correction -- see
`core/include/rta/dsp/OctaveBands.h`, `BandWeights.h`) the lack of
frequency-selective error weighting may simply not matter much in practice.
It is the right MVP, not the right permanent default.

### Candidate 2 -- weighted least-squares (scipy.signal.firls shape)

Minimize `∫ W(f) |H_d(f) - H(f)|² df` over the design band(s), for a linear
FIR of a given tap count. For evenly/arbitrarily weighted bands this reduces
to a linear system with **Toeplitz structure** (the normal equations of a
convolution), solvable in closed form without iteration -- classically via a
Levinson-type recursion, `O(N^2)` for `N` taps, not `O(N^3)` naive Gaussian
elimination. `scipy.signal.firls` is described (from memory, not re-read)
as solving exactly this, restricted to odd-length symmetric (Type I)
linear-phase filters.

**Why this is the right primary method for a correction curve**: `W(f)` is
precisely the knob Candidate 1 lacks. A strong candidate weight already
exists in this project's own vocabulary -- de-weight or hard-limit above the
room's transition frequency (a single mic's HF correction is not reliable
past it; L6b's spatial-averaging research thread found no product claims
otherwise) and weight heavily below it. The solve is deterministic (no
iteration to fail to converge) and a single linear system, not a per-band
spec the operator hand-authors.

**Cost of choosing it**: nothing in `rta_core` solves a Toeplitz system
today. This is genuinely new numerical code -- a Levinson-Durbin-shaped
recursion whose conditioning degrades as the desired response gets more
extreme (a very steep, very narrow boost pushes toward ill-conditioning,
whose failure mode is large spurious taps, not a clean error). Real cost
Candidate 1 does not have; budget it as such, not as free because "scipy has
a one-line function for it."

### Candidate 3 -- Parks-McClellan / Remez exchange (scipy.signal.remez shape)

Equiripple (minimax/Chebyshev) design: minimizes the *maximum* weighted error
rather than the integrated squared error, over a set of piecewise-constant
desired bands (Parks & McClellan 1972; McClellan-Parks-Rabiner 1973 gave the
practical exchange algorithm -- from memory, page/equation numbers not
re-verified).

**Argument against it here, specifically**: the classical formulation wants a
small number of piecewise-constant/linear bands with sharp edges -- the shape
of a crossover spec (**G18**, a sibling item in this L7 row,
`docs/plans/MASTER-EXECUTION-PLAN.md:125`), not a measured, continuous,
many-featured correction curve. Coercing a correction curve into "N
piecewise bands" either loses resolution (few wide bands) or hands the
exchange algorithm hundreds of narrow bands, reported (from memory of scipy
issue-tracker discussion -- UNVERIFIED) as where Remez convergence gets
unreliable at high order. Equiripple error is also the wrong shape
perceptually: equal ripple in linear frequency does not track a
per-critical-band hearing model, whereas a least-squares weight can be
handed a log-spaced `W(f)` directly. **Recommendation: park Remez for G18,
do not build it for G10.**

### Decision leaning (for the eventual `docs/dsp/` record, not decided here)

| Question | Leaning | Evidence |
|---|---|---|
| Primary design method | Weighted least-squares (Candidate 2) | frequency-selective weighting is what a correction curve needs; no L6b-surveyed product (Smaart/REW/SysTune/OSM) publishes a region-weighted FIR design, so `W(f)` from the room's transition frequency is this project's own choice, not consensus |
| Fallback / MVP method | Frequency sampling (Candidate 1) | zero new numerical code, reuses `RealFft` + `Window` already tested to 1e-12; ships before a Toeplitz solver exists |
| Remez / Parks-McClellan | Rejected for G10, kept for G18 | wrong problem shape (piecewise bands vs continuous curve); convergence risk at high order (UNVERIFIED, from memory) |
| Phase | Offer both, no single default | see next section -- different problems, not degrees of one problem |
| Export format | Self-describing text header (SR, gain convention) + optional 32-bit float WAV | headerless taps are the single most common real-world FIR bug (SR mismatch); WAV carries SR for free |
| Core/app split | Core returns taps + metadata struct; app writes the file | matches `ButterworthDesign::Result`; core never writes a file anywhere in this repo (`SessionCodec` persistence lives in `app/`) |

## Linear-phase vs minimum-phase: not "pick one," two different corrections

**Linear phase**: taps symmetric (or antisymmetric) about the centre tap, so
phase is an exactly linear function of frequency -- constant group delay
`(N-1)/(2*fs)` at every frequency, by construction, independent of the tap
*values* (Type I/II FIR symmetry, Oppenheim & Schafer, from memory; this is
also the closed-form identity proposed as a golden-free test below). No
frequency is delayed more than any other, which is what "zero added phase
distortion" means here -- it does not mean the phase is zero or flat. Cost:
the group delay is the
whole filter length, and every impulse-response feature -- including the
correction itself -- necessarily has energy *before* the nominal arrival
time (pre-ringing), because a symmetric impulse response is symmetric around
its centre. Pre-ringing is audible, and the audibility threshold (a
pre-masking window on the order of a few milliseconds, in the literature I
recall as Blauert & Laws-adjacent work -- **UNVERIFIED, not re-read this
session**) interacts badly with the same low-frequency correction that wants
thousands of taps: more taps for LF resolution directly means more
pre-ringing time, not just more latency.

**Minimum phase**: for a *given magnitude response*, the unique causal,
stable filter whose energy is most front-loaded in time (all zeros on or
inside the unit circle). No pre-ringing, and latency is only whatever group
delay the magnitude shape itself implies (a boost has more delay than a flat
band, but nothing like `(N-1)/2`). Cost: the phase is *derived from the
magnitude*, not chosen -- it does not correct any phase error actually
present in the room, only whatever phase a minimum-phase system with that
same magnitude would have. Two systems with identical magnitude and
different actual (measured) phase get the *same* minimum-phase correction
filter, which is a real and known limitation, not an implementation bug.

**Neither is strictly "the room correction" answer.** REW and rePhase (from
memory of using both) offer both, plus a mixed/"excess phase" blend as a
third option in rePhase specifically. This project should do the same: **two
phase modes, not a default that hides the other.** The trade-off is latency
and pre-ringing vs leaving the room's actual phase error uncorrected, and
that is an operator decision (studio/offline system tolerates linear-phase
latency; live sound cannot), not a DSP-quality one this codebase should
silently make.

### Deriving and verifying minimum phase

Homomorphic (cepstral) method, from memory of the standard derivation
(Oppenheim & Schafer; matches the documented shape of
`scipy.signal.minimum_phase(method='homomorphic')`):

1. `X[k] = FFT(taps)`, real cepstrum `c[n] = IFFT(log|X[k]|)`.
2. **Fold**: `c_min[0] = c[0]`, `c_min[n] = 2*c[n]` for `1 <= n < M/2`,
   `c_min[M/2] = c[M/2]` if `M` even, `c_min[n] = 0` for `n > M/2`. This is
   the step that turns a zero-phase magnitude-only cepstrum into the
   minimum-phase system's complex cepstrum -- it is the entire trick, and it
   is exact, not an approximation, given enough FFT length.
3. Complex cepstrum -> `exp` -> `FFT` -> take the real part / inverse-transform
   back to get the minimum-phase impulse response.

Two correctness requirements, both already answerable from this repo's own
conventions:

- `log|X[k]|` is undefined at an exact zero. The obvious fix is the floor
  this codebase already uses for exactly this reason:
  `TransferSnapshot::magnitudeDb` floors at **-120 dB**
  (`core/include/rta/dsp/TransferEstimator.h`). Reuse that constant rather
  than inventing a second floor value with a different number attached to
  it -- a divergence between "the floor everywhere else" and "the floor in
  the one place that needs it for numerical reasons" is exactly the kind of
  drift `memory/` exists to prevent.
- The cepstrum is, in principle, infinite; truncating it to a finite FFT
  length aliases it. `scipy.signal.minimum_phase` is documented (from
  memory, not re-read) to pad the working FFT to several times the tap
  count specifically to keep this aliasing below its resolution -- the
  exact oversampling factor is UNVERIFIED and should be re-derived or
  re-read from source before being hard-coded here, not copied from memory
  into a decision record as a cited number.

**Verification that does not depend on any of the above being right**: a
minimum-phase filter and its linear-phase sibling, by definition, share the
*same magnitude response*. `|RealFft::forward(h_min)| == |RealFft::forward(h_linear)|`
is a closed-form identity independent of which of the two derivation methods
above produced `h_min` -- it is the cheapest and strongest test available,
and it is float32-tolerance-shaped rather than "whatever the code printed"
(see golden strategy below).

## Export format: what a downstream convolver actually needs

Surveyed conventions (rePhase, REW, and DIY-convolution practice --
CamillaDSP's config docs and the general shape of hardware convolvers like
miniDSP's tap-limited DSPs; **all from memory of using these tools and
reading their public docs, not re-fetched this session, flag as
UNVERIFIED-in-detail**):

- **Raw ASCII text, one coefficient per line, no header.** rePhase's and
  REW's plainest export. Nothing in the file states sample rate, tap count
  (implicit in line count), normalization, or phase type -- meaningless
  without an out-of-band agreement, usually the filename.
- **32-bit float WAV as an impulse response.** Carries sample rate in the
  WAV header for free -- the one format surveyed where a mismatch is
  detectable, not silent. Accepted by any general-purpose convolution engine
  because it is just audio.
- **CamillaDSP-style config**: a text or WAV coefficient file referenced from
  a YAML config that states its *own* `sample_rate` field, separate from the
  file. The sharpest example of the real disagreement: **the file and the
  pipeline can each believe a different sample rate, and nothing anywhere
  checks.** A raw-text export inherits exactly this risk.
- **Hardware DSPs** (miniDSP-class, from general product-category knowledge,
  not one manual re-read): a hard tap-count ceiling tied to processing
  budget, sometimes halved at higher sample rates, sometimes fixed-point.
  A correction designed at 65536 taps for full LF resolution may simply not
  load on a chosen convolver at all.

**What this means for the export this repo should write**: never emit bare
taps with an implicit sample rate. Concretely -- a self-describing text
format with a small `#`-prefixed header stating sample rate, tap count,
phase type, design method, and the normalization convention in force (see
next paragraph), plus a 32-bit float WAV variant for tools that only accept
audio files and would otherwise need the header stripped by hand. Both are
cheap to emit from the same core-returned struct; this is an app/-layer
decision (two file writers, not two designs), matching the core/app split in
the table above.

**Normalization must be stated, not implied.** A correction filter is not
guaranteed unity gain -- if it contains a boost, applying it to full-scale
program material clips unless headroom was reserved. The export should state
which convention it used (unity gain at DC, unity peak amplitude, or the
correction's actual computed gain with a stated trim in dB) rather than
leaving the operator to infer it by playing test material and listening for
clipping.

## Tap count vs low-frequency resolution: the honest limit

A FIR's frequency resolution is the same quantity as an FFT's bin width:
roughly `fs / N` for `N` taps at sample rate `fs` -- this is not a
rule-of-thumb borrowed from a forum, it is the same math this repo already
has in `Window::equivalentNoiseBandwidth()` and the MTW station-1 record's
`PSD = PS / ENBW, ENBW ∝ 1/N` identity (Heinzel/Rüdiger/Schilling, cited in
`docs/research/2026-09-05-mtw-station1-research.md`). Correcting a narrow
feature near 20-40 Hz meaningfully wants `N` in the low thousands to tens of
thousands at 48 kHz -- the DIY-community rule of thumb (from memory,
UNVERIFIED) is roughly `N ≈ 1000 * fs_kHz / target_resolution_Hz`, which
lands in the same range independently.

**The cost is not abstract**: for linear phase, latency is `(N-1)/(2*fs)`.
65536 taps at 48 kHz is **≈ 683 ms** one-way -- unusable in a live-sound
context (this project's own real-time-safety rule in `CLAUDE.md` exists
precisely because dropouts "at a live show" are the failure this tool is
built to prevent; a 683 ms fixed added latency is a different failure of the
same kind of care). For minimum phase the same tap count buys the LF
resolution without that latency, but only because the pre-ringing that
linear phase would have spent that time on is simply not there to correct
with -- it is not a free lunch, it is the trade-off from the phase section
above surfacing again in tap-count terms.

**How to state this honestly to the operator**: expose the resolution/
latency trade-off as two numbers computed directly from `N` and `fs` (no
approximation needed, both are exact), not as a single opaque "quality"
slider, and let the phase-type choice change what the second number means
(latency for linear phase, "no meaningful latency cost, but see phase
section" for minimum phase). A tap-count ceiling tied to a target convolver's
documented limit (see export section) is a second, independent number the
operator needs, not a DSP concern this project can solve by choosing a
default.

## Golden-vector strategy, float32-aware

In order of preference, matching `CLAUDE.md`'s verification standard:

1. **Closed-form identities, no golden file needed:**
   - *DFT/IDFT round-trip*: for the frequency-sampling method, the taps'
     forward transform at the exact sample-grid frequencies must reproduce
     the sampled target exactly (Parseval / DFT-basis orthogonality -- this
     is provable, not measured). Tolerance: the two-term shape from
     `memory/float32-fft-precision.md`, `binValue * 1e-6 + peak * 1e-7`,
     since it runs through the same single-precision `RealFft`.
   - *Linear-phase symmetry implies exact linear phase*: for ANY set of
     taps symmetric about the centre tap, `phase(e^{jω}) = -ω*(N-1)/2` (mod
     2π) is an algebraic identity independent of the tap *values* -- this
     tests the phase-type implementation itself, not a specific design
     result, and needs no reference implementation at all.
   - *Minimum/linear-phase magnitude equality*: `|H_min(e^{jω})| ==
     |H_linear(e^{jω})|` for the same target, using the same two-term
     tolerance (both paths go through `RealFft`).
2. **A published identity for a trivial target**: an all-pass (flat 0 dB,
   zero phase) target's least-squares or frequency-sampling solution is a
   single delta (at sample 0 for zero-phase, or a delayed delta at the
   centre tap for linear-phase) -- assert this to a tolerance an order of
   magnitude above `RealFft`'s ~1.3e-6 relative-error floor, not to
   `epsilon(1e-9)` (which the dual-FFT and transfer-estimator tests already
   found unattainable against *correct* code, per
   `memory/float32-fft-precision.md`).
3. **A committed golden vector via `tools/gen_fir.py`** (argparse'd, copying
   `gen_mtw.py`'s shape -- not the other seven generators): a small, named
   least-squares test case (e.g. `N = 63` taps, a simple boost-and-cut
   target with a stated weight function) computed by `scipy.signal.firls`
   at generation time, committed as taps under `core/tests/golden/`, and
   compared against this repo's own Toeplitz solver. **Tolerance needs its
   own shape here, not a copy of the FFT one**: this is comparing two
   *independently implemented numerical solves* of the same linear system,
   not a filter passing through a fixed transform, so the error is
   solve-conditioning-dependent rather than FFT-precision-dependent.
   Recommended shape: `abs(tap_core[n] - tap_golden[n]) <= atol + rtol *
   max(|tap_golden|)`, with `atol` around `1e-6` and `rtol` around `1e-4`
   as a starting point (both **UNVERIFIED numbers** -- they should be
   measured against the actual conditioning of a real Levinson-Durbin
   implementation on a real target curve before being written into a test,
   not guessed once and frozen; a fixture that happens to be extremely
   well-conditioned can read a misleadingly tiny residual and never
   exercise the tolerance at all -- see
   `memory/a-fixture-can-be-too-well-behaved-to-fail.md`).

## Core/app boundary and the polynomial-form guard

Core returns coefficients as data: a `Result`-shaped struct on the
`ButterworthDesign::Result` precedent -- taps (`std::vector<float>` or
`std::span<const float>`), sample rate, phase type, tap count, design
method, and whatever normalization value was applied -- and nothing writes a
file. `app/` owns both file formats (text-with-header, WAV) as two thin
writers over that struct, matching where `SessionCodec`/`SessionDocument`
persistence already lives (confirmed in `app/`, not `core/`, per the L6b
Part C plug-in-surface read).

**The polynomial-form guard needs a clarification, not a change.**
`core/tests/check_no_polynomial_form.cmake` forbids `signal.lfilter`,
`freqz(b`, `zpk2tf`, `tf2sos`, etc. anywhere under `core/` and `tools/`,
because an IIR ("ba") transfer-function polynomial can have a denominator
root outside the unit circle and diverge (`docs/dsp/2026-08-27-filterbank.md`'s
`7.4e+130` measurement). **FIR taps have no denominator** (`a = [1]`) and
therefore no pole to escape the unit circle -- an FIR coefficient vector
cannot diverge the way that guard exists to catch. This is worth writing
down explicitly in the eventual decision record so a future reader does not
assume the guard's existence means "core must never hold a coefficient
vector" -- it means specifically "core must never hold an IIR numerator/
denominator pair," which FIR is not.

That said, the guard's **pattern matching is blunt, not semantic**: a golden
generator that calls `scipy.signal.freqz(taps, 1, ...)` to verify an FIR's
frequency response is fine mathematically and would still trip the literal
string `freqz(b` only if the variable happened to be named `b`; a generator
that calls `scipy.signal.lfilter(taps, [1], x)` to apply an FIR in the
time domain trips the guard's `signal\.lfilter` pattern regardless of
whether a denominator with roots outside the unit circle is even possible
here. **Practical guidance for `tools/gen_fir.py`**: don't name the tap
variable `b`, and apply FIR taps with `numpy.convolve` rather than
`scipy.signal.lfilter` -- not because `lfilter` is unsafe for FIR, but
because the guard cannot tell the difference and there is no reason to make
a future CI failure someone else has to diagnose.

## Where sources disagree

1. **Phase default**: rePhase and REW both expose linear and minimum phase
   as parallel choices with no single default pushed harder than the other
   (from memory of using both -- UNVERIFIED in detail). No surveyed source
   argues one is simply better; they solve different problems (see phase
   section).
2. **Design method**: rePhase's documented workflow is frequency-sampling
   (IFFT + window); this repo's proposed primary (weighted least-squares) is
   not what either rePhase or REW is understood to do by default -- this is
   this project's own choice, not a consensus position, and should be
   labeled as such in the decision record, not attributed to "how the
   industry does it."
3. **Export format**: raw headerless text (rePhase, REW plain export) vs
   SR-carrying WAV vs external-config-declared SR (CamillaDSP) -- three
   different places to put the one fact (sample rate) that must never be
   implicit, and no convention surveyed puts it in more than one place at
   once, which is exactly how it goes missing.
4. **Whether Remez is even a candidate for correction curves**: this
   document argues no (wrong problem shape, piecewise-band formulation);
   no source directly argues the opposite for a *correction* application
   specifically -- Remez's real, undisputed home is brickwall/crossover
   design, which is G18, not this sub-lane.

## Open questions for the decision record

1. Exact conditioning behaviour of a from-scratch Levinson-Durbin solver on a
   realistic correction target (a sharp narrow boost is the adversarial case)
   -- untested, no golden vector exists yet; the `atol`/`rtol` pair above is
   a placeholder, not a measurement.
2. Whether to expose a mixed/"excess phase" blend (rePhase's third option)
   in a first version, or ship linear/minimum only and add the blend later
   -- a real, precedented third design point, just a scope call not made here.
3. Exact FFT-oversampling factor the homomorphic minimum-phase method needs
   to keep cepstral aliasing negligible -- flagged UNVERIFIED above, needs a
   from-scratch derivation or a confirmed read of a real
   `scipy.signal.minimum_phase` source tree before being hard-coded.
4. What tap-count ceiling this project should warn about by default, with no
   specific downstream convolver chosen yet -- today's honest answer is
   "state resolution and latency, let the operator judge."
5. Whether the auto-EQ / target-curve source this design will consume (both
   upstream of G10, neither built yet per `docs/plans/MASTER-EXECUTION-PLAN.md:125`)
   hands this module a smooth banded curve (`OctaveBands`/`BandWeights`
   shape) or a raw per-bin curve -- the least-squares weight function above
   assumes the former; confirm once G10's upstream source is designed.
