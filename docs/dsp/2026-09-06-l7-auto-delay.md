# Auto-delay and delay suggestions (lane L7, sub-lane L7-DELAY)

*Decision record, station 2 of the pipeline in `docs/reports/README.md`.
Written 2026-09-06 on `1ad48da` from the station-1 report
`docs/research/2026-09-06-l7-auto-delay-station1-research.md`. Every claim
about the code below was re-read in the file it names. Every number that is
not closed-form was produced by two throwaway NumPy probes run for this record
(`phat_probe.py`, `phat_snr.py`, scratchpad only — the plan re-creates them
under `tools/` with argparse, per `memory/a-gen-script-runs-the-moment-you-invoke-it.md`).
This record consumes the output-path interface of
`docs/dsp/2026-09-06-l7-output-path.md` §6/§11 and does not redesign it.*

## 0. What L7-DELAY is for, and what already exists

An operator needs the number to type into a delay line: how many samples the
measurement lags the reference. `rta::dsp::findDelayPhat`
(`core/include/rta/dsp/DelayFinder.h:97`) already answers that question —
GCC-PHAT, linear (zero-padded) correlation, relative regularisation, integer
argmax over `|r|`, parabolic sub-sample refinement, a normalised `peak`, and a
polarity flag — and nine fixtures pin it (`core/tests/test_delay_finder.cpp`).
The research's reframing is adopted without reservation: **auto-delay is a
policy layer over that primitive, not a new correlator.** What it adds is
(a) which of several peaks is the answer, (b) a trust figure with a derivation
rather than a vibe, (c) a live number that updates without a new capture, and
(d) the `app/` work that turns a number into a suggestion an operator applies.

## 1. Provenance: what the research established, and what this record corrects

Confirmed by re-reading: the correlator's API and its nine fixtures; the
`Sxy = conj(X)·Y` convention shared with `DualFftEngine`; the sign convention
"positive = measurement lags reference, feeds `referenceDelaySamples` with no
negation" (`DelayFinder.h:12-17`); the sub-sample sign edge case documented as
unverified (`DelayFinder.cpp:147-162`); `GroupDelay.h` as the per-bin phase
derivative; OSM's separate `2^16` correlator and 25-block counter as the
"obvious" design. Six things the research left open or got wrong:

1. **`findDelayPhat` has no caller outside its own tests.** A grep of `app/`
   for `findDelayPhat` or `DelayEstimate` returns nothing; `app/` only carries
   `referenceDelaySamples` through `Analyser::Config`. There is also **no raw
   capture path**: `AnalysisThread::drainPaired` hands hops to
   `Analyser::pushPair` and nothing accumulates a multi-second span of both
   channels. The one-shot mode needs one; §11 places it.
2. **The L4a lesson runs the other way for delay.** The research reads L4a's
   rejection of "loudest peak wins" as a warning auto-delay should inherit. L4a
   itself says the opposite about *timing*: on a subwoofer the first-crossing
   index moved 67 to 158 samples with the fraction and "must not be quoted as a
   delay" (`docs/dsp/2026-08-30-sweep-ir-l4a.md` decision 6). L4a's rule
   picks a **sign**; it is a bad statistic for a **time**. §3 shows the
   first-crossing rule fails on PHAT output for a second, closed-form reason.
3. **Approach C (phase-slope fit) is rejected, on grounds the research
   half-stated.** A linear fit needs unwrapped phase, which L2 §6 keeps out of
   `core/` because it is history-dependent; an unwrap-free variant is a
   *centroid* and reports neither arrival under multipath. The data source it
   proposes — the engine's averaged `Sxy` — is right; the estimator is
   replaced (§5).
4. **The parity table's G16 wording is backwards.** "Delay finder gains an
   optional environment input" (`docs/dsp/2026-08-28-competitive-parity.md:46`)
   would have air temperature change a count of samples. G16 is a unit
   conversion and a drift watch downstream of this lane (§9). No G16 code
   exists anywhere (grep for `speedOfSound|temperature|humidity` over
   `*.h,*.cpp`: nothing).
5. **A periodic excitation breaks the one-shot.** `test_delay_finder.cpp:15-17`
   already names it: a looped source puts peaks at `D + k·period`. `gen::Mls`
   has period `2^n − 1`; the app must not excite the one-shot with it unless
   the capture is shorter than the period. Pink noise (`gen::PinkNoise`, PCG32,
   aperiodic over any capture) is the default excitation (§11).
