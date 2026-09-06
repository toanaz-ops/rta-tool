# Multichannel workflows (lane L6b): spatial averaging, coherence weighting, sequencing, routing, presets

*Decision record, station 2 of the pipeline in `docs/reports/README.md`.
Written 2026-09-06 on `60ba99c` from the three station-1 reports preserved in
`docs/research/2026-09-06-l6b-station1-research.md` (Parts A, B, C). Every
number below is either closed-form, quoted from a standard with its clause, or
was computed from a function that already ships in `core/`; the arithmetic is
shown where it is not obvious. Gaps addressed: G14, G15, G20 and the routing
matrix and presets from the L6b row of the master plan. The remote API is
scoped out in §10 with its reasons.*

## 0. What L6b is for, in one paragraph

One microphone tells an operator what one seat hears. A system is tuned for a
room, so the operator walks the room — or plants several microphones — and
needs one trace that stands for the room without hiding what any one position
hears. SMPTE ST 202 §5.2 puts a number on why: positions are added until the
position-to-position standard deviation is under 3 dB, typically four of them.
L6b makes the analyser hold N transfer functions at once against one
reference, combine them into a spatial average whose weights say how much each
position and each frequency is trusted, refuse captures the data itself says
are bad, and remember the whole arrangement as a preset. Nothing in this lane
is new DSP: it is arithmetic over `TransferSnapshot`s the L2 and L3 engines
already produce, and the design work is deciding *which* arithmetic and
*where* it may run.

## 1. Provenance: what the research found, and what it corrected

Three premises the lane opened with were wrong and are corrected here so nobody
re-derives them: SysTune "SSA" is *Spectrally Selective Accumulation*, a
temporal bad-data filter (a G20 precedent, not a G15 one); Open Sound Meter
**has** a remote API; REW has **no** coherence weighting. Smaart is the only
product that ships a coherence-weighted spatial average, and it does not
publish the weight. No product publishes how the average's own coherence is
computed. Every product that averages phase does so through the complex mean,
never through unwrapped angles. Bendat & Piersol give the variance of an H1
estimate as `|H|²(1−γ²)/(2·n_d·γ²)` (form confirmed by three sources; equation
number unverified — the book was not opened). In code, OSM's `Union` averages
already-formed `H` and reports `Σ|H_k|γ_k/Σ|H_k|` as "coherence", which cannot
see two coherent positions cancelling; scipy pools cross-spectra and computes
the coherence *of* the pooled estimate. This repo's `DualFftEngine` exposes
`referencePsd()`, `measurementPsd()`, `crossPsd()` and `effectiveAverages()`,
so either estimator is reachable; its `TransferSnapshot::coherence` is an
`optional` that is `nullopt` until the engine has cleared
`minimumEffectiveAverages = 8`, and the guard `coherence_gate_is_not_bypassed`
greps every `core/` source but `TransferEstimator.cpp` for any write to a
field literally named `coherence`.

The repo constraints that shape the design: `DualFftEngine` is not movable
(`unique_ptr` only); `Analyser` owns exactly one fixed and one MTW engine;
`AnalysisThread` reads exactly one `Measurement` channel through
`firstChannelWithRole`; there is **no generator output path** — the audio
callback clears every output; there is no network code; `SessionCodec` is a
line-oriented `key=value` format at schema 2 that refuses newer schemas;
`Analyser.cpp` and `SessionCodec.cpp` are at 287 lines each against a 300 aim.

## 2. Decision: the spatial average is a weighted mean of dB magnitudes; power is the option; phase is the weighted circular mean, reported with its agreement

**Decision.** Per bin, over the N contributing positions, with weights `W_i`
from §3:

```
L      = Σ W_i · 20·log10|H_i|  / Σ W_i                      default ("dB")
L_pow  = 10·log10( Σ W_i · |H_i|² / Σ W_i )                  option  ("Power")
z      = Σ W_i · H_i/|H_i|      / Σ W_i                      phase = arg z
R      = |z|                                                  phase agreement, 0..1
```