6. **`referenceDelaySamples` is construction-time.** `DualFftEngine.h:101-105`
   calls the skips "one-time, construction-only"; applying a delay rebuilds
   the engines and re-fills sixteen frames before the coherence gate re-opens.
   That fact decides §8 on its own.

## 2. Decision: one correlator, two front doors

**Decision.** The PHAT weighting, inverse transform, windowed peak pick and
parabolic refinement that live inside `findDelayPhat` today are factored into
a spectral half that both modes call:

```
cross-spectrum + per-bin weight  ─►  whiten & weight  ─►  IFFT  ─►  pick peaks in window  ─►  refine
        ▲                                                                        │
  one-shot: X*·Y of two zero-padded spans (linear, weight = 1 in band)           ▼
  tracker:  DualFftEngine::crossPsd() (circular, weight = γ²)          verdict + candidates
```

`findDelayPhat(span, span, PhatOptions)` keeps its signature and its nine
fixtures, which are the regression lock for the refactor. No second window, no
second regularisation constant, no second FFT engine: the only forward
transforms in the tracker are the ones `DualFftEngine::process()` already
performs every hop.

**Against the obvious transplant — OSM's second correlator on raw audio.**
The research's rejection stands and is sharpened by §1.1: this repo does not
even have the raw-audio path OSM re-transforms, so the transplant would build
a ring, a window, a 2^16 FFT and a counter to reach a number the engine's
accumulator already holds in `sxyMean_`. Its one virtue — a real correlation
search rather than a fit — is kept by running that search on the accumulator
instead (§5).

## 3. Decision: the one-shot picks the argmax inside a stated window and returns the candidates — there is no first-arrival fraction

**Decision.** The verdict is the largest `|r|` inside a **plausibility
window** `[minLag, maxLag]` the caller states in samples (default: the whole
linear range; the app derives a tighter one from an operator-stated maximum
distance — "100 m" is a fact about a venue, "0.5 of the peak" is not). Beside
the verdict the result carries up to `K` (default 4) **local maxima** in the
window, ranked by height, each with lag, sub-sample, signed height and
`inverted`, and an **ambiguity** `= |second| / |best|` in `[0, 1]`. Ambiguity
is displayed and never gated on — L4a's `margin` failed as a gate and earned
its place as a display (`Polarity.h:103-110`).

**Why not the first-crossing rule, derived.** PHAT whitens the cross-spectrum.
For two arrivals `y = x[n−D₁] + a·x[n−D₂]`, `Δ = D₂ − D₁`, the whitened
spectrum is `(1 + a·e^{−jωΔ}) / |1 + a·e^{−jωΔ}| · e^{−jωD₁}`. Expanding
`|1 + a·e^{−jθ}|^{−1} ≈ 1 − a·cos θ` to first order gives
`1 + (a/2)·e^{−jωΔ} − (a/2)·e^{+jωΔ}`: a peak at `D₁` (height 1), a peak at
`D₂` (height `+a/2`), and an **acausal, polarity-inverted ghost at `D₁ − Δ`
of height `−a/2`** — an arrival earlier than the direct sound that does not
exist. Measured (white `x`, `L = 2^14`, `D₁ = 300`, `D₂ = 420`):

| a | r[D₁] | r[D₂] | r[D₁−Δ] | argmax | ghost / main | a/2 |
|---|---|---|---|---|---|---|
| 0.3 | +0.949 | +0.146 | **−0.142** | 300 | 0.15 | 0.15 |
| 0.6 | +0.874 | +0.306 | **−0.247** | 300 | 0.28 | 0.30 |
| 0.9 | +0.715 | +0.510 | **−0.254** | 300 | 0.36 | 0.45 |
| 1.5 | +0.352 | +0.840 | −0.067 | **420** | — | — |

A "first `|r|` above fraction `f`" rule latches the ghost whenever
`f < a/2`: at `f = 0.2` a reflection 4.4 dB down (a = 0.6) answers 120
samples **early** and **inverted**. This is the linear-phase-FIR pre-ring
failure that killed `arrivalFraction = 0.2` in L4a (`Polarity.h:60-65`),
produced here by the whitening itself rather than by the loudspeaker. The
polarity rule works on a *deconvolved IR* whose reflections are causal; PHAT's
correlation is a different object with ghosts on both sides. **Research
question 6 is answered: the two rules do not become one primitive.**

**What argmax gets wrong, and where that case actually belongs.** Argmax
picks `D₂` once `a > 1` (row 4). A later arrival louder than the direct path
is, in live sound, almost always a **second loudspeaker playing at the same
time** — a main while the sub is measured, a fill nearer the mic — and the
fix is `soloOutput` (L7-OUT §3), not a peak rule; the sequence in L7-OUT §6
already gives every solver one box at a time. The residual case (a mic in the
direct-path shadow, a focusing surface) is exactly what the candidate list is
for: the operator sees "0.84 at 8.8 ms, 0.35 at 6.3 ms" and chooses, which
no surveyed product lets them do. A rule that guessed for them would be
another threshold read off a grid.

## 4. Decision: trust is the normalised peak against a derived null floor; the accept multiple is the one number the build surveys

**Decision.** `trust = peak / f_band`, where `f_band = (maxHz − minHz)/(fs/2)`
is the in-band fraction the existing band-limit fixture already pins
(`test_delay_finder.cpp:106-107`). `trust ∈ [0, 1]` by construction: it is
`|Σ_k w_k e^{jφ_k}| / Σ_k w_k`, a triangle-inequality bound — kind 1 in
`memory/a-threshold-read-off-a-grid-is-that-grids-floor.md`, the variable
cannot run away. Under the null hypothesis (uncorrelated channels) the
correlation is `m` samples of a sum of `M_in` unit phasors, standard deviation
`√(2·M_in)/m`, whose maximum sits near `σ·√(2 ln m)`; normalised, the
**null floor is `√(ln m / M_in)`**, which for the full band is
`√(2 ln m / m)`. Measured over 40 trials each:

| L | m | predicted floor | mean of max | worst of 40 |
|---|---|---|---|---|
| 2^13 | 16384 | 0.0344 | 0.0367 | 0.0424 |
| 2^14 | 32768 | 0.0252 | 0.0273 | 0.0315 |
| 2^15 | 65536 | 0.0184 | 0.0199 | 0.0220 |

The leading-order prediction reads 7 % low on average and 25 % low at the
worst of 40 — the Gumbel tail the `√(2 ln m)` term omits. The band-limited
form `√(ln m / M_in)` is derived and not yet measured; the plan measures it.

**Where argmax actually fails, measured against that floor** (white `x`,
white noise, `m = 32768`, `D = 300`, ten trials per row):

| SNR | wrong / 10 | trust mean | trust min | trust / floor |
|---|---|---|---|---|
| 0 dB | 0 | 0.592 | 0.583 | 23× |
| −6 dB | 0 | 0.355 | 0.344 | 14× |
| −12 dB | 0 | 0.191 | 0.179 | 7.6× |
| −18 dB | 0 | 0.097 | 0.086 | 3.8× |
| −24 dB | 0 | 0.046 | 0.034 | **1.8×** |
| −27 dB | **1** | 0.034 | 0.029 | 1.35× |
| −30 dB | 7 | 0.028 | 0.025 | 1.1× |
| −33 dB | 10 | 0.028 | 0.025 | 1.1× |

The answer goes wrong when, and only when, the true peak sinks into the null
maximum. **The boundary is the derived floor, not a grid floor.** This is why
this gate does *not* need L4a's multi-filter-family survey: polarity's gate
hunted a deterministic boundary through filter space with nothing to bound it;
this gate separates a signal from a noise whose distribution is known. PHAT
whitening removes the magnitude response from the problem — an allpass and an
LR4 crossover leave argmax at 300 and trust at 0.81 (measured, §7) — so
filter *shape* is not an axis.