`R` is the mean resultant length of circular statistics: 1 when every position
agrees in phase, 0 when they cancel. It is bounded by construction, so it needs
no threshold to be meaningful, and it is what the operator reads instead of a
phase trace that quietly means nothing.

**Why dB and not power, against the standard that says power.** SMPTE ST 202
§A.3.5 averages by "the sum of the squares" and allows arithmetic averaging
only when the spread is within 4 dB. That standard averages **1/3-octave
sound pressure levels of pink noise** — an energy quantity, where the power
mean answers "how loud is the room on average". A transfer function used to
equalise a system answers a different question — "what correction serves every
listener" — and there the position that happens to be hottest at a frequency
should not outvote three quieter ones. Smaart's default is dB for this reason
(LE v9.1 p.77, "a consensus view"); power "gives more weight to the
contributors with higher levels". The cost of the difference is closed-form:
for two positions `Δ` dB apart the power mean sits above the dB mean by

| Δ | 2 dB | 3 dB | 4 dB | 6 dB | 10 dB |
|---|---|---|---|---|---|
| power − dB | 0.11 | 0.25 | 0.45 | 0.96 | 2.40 |

so SMPTE's 4 dB clause is the point where the two disagree by less than half a
decibel, and past it the power mean is measurably a different curve. Power is
kept as the option because it has one documented virtue: it de-emphasises the
nulls of comb filters. Both modes take the same weights.

**Why not the complex (vector) mean.** It is the right estimator for repeated
captures at the *same* position and the wrong one across a room: positions
disagree in phase at every frequency above the first few hundred hertz, and
the magnitude of their complex mean collapses toward zero — REW's help hints at
it by demanding time alignment before a vector average; Part B found OSM's
`Vector` type does it silently. A spatial vector average would show a room
that has no high end. Not shipped; a complex mean of *elements* is a summation
prediction and belongs to G11 (L7).

**Why phase is a circular mean and not an unwrapped mean.** Every product that
answers (REW, Smaart) takes phase from the complex average; pyfar alone unwraps,
offline. Unwrapping across mics is undefined when one position is 2π ahead;
the circular mean degrades gracefully and `R` says by how much.

## 3. Decision: the weight is the per-mic trim times the gated coherence, `W_i = u_i · γ²_i`, and a position below the gate contributes nothing

**Decision.** `W_i(k) = u_i · γ²_i(k)` where `u_i ≥ 0` is the operator's
per-position weight (linear, default 1; a dB trim in the UI) and `γ²_i(k)` is
`TransferSnapshot::coherence` at bin `k`. A snapshot whose `coherence` is
`nullopt` — the engine has not cleared `minimumEffectiveAverages` — is
**excluded** from every bin, and the result carries the number of contributors
per bin. A position with `u_i = 0` is a muted member: it is excluded everywhere,
exactly as if ungated. **Two distinct absences are defined, because the
weights can legitimately sum to zero where contributors exist**:
`magnitudeSquaredCoherence` returns exactly `0.0` at a gate-passed bin when
`sxx` or `syy` is non-positive (`TransferEstimator.cpp:39-48`), so every
contributor at a bin can carry `W_i = 0`. Where `Σ W_i = 0` — whether because
no position passed the gate or because every one that did has zero weight —
the result at that bin is **absent**, and the per-bin record says which of the
two it was (`noContributor` vs `noWeight`). Absence is carried in the type, as
L2 §3 requires of coherence itself, never as a `0/0` that becomes NaN or a
silent zero. If `Σ W_i = 0` at every bin, the function returns no result at
all rather than a curve of anything — the lesson of
`memory/a-fixed-defect-returns-through-the-silent-fallback.md`.

**Why `γ²` and not the inverse-variance weight, which is the "obvious"
statistically optimal choice.** The Bendat & Piersol variance gives
`w ∝ n_d·γ²/(1−γ²)`. It is optimal **only when every position estimates the
same quantity**, and across a room they do not: the difference between
positions is the signal L6b exists to summarise, not noise to be minimised
away. Under inverse-variance weights a single clean position dominates:

| γ² | 0.3 | 0.5 | 0.8 | 0.9 | 0.95 | 0.99 |
|---|---|---|---|---|---|---|
| `γ²` | 0.30 | 0.50 | 0.80 | 0.90 | 0.95 | 0.99 |
| `γ²/(1−γ²)` | 0.4 | 1.0 | 4.0 | 9.0 | 19.0 | 99.0 |

A position at 0.99 outvotes one at 0.9 eleven to one, and the average stops
being spatial. Worse, `γ²` is highest exactly where a position sits on the
constructive peak of a comb and lowest in its nulls, so inverse-variance
weighting is biased toward peaks (the objection Part A ranks fourth among
things nobody documents). That claim holds under the additive-noise model
`γ² = |H|²·Sxx / (|H|²·Sxx + Snn)` with `Snn` and `Sxx` roughly flat across a
comb: the null itself does not lower coherence, the constant noise floor does,
and it does so exactly where `|H|` is small. Under a model where the noise
follows the signal (e.g. distortion) the bias disappears; the decision does not
depend on it, the dominance table alone is sufficient. The bounded weight
discounts a noisy position (0.3 against 0.95 is a 3:1 vote) without letting
any one position own the curve. It also needs no floor: `1/(1−γ²)` explodes as
`γ² → 1`, and the estimator cannot resolve `1−γ²` below its own bias, which
for a true coherence of zero is about `1/n_d` (Carter, Knapp & Nuttall 1973
give `E[γ̂²] ≈ γ² + (1−γ²)²/n_d`; **form recalled, paper not opened —
UNVERIFIED**, 0.116 at the shipping `n_d = 8.5866` if it holds), so a floor
would have been a judgement call dressed as statistics. `γ²` is also the most
literal reading of the only shipping product's description, "weighted
according to its coherence value".

**Cost of the rejected alternatives.** *Inverse-variance*: dominance, peak
bias, a floor to invent. *Unweighted (`u_i` only)*: a noisy position drags the
average at every frequency where it is noisy — the whole of G15 forgone. *A
hard coherence threshold instead of a weight*: another number read off a grid
(`memory/a-threshold-read-off-a-grid-is-that-grids-floor.md`); Smaart's
blanking threshold is a slider with no published default for the same reason.
*`n_d` in the weight*: the L3 design already makes `n_d` uniform across bands
and engines (frames-uniform averaging), so at steady state it multiplies every
weight equally; during fill it would let the longest-running mic dominate,
which is a start-up transient, not a property of the room.

**What the gate does to the average during fill, from the shipped
functions.** All engines start together on a capture; each clears the gate at
frame 15 (`fifoEffectiveAverages(hann, N/4, 15) = 8.07`, saturating at 8.5866
at depth 16 — computed in L3 from `AverageCount.cpp`, verified in
`test_mtw_layout.cpp`). Because every position clears together, the average
appears all at once per band: 80 ms at the top MTW band, 5.1 s below
187.5 Hz — the same figures the L3 record prints on the readout strip. A
position added later contributes nothing until its own fifteenth frame, and the
per-bin contributor count shows it arriving. No new number is introduced.

## 4. Decision: the average reports a trust and an agreement, neither of which is a coherence estimate, and the guard's purpose is kept by construction

**Decision.** The result carries two bounded per-bin figures beside magnitude
and phase: `weightedCoherence = Σ u_i γ²_i / Σ u_i` over contributing positions
— the operator-weighted mean of the trust the gate already granted each
position — and `phaseAgreement = R` from §2. Displayed with two decimals, like
coherence. Neither is named `coherence`, and the difference is substantive, not
lexical: the first is a mean of gated values, the second is a phase statistic.
The function accepts only snapshots that have passed `makeSnapshot()`'s gate
(an ungated one is *excluded*, never read), so the property the guard exists to
protect — no coherence-like number reaches the display without `n_d ≥ 8`
behind it — holds for every input. The guard's own header calls it "a tripwire
for the ordinary and the careless, not a proof"; this record states the
property the tripwire stands for and keeps it.