**The one remaining judgement.** `accept iff trust ≥ c · √(ln m / M_in)`,
with `c` a `DelayPolicy` field. The record proposes **`c = 4`** (trust
≈ 0.10 at `m = 32768`, i.e. −18 dB SNR on white noise) as the plan's starting
value: comfortably above the 1.8× at which no failure was seen and well below
anything a working measurement produces (a clean capture reads 0.96). The
survey the build must run is **one mechanism along four axes** — SNR, `D/L`
overlap (peak measured 0.93 / 0.77 / 0.59 / 0.41 at `D/L` = 0.06 / 0.25 /
0.50 / 0.75, argmax still right at 0.75, so overlap is a real trust loss but
not a failure), band limit (§7), and excess-phase order — with two acceptance
checks: the null floor within 30 % of prediction, and zero wrong answers above
`c` times it. It confirms a derivation; it does not source the number. A
refusal is named (`BelowFloor`, `WindowEmpty`), never a silently returned
zero.

## 5. Decision: the tracker is coherence-weighted PHAT on the engine's averaged cross-spectrum — not a phase-slope fit, not a second correlator

**Decision.** The live mode applies `ψ_k = γ²_k / |Sxy_k|` to
`DualFftEngine::crossPsd()` (`DualFftEngine.h:81`), one real IFFT of the
engine's `fftSize` per publish, then the same windowed pick and refinement as
§3. `γ²_k` is the gated `TransferSnapshot::coherence`; when that is `nullopt`
the tracker **returns absence** — nothing is computed, nothing is shown. The
number it reports is the **residual** after the compensation the engine
already applies: `total = config.referenceDelaySamples + residual`. After a
correct Apply it reads ≈ 0, which *is* Smaart's "measure twice" confirmation,
obtained without a second capture.

**Why not the phase-slope fit the research recommended.** Three defects, each
sufficient. (1) A linear fit needs unwrapped phase, and L2 §6 rules that
unwrapping is a view operation because one bad bin steps every bin above it —
putting it in `core/` for the tracker would reopen a settled decision. (2) The
unwrap-free variant — averaging adjacent-bin phase differences, or the
coherence-weighted mean of `groupDelaySeconds` — is the energy-weighted
**centroid** of the correlation, and under two arrivals a centroid sits
between them; PHAT's peak is the **mode** and sits on one. Delay wants the
mode. (3) `GroupDelay.h:17-22` says of itself that a derivative amplifies
bin-to-bin noise and must state a smoothing width; a correlation peak needs no
such parameter. The fit's one advantage, cost, is not real: one IFFT of 16384
points at 20 Hz is a fraction of a millisecond on the analysis thread.

**Why `γ²` and not flat PHAT or the maximum-likelihood weight.** Flat PHAT on
gated bins needs a coherence *threshold* to decide which bins — a grid
number. The Hannan–Thomson weight `γ²/(1−γ²)` is optimal and explodes as
`γ² → 1`, needing a floor the estimator's own bias makes a judgement call —
L6b §3's argument against inverse-variance weights, verbatim. `γ²` is bounded,
needs no floor, is the weight L6b already ships (`W = u·γ²`), and gives a
tidy identity: for a pure residual delay the peak is bounded by the mean
gated coherence, with equality when the phase is exactly linear. Measured
(`N = 4096`, Hann, hop `N/2`, 16 frames, noise at −6 dB):

| residual D | D/N | argmax | peak | mean γ² |
|---|---|---|---|---|
| 3 | 0.001 | 3 | 0.801 | 0.804 |
| 37 | 0.009 | 37 | 0.797 | 0.801 |
| 700 | 0.17 | 700 | 0.552 | 0.560 |
| 1500 | 0.37 | 1500 | 0.157 | 0.173 |
| 2100 | 0.51 | **−1996** | 0.042 | 0.075 |

**The tracker's bound, stated so nobody asks it to acquire.** The engine's
cross-spectrum is circular over `N` and its frames are windowed, so a residual
beyond `N/2` aliases (row 5) and coherence collapses well before that (row 4:
the two Hann windows barely overlap). With the L6b fixed engine at
`fftSize = 16384` the tracker follows a residual of ±1500 samples (±31 ms)
comfortably and ±4000 marginally. **It is a residual tracker.** Acquisition —
an unknown delay of 300 ms in a stadium — is the one-shot's job, with its
linear correlation over a span of any length. This asymmetry is not a product
choice; it is why there are two modes.

## 6. The two modes, precisely