**Why not OSM's `Σ|H|γ/Σ|H|`.** Weighting trust by loudness lets a hot, noisy
position report high confidence. **Why not the pooled-spectra coherence
`|ΣSxy|²/(ΣSxx·ΣSyy)`.** It is the honest coherence of a *vector* average and
collapses when positions disagree in phase — correct for the estimator §2
rejected, misleading beside a dB mean whose magnitude is unaffected by phase
disagreement. Its information is carried instead by `R`, which is exactly the
phase-agreement component, separated from the noise component. Two numbers,
two meanings, both in [0, 1] by construction.

## 5. Decision: positions must share one engine configuration; MTW averages band by band; each position carries its own delay

**Decision.** `spatialAverage(std::span<const TransferSnapshot>, std::span<const double> u, Mode)`
requires identical `binWidthHz`, `sampleRate` and vector length across inputs
and throws `std::invalid_argument` otherwise: in this app every position's
engine is constructed from one shared `Config`, so a mismatch is a programming
error, not an operator situation (REW's intersection and OSM's toast both
answer a question this design does not ask). For MTW, the same function runs
once per band across positions on the per-band `TransferSnapshot`s that
`MtwResult::bandSnapshots` already holds, and the stitch is the existing one —
1281 points, seams where they were. Each position's engines carry their own
`referenceDelaySamples`, so phase is averaged after per-position delay
compensation, which is the condition REW states for any phase-bearing average.
A positive delay (the physical case — a microphone hears the loudspeaker after
the electrical reference) discards leading **measurement** samples and leaves
the reference ring untouched (`DualFftEngine.cpp:116-127`), so every
position's `Sxx` is computed from identical reference frames; a negative delay
skips the reference instead and shifts that engine's frame grid. Nothing in
this design depends on `Sxx` being identical across positions (§6), but the
plan must not assume it either.

**Argument against the obvious alternative, averaging the stitched MTW
vectors.** It would need a second stitching pass and a flat coherence array in
`core/`, which is what the L3 record refused for the guard's sake. Per-band
averaging reuses `makeSnapshot()`, the gate, and the stitch unchanged.

## 6. Decision: N transfer functions are N `Analyser`s behind a routing table; the average is the published product; individual positions are published on demand

**Decision.** `ChannelConfig` gains, per channel, a transfer-function index
beside the role (still relaxed atomics, so the callback stays lock-free), and a
lookup that returns *all* channels of a role. `AnalysisThread` holds
`std::vector<std::unique_ptr<Analyser>>`, one per transfer function, built once
and never resized, each with its own measurement scratch. **Each transfer
function names its own reference channel** (Smaart's per-engine model, and the
`[tf]` field §9 persists); the drain reads every distinct reference channel
once per hop and hands each `Analyser` the one it names. In the common case
every position names the same reference and one read serves all. A **spatial
average group requires its members to share one reference channel**, and the
group refuses a member that does not — two systems measured against two
references are two groups, not one average. One device only, as today:
Smaart's *Allow Multi-Device TF* is off by default with "clock drift" printed
as the reason, and this app has one `AudioIo`.

Averaging snapshots (§2) rather than pooling cross-spectra is the decision
regardless of the reference arrangement: the chosen estimator is a weighted
mean of *dB magnitudes*, for which "pooling `Sxy`" is not even defined. The
observation that pooling and an unweighted complex mean coincide when `Sxx` is
shared (Part B, scipy section; and only for non-negative delays, §5) is a
consistency note, not a load-bearing premise.

**What it costs, in the numbers that matter.** From the formula Part C derived
and cross-checked against three published L3 figures,
`bytes(N, D) = 106N + 16DN + 32D + 88` per `DualFftEngine`:

| | per instance |
|---|---|
| fixed engine, `fftSize` 16384, depth 16 | 5.93 MB |
| MTW engine at defaults (ΣN = 130 048), depth 16 | 47.08 MB |
| per position (both) | 53.0 MB |
| N = 8 positions | 424 MB, plus the bus's flat 16 MB |

Resident memory is acceptable. **Churn is not, as `Analyser::publish` stands.**
It rebuilds every result from scratch at up to 20 Hz: `MtwResult` ≈ 1.86 MB,
the fixed snapshot ≈ 0.23 MB, the app-side blocks ≈ 0.12 MB — about 2.21 MB
per position per publish, which at N = 8 is 354 MB/s of allocate-and-free on
the analysis thread, against a comment at `AnalysisThread.cpp:196-198` that
budgets "~8 KB". The design answer is scope, not cleverness: **the spatial
average is the published trace**, plus one *soloed* position when the operator
asks for it; the other positions publish a per-position summary (level,
`weightedCoherence`, contributor state) and nothing else. Publish cost then
scales with 2, not N. The plan measures this with a counting allocator in the
publish path, and the "~8 KB" comment is corrected to the measured figure.

**Cost of the rejected alternatives.** *One `Analyser` owning N engine pairs*:
`Analyser.cpp` is at 287 lines and its publish body is the part that grows;
N `Analyser`s keep the file where it is. *Pooling cross-spectra in core instead
of averaging snapshots*: requires a second sentinel in the coherence guard, and
buys nothing while the reference is shared. *MTW per position off by default
to save memory*: 47 MB per position is not the problem, churn is, and the
churn is solved by publishing less, not measuring less.

## 7. Decision: level alignment is a per-position trim plus an optional band-limited auto-align over an operator-chosen band; no fixed normalisation band ships

**Decision.** `u_i` from §3 is entered as a dB trim (Part A: SysTune's
per-channel gain is the only documented per-mic weight, and it is a gain).
An *Align levels* action sets each position's trim so that its
`weightedCoherence`-weighted mean level over the currently displayed frequency
span equals the group's — REW's *Align SPL* over a chosen span. It is an
action the operator invokes, with the numbers it produced shown as the trims.

**Why not Smaart's automatic 225 Hz – 8.8 kHz.** Those bounds are Smaart's
judgement, not a derivation, and Smaart itself prints the failure mode (a
subwoofer has nothing in that band). Baking them into a default is a
threshold read off someone else's grid. The operator's displayed span is a
choice they already made and can see.

## 8. Decision: sequencing is a capture state machine in `app/` that refuses captures on two criteria that need no invented number; generator solo/mute is not built here

**Decision.** A *measurement group* is an ordered list of transfer functions.
A *sequence* steps through the group: arm, wait for every member's gate,
capture, store into the group with the member's name, advance. A capture is
**refused** — a distinct, named refusal, never a silently degraded trace —
when either holds during its window:

1. **Overload**: three or more consecutive samples with `|x| ≥ 1 − 2⁻¹⁵` on
   the reference or the measurement channel (Smaart's published criterion, LE
   v9.1 p.78: "three or more consecutive samples with maximum (0 dBFS)
   amplitude"). The threshold is the coarsest integer full scale a device can
   deliver: int16 clips at `1 − 2⁻¹⁵ = 0.99997`, int24 at `1 − 2⁻²³`, int32
   at `1 − 2⁻³¹`, float at 1.0 — all at or above it. It is a statement in the
   vocabulary of the thing measured, not a grid floor. The detector is a pure
   function on `std::span<const float>` in `core/`, run on the analysis thread
   over each hop, never in the callback.
2. **Gate not cleared**: any member whose `coherence` is `nullopt` at capture
   time — the L2 §3 floor, unchanged.

The coherence-trusted fraction of bandwidth is **reported per capture and not
enforced**: the hypothesised rule "below X across Y % of the band" appears in
no source (Part A), and a value for it is a question for a real system, filed
in `docs/HUMAN-QA-QUEUE.md`. SysTune's excursion filter (soft down-weighting
of a block that departs from the running average) is the one documented soft
rejection and is recorded as a candidate for a later amendment, not built.