| | **Locate** (one-shot suggestion) | **Track** (live residual) |
|---|---|---|
| Competitor name | Smaart *Delay Locator*; OSM `maxIndex()` | Smaart *Delay Tracker*; OSM's 25-block re-estimate |
| Input | two aligned spans of `L` samples, captured for the purpose | `crossPsd()` + gated `coherence` of the running engine |
| Correlation | linear, zero-padded to `m = bit_ceil(2L)` | circular over `fftSize`, Hann-windowed frames |
| Range | `±L`, loss with `D/L` | residual within `±fftSize/4` in practice |
| Coherence | unavailable — one frame reads `γ² ≡ 1` (L2 §3) | the weight itself |
| Band limit | operator-stated (§7) | automatic — `γ²` is low where `Y` has no energy |
| Excitation | pink noise via L7-OUT §6, or programme material | whatever the engine is already measuring |
| Output | verdict, `trust`, floor, ambiguity, ≤ K candidates, named refusal | residual, sub-sample, peak, mean `γ²`, `inverted`; or absent |
| Cadence | on demand | every publish (≤ 20 Hz), one IFFT |
| May it write `referenceDelaySamples`? | **only through an operator or solver Apply** | **never** (§8) |
| Absence | refusal with reason | `nullopt` while the gate is closed |

OSM collapses these into one silent periodic re-run and Smaart leaves the
tracker undocumented; both are recorded in the research as the ambiguity this
record had to close. It closes it on the maths of §5, not on taste.

## 7. Decision: the one-shot's band limit comes from the operator's stated span, because whitening punishes a narrowband system

**Decision.** `PhatOptions::minHz/maxHz` for the one-shot are set by `app/`
from the frequency span the operator is already displaying (L6b §7's rule for
level alignment: a choice they made and can see), never from a fixed default.
The tracker needs no such input.

**Why.** PHAT whitens every bin in `[minHz, maxHz]` to unit magnitude. Where
the *measurement* has no energy the whitened bins are noise, and the peak they
should have built goes into the floor. Measured with no band limit, no noise,
`D = 300`:

| system on the measurement channel | argmax | trust |
|---|---|---|
| 2nd-order allpass, 1 kHz, Q 0.7 | 300 | 0.806 |
| LR4 crossover sum (excess phase only) | 300 | 0.808 |
| `butter(8)` band-pass 1–16 kHz (a horn) | 304 | 0.452 |
| `butter(4)` band-pass 60–960 Hz (a low box) | 304 | **0.140** |

The +4 samples on the band-passed rows are the filters' own in-band group
delay — a real latency, not an estimator error. The **trust** collapse is the
cost: a perfectly good low-frequency box reads 0.14, one noisy capture from a
`c = 4` refusal, because `f_band` normalises for the band *asked for*, not the
band the loudspeaker *has*. With `minHz/maxHz` set to the box's band the same
capture reads near 1. The dual-FFT record §4 names this as PHAT's one weakness
and the band limit as its one mitigation; this decision only says where the
band comes from. An automatic band from the measurement's own spectrum would
be a fraction-of-maximum threshold — declined for the reason §3 declines the
other one.

## 8. Decision: nothing applies itself silently

**Decision.** (a) The tracker never writes `referenceDelaySamples`. (b) The
tracker never falls back to a fresh one-shot when coherence collapses. (c) An
Apply — from the operator's button, from `L7-ALIGN`, from any solver — is an
explicit `app/` action that rebuilds the `Analyser` with the new value and
accepts the sixteen-frame gate re-fill that follows.

**Why (a).** §1.6: an Apply is an engine rebuild and a coherence blackout of
~15 frames. A tracker that nudged the compensation on every publish would make
every trace blink and the gate never settle — the L6b §3 fill dynamics, on a
loop. Smaart's tracker is a *watch*; OSM's silent `m_estimatedDelay` update is
the research's disagreement 3, resolved against it.

**Why (b).** `memory/a-fixed-defect-returns-through-the-silent-fallback.md`.
A mic that moved, a source that stopped, and a cable that came out all look
the same to the accumulator: coherence gone. Absence is the correct report
for all three; which one it was is for the operator, or for a wizard that
*asks*, to decide. A re-locate is a one-shot the app can offer or a solver can
choose to run — visibly, never inside the tracker.

## 9. G16 is a consumer of this lane, not an input to it