**Why generator solo/mute is not in this lane.** SMPTE ST 202 §5.6 ("each
channel or bank shall be measured separately in turn") and M1's *Autosolo*
both assume the analyser drives the outputs. This repo's audio callback clears
every output and says generator output is out of scope (`AudioIo.cpp:134-137`);
`core/src/gen/*` has no caller in `app/`. Building it changes the audio
callback's contract — the one real-time rule in `CLAUDE.md` — and needs its own
research pass (which outputs, what routing, how mute is made lock-free, what
the `audioio_scoped_no_denormals_is_first` guard must keep true). The sequencer
is built with a *step* hook that a future output path attaches to, and until
then a sequence step prompts the operator to solo by hand. Argued against
the obvious ("just write the generator into the callback"): a mute that takes
a lock, or a generator that allocates, is a dropout at a show.

## 9. Decision: presets are a schema-3 session section, routing is stored by device name and channel count and loads unbound rather than silently rebinding

**Decision.** `SessionDocument` goes to `kSchemaVersion = 3` with new sections
`[tf]` (name, measurement channel, reference channel, delay samples, trim dB,
polarity, member-of-average, per-TF averaging with Smaart's *Use Global /
pinned* pattern) and `[average]` (mode, member list). `CaptureMeta` is not
touched — it is immutable history; a preset is editable configuration. The
device is stored by name **and** by input channel count; on load, if no
current device matches both, the routing loads **unbound** and visibly so,
never onto whichever device now answers to the name (OSM's `deviceIdByName`
trap, Part B). Older builds see `NewerSchema`, which is the codec's designed
behaviour. `SessionCodec.cpp` is split before the new arms are added.

## 10. Not decided here: the remote API

Every surveyed transport differs — Smaart binds every adapter including
wireless with an optional password; REW binds `127.0.0.1` with none; OSM
multicasts writable routing to the subnet with none. This repo has no network
code and links no JUCE networking module; the API is a new dependency and a
new attack surface, and choosing its transport, its bind default and whether
it may *write* routing are decisions that deserve their own station-1 pass.
What this record does fix: it lives in a new sibling of `platform/`, never in
`core/` (the framework guard names `asio`) or `ui/`; it consumes
`SnapshotSource`, not `Analyser`; it starts read-only; it reuses the
`key=value` line convention before any JSON dependency is added; and it binds
localhost by default.

## 11. How CI proves this with no sound card

Core tests in `core/tests/test_spatial_average.cpp`, app tests in
`app/tests/`; golden `core/tests/golden/spatial.txt` from `tools/gen_spatial.py`
(argparse, `--out`, `--help` exits before any write — following `gen_mtw.py`).

1. **Identity.** N copies of one snapshot: `L` equals the input to float
   precision, phase equals, `R = 1.0` exactly, contributors `N` at every bin.
2. **Opposition.** Two snapshots `H` and `−H`, equal magnitude, equal
   coherence: `L` equals the input magnitude (a vector mean would give the
   floor), `R = 0.0`, and the result's phase is marked absent where `R` is
   below float resolution.
3. **Weight identity.** Position A with coherence 1.0 and level `a`, position
   B with coherence `c` and level `a + Δ`: `L = a + c·Δ/(1 + c)` closed-form;
   with `u_B = 0`, `L = a` exactly; with `u` all equal and `c = 1`,
   `L = a + Δ/2`.
4. **Power option.** Same fixture in `Power` mode equals
   `10·log10((10^{a/10} + c·10^{(a+Δ)/10})/(1 + c))`; at `c = 1, Δ = 4` the
   two modes differ by 0.445 dB (the SMPTE clause, in numbers).
5. **Gate propagation and zero weight.** A snapshot with `coherence == nullopt`
   is excluded, the contributor count drops by one, and with every input
   ungated the function returns no result — asserted as absence, not as zeros.
   Two gate-passed positions with coherence exactly 0.0 at one bin and 0.9
   elsewhere: that bin is absent with reason `noWeight`, contributor count 2,
   every other bin present; the result carries no NaN anywhere. `u = 0` for
   every member returns no result. Mismatched `binWidthHz` or length throws.
6. **Trust and agreement.** `weightedCoherence` equals `Σuγ²/Σu` on a
   hand-built fixture; `R` for three unit vectors at 0°, 120°, 240° is 0.
7. **MTW per band.** Two `MtwEngine`s fed the same pure-delay pair produce an
   average with 1281 points whose stitched magnitude equals either input and
   whose per-band `R` is 1; fed delays `D` and `D + 1` sample, the top band's
   `R` is `cos(π·f/fs)`-shaped and closed-form per bin.
8. **Golden.** scipy computes N cross-spectra for N positions of a synthetic
   system with independent noise at known SNRs; the golden holds the expected
   `L`, phase, `weightedCoherence` and `R` at the two-term float tolerance
   `memory/float32-fft-precision.md` prescribes.
9. **Overload detector.** Three consecutive samples at `1 − 2⁻¹⁵` flag; two do
   not; int16-full-scale-shaped and float-1.0 fixtures flag; a sine peaking at
   `1 − 2⁻¹⁴` does not.
10. **Guards.** `coherence_gate_is_not_bypassed` stays green with the new file
    scanned (62 → 64); made RED once by writing `result.coherence = …` in the
    new file and watching it fail. `core_has_no_framework_deps` 96 → 99,
    `filter_design_has_no_polynomial_form` 113 → 116; `measure_has_no_framework_deps`
    grows by exactly the number of new `app/measure` files added to its explicit
    list.
11. **Routing.** `ChannelConfig` returns all channels of a role in index
    order; a config with two `Measurement` channels drives two `Analyser`s
    that receive identical reference hops (asserted by sequence counters).
12. **Publish churn.** A counting allocator around one publish at N = 4
    asserts bytes allocated are within 2× of the N = 1 figure — the "average
    plus one solo" scaling of §6.
13. **Preset round trip.** Schema-3 encode → decode preserves every `[tf]` and
    `[average]` field; a schema-2 document decodes with no transfer functions;
    a device-name match with a different channel count loads unbound.

## 12. What this record does not decide

- The remote API's transport, bind default, authentication and write surface
  (§10; own record).
- The generator output path and lock-free solo/mute (§8; own record — it
  changes the audio callback contract).
- A numeric coherence-trusted-fraction refusal for captures (`HUMAN-QA-QUEUE`).
- SysTune-style excursion down-weighting of a block against the running
  average (candidate amendment to §8).
- A complex (vector) spatial mode for repeated same-position captures, and the
  sum-of-elements display — the latter is G11 (L7).
- Storing spatial-average traces in the library: MTW traces are still
  live-only (L5 amendment), and the average inherits that.
- Whether the AFMG patent family (US 8,208,647, real-time multi-channel IR
  averaging) bears on any future *impulse-response* averaging; this record
  averages transfer functions in the frequency domain only. No freedom-to-
  operate opinion exists.

## Sources

Smaart LE v9.1 User Guide pp.49, 75–78, 85–88; Rational Acoustics support
articles 150000214543–6 (spatial, power vs dB, normalized power, coherence
weighted), 150000187830 (magnitude thresholding). AFMG SysTune Manual 1.3
§5.3 pp.123–127, §5.5 pp.139–142; US 8,208,647 B2. REW help `graph_allspl.html`,
`api.html`. SMPTE ST 202:2010 §5.2, §5.3, §5.5, §5.6, A.3.5, A.3.6, A.4. ISO
3382-1:2009 §4.3 (Clause 8 body not available). Bendat & Piersol, *Random
Data*, Ch. 9 (via Gille, UCSD SIOC 221A lectures 15–16; equation numbers
unverified). Open Sound Meter `src/source/union.cpp`, `src/remote/*` at
`1e08de2`; scipy `_spectral_py.py` at `78f6a6a`; pyfar `dsp.py` at `32512a3`.
This repo: `docs/dsp/2026-08-28-dual-fft.md` §2, §3, §5;
`docs/dsp/2026-09-05-mtw-l3.md` §5, §8; `core/tests/check_coherence_gate.cmake`;
`memory/a-default-must-be-run-through-the-gate-it-feeds.md`,
`memory/a-threshold-read-off-a-grid-is-that-grids-floor.md`,
`memory/a-fixed-defect-returns-through-the-silent-fallback.md`.