**Clarification.** Three things travel under "G16 environment compensation":
(1) `c(T, RH)` — speed of sound from temperature and humidity, a pure closed
form for `core/` (Cramer 1993 or ISO 9613-1; which one is G16's own
station-1 question); (2) the conversion samples → ms → metres in the readout,
which is `app/` presentation using (1); (3) the **drift watch** of spec
`docs/specs/2026-08-28-interactive-tuning-visuals.md` V3d — "delay drift vs
temperature", the HUD chip that goes amber when the 3 pm tune has moved by
evening. (3) is **exactly the tracker's residual over time**: 1 °C moves `c`
by ~0.17 %, a 30 m throw by ~7 samples; ten degrees is 1.5 ms, half a cycle
at 330 Hz. The tracker supplies the measured drift; G16 supplies the predicted
drift from a temperature the operator enters; the chip compares them. Nothing
in G16 changes what the correlator computes, so the parity table's row 46
should be re-worded at station 3 to "the delay *readout* gains an
environment input; the drift watch consumes the L7-DELAY tracker". Whether
the amber threshold is a fraction of a period at the crossover frequency (a
statement in the vocabulary of the thing measured) is for G16 to decide, not
here.

## 10. Rejected options and what each would have cost

| Rejected | Cost that decided it |
|---|---|
| OSM's second correlator on raw audio, every 25 blocks | duplicates the forward FFT, window, ring and regularisation for a number `sxyMean_` already holds; this repo has no raw path to re-run it on (§1.1, §2) |
| First-arrival fraction rule (L4a's shape) for peak selection | PHAT creates an inverted ghost at `D₁ − Δ` of height `a/2`; any `f < a/2` answers early and inverted; L4a itself says the index is not a delay (§3) |
| A rule that prefers the earlier of two strong peaks | guesses on the operator's behalf where solo already removes the common case; the candidate list is strictly more informative (§3) |
| Gate on ambiguity | L4a's `margin` refused 74–88 % of good boxes as a gate; display-only (§3) |
| Threshold on raw `peak` | scales with the band asked for; `trust = peak/f_band` is the bounded form (§4) |
| Multi-filter-family survey for the trust gate | whitening removes magnitude shape from the problem; the boundary is the derived null floor, confirmed by a one-mechanism survey (§4) |
| Phase-slope fit over `groupDelaySeconds` (research approach C) | needs unwrap (L2 §6) or is a centroid; a derivative with a smoothing width; no cheaper than one IFFT (§5) |
| Flat PHAT on gated bins for the tracker | needs a coherence threshold — a grid number (§5) |
| Hannan–Thomson `γ²/(1−γ²)` weight | explodes near 1, needs a floor; L6b §3 (§5) |
| Tracker auto-applies | every nudge rebuilds the engines and blanks coherence for 15 frames (§8) |
| Silent fallback to a one-shot on coherence loss | the memory's silent-fallback failure; three causes look alike to the accumulator (§8) |
| Automatic band limit from the measurement spectrum | a fraction-of-maximum threshold in disguise (§7) |
| Temperature as an input to the correlator | air does not change sample counts (§9) |

## 11. Boundary: what each layer owns

- **`core/`** — the refactored correlator: a spectral half (whiten-and-weight,
  IFFT, windowed pick of ≤ K local maxima, parabolic refinement) with two
  front doors. `suggestDelay(span, span, PhatOptions, DelayPolicy)` returning
  `{DelayEstimate best, trust, nullFloor, ambiguity, candidates, verdict}`;
  `findDelayPhat` unchanged in signature and implemented on the same halves.
  A `ResidualDelayTracker` class constructed for one `fftSize` (it owns a
  `RealFft` and scratch, so `update()` never allocates) whose
  `update(const DualFftEngine&, const TransferSnapshot&)` returns
  `std::optional<DelayTrack>` — `nullopt` whenever `snapshot.coherence` is.
  It *reads* the coherence field; `check_coherence_gate.cmake` stays untouched
  because nothing here writes one. Spans and accumulators in, numbers out; no
  device, clock or thread. Exact names are the plan's; the shape is fixed here.
- **`platform/`** — nothing new. Excitation goes through `OutputEngine` as
  L7-OUT §6 specifies; capture comes off `CaptureBus` as today.
- **`app/`** — (1) a **raw-capture accumulator** on the analysis thread: for a
  Locate, collect `L` samples of the reference and of the measurement channel
  the active transfer function names, from the same `drainPaired` hops the
  engine already receives, allocated once at arm time, never in the callback.
  (2) The Locate sequence: `setSource(PinkNoise)` → `routeOutput(ch, true)`
  (solo per L7-OUT §3) → `armSource()` → wait `renderedSamples() ≥ 480·fs/48000`
  (L7-OUT §11: the first 10 ms are not stationary) → capture `L` → `disarmSource()`
  → `suggestDelay` → present. (3) **Apply**: rebuild the `Analyser` with
  `referenceDelaySamples = best.delaySamples` (sub-sample shown, not applied —
  the engine offset is an integer by design, `DualFftEngine.h:43-50`).
  (4) Presentation: one-decimal ms plus whole samples (spec
  `interactive-tuning-visuals.md:75`), trust as words or a bar and never as a
  bare float, candidates as a short list, `inverted` as a flag; the tracker's
  residual as the drift readout G16's chip consumes. (5) `L` from the
  plausibility window: `L ≥ 4·maxLag` keeps overlap loss under the 0.77 row of
  §4's table — a convenience default, not a gate.

## 12. How CI proves this with no sound card

Core tests in `core/tests/test_delay_policy.cpp` and
`test_residual_tracker.cpp`; the existing `test_delay_finder.cpp` unchanged
and green before and after the refactor. Probes under `tools/` with argparse.

1. **Refactor lock.** All nine existing fixtures pass unchanged; `findDelayPhat`
   and `suggestDelay(...).best` agree bit-for-bit on every one of them.
2. **Two-arrival closed form (§3).** `y = x[n−300] + a·x[n−420]`: for
   `a ∈ {0.3, 0.6, 0.9}` the verdict is 300; the candidate list contains 420
   with positive sign and 180 with `inverted == true`; `|r[180]|/|r[300]|`
   is within 0.05 of `a/2` at `a = 0.3` (first-order identity, tight only for
   small `a`). For `a = 1.5` the verdict is 420 and 300 is the second
   candidate — asserted as *what argmax does*, labelled as the case solo
   removes, not as correctness.
3. **Window.** The same fixture with `maxLag = 350` returns 300 for `a = 1.5`
   (the window removes the louder later arrival); with `[100, 200]` it returns
   the ghost at 180 **with `inverted == true`** — the window is honoured and
   the inversion flag survives, so a wrong window is *visible* rather than
   silently plausible; with `[1000, 2000]`, where no local maximum exists
   above the null floor, it returns `WindowEmpty`.
4. **Null floor (§4).** Forty uncorrelated pairs at `m ∈ {2^14, 2^15, 2^16}`:
   the mean of the maximum normalised peak within 15 % of `√(2 ln m/m)` and
   no sample above 1.4× it; the band-limited form `√(ln m / M_in)` at
   200–4000 Hz within the same bounds (this row is the derivation's first
   measurement). Stated as a statistical bound with a fixed seed, labelled.
5. **Trust gate, both directions (§4).** The shipped noise fixture
   (`x + 2n`, −6 dB) accepts at `c = 4` with trust > 0.3; noise at −30 dB is
   refused `BelowFloor`; silence is refused, never NaN. The SNR table of §4
   is regenerated by a `tools/probe_delay_trust.py` and its two acceptance
   checks are the plan's numbers.
6. **Tracker closed form (§5).** L2 §7's `y = g·x[n−D]` through a real
   `DualFftEngine` at `fftSize = 4096`: after the gate opens, `residual == D`
   exactly for `D ∈ {0, 3, 37, 700}`; `peak ≤ mean γ²` at every publish and
   within 0.02 of it for `D ≤ 37`; before the gate opens, `nullopt`.
7. **Residual semantics.** The same signal with `referenceDelaySamples = D`
   reads residual 0; with `D − 5`, residual 5.
8. **Alias bound.** `D = 0.51·N` reads a negative residual — asserted as the
   documented limit, so a later reader cannot mistake it for a bug.
9. **No silent fallback.** Mid-run, replace the measurement with independent
   noise: `coherence` returns to `nullopt` after the FIFO drains and the
   tracker returns `nullopt` — never the last good value.
10. **Excess phase (§7).** An allpass and an LR4 sum leave the verdict at `D`
    with trust > 0.75; a 60–960 Hz band-pass with `minHz/maxHz` set to its
    band reads trust > 0.75, and without them reads below 0.2 — the band-limit
    row that justifies §7, asserted in both directions.
11. **Allocation.** `ResidualDelayTracker::update` under a counting allocator
    allocates nothing after construction.
12. **Sub-sample honesty.** Fractional offsets 0.1…0.9 by linear
    interpolation: total within 0.1 sample at each, the existing margin, and
    **no tighter claim** — the `.cpp` comment's 0.11-sample divergence at
    frac 0.3 stays documented and untested for the reason it gives.
13. **App.** The raw accumulator yields spans of exactly `L` from the same hop
    sequence the engine saw; Apply rebuilds and the next snapshot's
    `referenceDelaySamples` equals the verdict; the Locate sequence against
    `OutputEngine` in a JUCE-free test observes arm → capture → disarm order.

## 13. What this record does not decide

- The exact `c`; §4 proposes 4 and the survey confirms or moves it, and the
  record is amended in place either way.
- Whether Locate may also run on programme material with no excitation (it
  can; whether the UI offers it is an L7 presentation call).
- The G17 wizard's use of candidates and trust (research question 5): it
  receives the whole `DelaySuggestion`; what it does with ambiguity is its
  record's business.
- G16's formula for `c(T, RH)` and its amber threshold (§9).
- MTW: the tracker runs on the fixed engine only; a per-band residual over
  `MtwResult::bandSnapshots` is a later amendment nothing here forecloses.
- Persisting the applied delay per transfer function — already a `[tf]` field
  in L6b's schema 3; nothing new.
- Decorrelated or multi-signal excitation (L7-OUT §12).

## 14. Open questions for a human

1. **Default plausibility window.** The record defaults to the full linear
   range and lets the operator narrow it by distance. Should the app ship a
   venue-scale default (e.g. 150 m ≈ 440 ms) so a stadium and a studio do not
   start from the same window? One sentence fixes it.
2. **Does Locate solo by default?** §3 leans on `soloOutput` to remove the
   louder-later-arrival case. L7-OUT §13.2 already asks whether G20's default
   is strict solo or additive; the same answer applies here and should be
   given once.
3. **Should the tracker be on by default** whenever a transfer function is
   live, or armed by the operator? It costs one IFFT per publish and returns
   absence when the gate is closed, so "on" is cheap — but a residual that
   reads 3 samples on every screen may invite chasing noise. The spec's V3d
   chip suggests it lives with the drift watch; confirm.

## Sources

This repo: `core/include/rta/dsp/{DelayFinder,GroupDelay,DualFftEngine,TransferEstimator}.h`,
`core/src/dsp/DelayFinder.cpp`, `core/tests/test_delay_finder.cpp`,
`core/include/rta/ir/Polarity.h`, `app/src/measure/{Analyser,AnalysisThread}.h`,
`docs/dsp/2026-08-28-dual-fft.md` §3, §4, §6, §7;
`docs/dsp/2026-08-30-sweep-ir-l4a.md` decisions 6, 6b;
`docs/dsp/2026-09-06-multichannel-l6b.md` §3, §7; `docs/dsp/2026-09-06-l7-output-path.md`
§3, §6, §11, §13; `docs/dsp/2026-08-28-competitive-parity.md` G16/G17;
`docs/specs/2026-08-28-interactive-tuning-visuals.md` V3d;
`docs/research/2026-09-06-l7-auto-delay-station1-research.md`;
`memory/a-threshold-read-off-a-grid-is-that-grids-floor.md`,
`memory/a-fixed-defect-returns-through-the-silent-fallback.md`,
`memory/a-gen-script-runs-the-moment-you-invoke-it.md`.
External, as reached by station 1: Rational Acoustics support article
150000191499 (Delay Locator, measure-twice); `psmokotnin/osm`
`src/source/measurement.cpp:545-547, 638-639`; Knapp & Carter 1976 (GCC
weightings — PHAT, SCOT, Hannan–Thomson; form recalled, paper not opened).
Measurements in §3, §4, §5, §7: two NumPy probes run 2026-09-06 with the
main-checkout venv (`scipy.signal.butter/lfilter`, `numpy.fft`), seeds 7 and
11, ten to forty trials per row as stated.
