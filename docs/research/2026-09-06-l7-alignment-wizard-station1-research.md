# L7-ALIGN — alignment wizard, virtual processor, crossover surface

*2026-09-06. Station 1 research, lane L7-ALIGN (G11+G17+G18). Read-only pass:
literature, commercial-tool descriptions, and this repo's existing `core/` and
`app/` surfaces. No code touched. Companion sub-lanes: L7-DELAY (auto-delay,
assumed as an interface here) and whichever sub-lane owns relative-polarity
`ρ` (still unbuilt — see "Open questions").*

**The fixed premise, restated so this record cannot drift from it.** G17 is a
**phase question**, not a measurement question. The correct phase offset at a
crossover is set by a topology chosen at design time — Linkwitz-Riley (any
even order): **0°**; 2nd-order Butterworth: **180°**; odd-order Butterworth:
**90°** — and no algorithm here may try to infer that topology from the
measured traces. This is not this lane's opinion: it is `docs/HANDOFF.md`'s
record of a G21 tier-3 amendment the owner approved on 2026-08-30, and it is
restated in `docs/dsp/2026-08-30-sweep-ir-l4a.md` §"Owner decisions recorded"
after a building session measured that a correctly wired sub/main pair reads
the **opposite** sign at some crossover orders and the correct sign at
others — "the order-dependence is itself the finding." Everything below
designs the *asking* and the *previewing*, never the guessing.

---

## Decision → evidence table

| # | Decision this record argues for | Evidence |
|---|---|---|
| D1 | Topology target table: LR any order 0°, BW2 180°, BW odd-order 90° | Confirmed independently by Rane Note 107/160 and secondary summaries (below); matches the G21 tier-3 ruling already in `docs/dsp/2026-08-30-sweep-ir-l4a.md` |
| D2 | The wizard fits phase-difference vs. frequency over the crossover band (slope + intercept), not a single-bin reading | Bob McCarthy's stated method — "match the look of the phase traces" over a range, not a point (Sound Design Live summary of *Sound Systems: Design and Optimization*); a single bin can be low-coherence per the dual-FFT coherence floor (`docs/dsp/2026-08-28-dual-fft.md` §3) |
| D3 | Delay and topology-offset are separable via linear regression: slope → residual delay error, intercept mod 360° → constant offset to compare against D1's table | Follows from D4's closed form: a pure delay is `φ(f) = −2πfτ` (linear in `f`, zero intercept); a topology offset is a frequency-independent additive constant. No literature source states this decomposition explicitly for a GUI wizard — it is this lane's synthesis, flagged as such, not a citation |
| D4 | Applying delay to a trace is exact multiplication by `e^{-j2πfτ}`; polarity is `×(−1)`; a biquad cascade is `×H_biquad(f)`; summation is `H1+H2` | Standard LTI algebra; already partially present in this repo — `Biquad::attenuationDb` computes `\|H\|` from the same numerator/denominator this record needs in complex form (`core/include/rta/dsp/Biquad.h:107-123`); `GroupDelay.h` already carries `std::complex<double>` transfer functions through `core/` |
| D5 | Two equal-magnitude in-phase sources sum to +6.02 dB; equal-magnitude, opposite-phase sources sum to an exact null | `20·log10(2) = 6.0206…`; `1 + (−1) = 0`. Closed-form, no measurement needed |
| D6 | An ideal Linkwitz-Riley crossover sums to a **flat, 0 dB** response (`H_lp+H_hp≡1` by construction); a 2nd-order Butterworth crossover, summed without polarity inversion, peaks **+3 dB** at the crossover | Rane Note 107 (LR primer) and secondary sources (leaprofessional.com "Butterworth, Linkwitz-Riley, Bessel"); this repo already has `ButterworthDesign.h` to build the fixture |
| D7 | `findDelayPhat`'s `inverted` flag is a *second, independent* polarity signal from L4a's IR-based `Polarity` class, and the two can disagree | Read directly from `core/include/rta/dsp/DelayFinder.h:48-52` and the L4a record's decision 6b; no test in this repo currently checks agreement between the two |
| D8 | Virtual (previewed) traces must be distinguishable from measured ones by construction, not by a naming convention | `app/src/trace/Trace.h`'s own doctrine: "What the measurement WAS. Fixed at capture and never edited" (`CaptureMeta` comment, line 22) — a virtual trace is not that, and must not be shoehorned into the same type without a marker |

---

## Where sources disagree

- **Point-match vs. band-match.** The "obvious" retail description of phase
  alignment ("adjust delay until the phase traces line up") is a point
  statement at a glance but every source that explains *why* it works
  (McCarthy; Smaart's own delay-finder filter control, see candidate B below)
  actually means matching the **trace shape over a band**, because filter
  order changes the *slope* of phase near crossover, not just its value at
  one frequency. A wizard built from the retail description alone would
  under-serve steep crossovers.
- **Whether summation is the verification or the target.** Smaart's stated
  practice ("turn both on — +6 dB says aligned, no change or a drop says out
  of phase") uses summation as a **post-hoc check**. A tempting design
  (candidate C below) instead uses summation as the **objective to
  maximize**. These are not the same thing once the topology is not 0°: a
  correctly-aligned Butterworth-2 crossover reads as a +3 dB *bump*, not the
  same +6 dB a maximized-sum search would converge toward, and maximizing
  blind sum would silently re-derive "make it louder" instead of "hit the
  designed target" — which is a second way the forbidden premise (infer
  topology from measurement) sneaks back in through the objective function.
- **What "the crossover frequency" means.** Rane's primer and most retail
  descriptions treat it as the single filter corner frequency, chosen at
  design time. Smaart's own workflow instead **finds it empirically** — "the
  frequency range where the two magnitudes are within about 10 dB of each
  other" — because the measured acoustic crossover (where the two sources
  are actually equal-level in the room) can drift from the electrical corner
  due to driver sensitivity, horn loading, and placement. This repo's wizard
  should compute the spectral crossover the Smaart way (a pure `core/`
  magnitude comparison) rather than only accepting an operator-typed corner
  frequency, while still asking the operator for the *topology* — the two
  are independent unknowns and only one of them is recoverable from
  measurement.
- **REW / Smaart / SysTune do not converge on how a filter preview is
  scoped.** Smaart's delay-finder filter control (Sound Design Live,
  2026-fetched) applies a virtual band-pass to the *delay estimate only* — a
  narrow, single-purpose preview. REW's "All SPL" and convolution features
  apply a virtual EQ to a *stored trace* for general viewing. G11 as scoped
  here follows REW's shape (a general trace operation), because G18's
  crossover surface needs the same virtual traces the operator is
  previewing, not a delay-only side channel.

---

## D1, detailed: the topology → phase-offset table

| Topology | Target relative phase at crossover | Literature |
|---|---|---|
| Linkwitz-Riley, any even order (LR2, LR4, LR8, …) | **0°** (in phase); LP+HP sum to unity magnitude, flat, by construction | Rane Note 107/160, "Linkwitz-Riley Crossovers: A Primer"; Linkwitz's own derivation is that LR-N is Butterworth-N/2 cascaded with itself, which forces LP and HP to share both magnitude (−6 dB at fc, matching) and phase at fc |
| Butterworth, 2nd order | **180°** | Rane and multiple secondary sources agree the un-inverted BW2 outputs are 180° apart at crossover; summed without inverting one driver this produces a **+3 dB** peak, not a null — BW2 crossover networks historically invert one output's polarity in the network itself to get a flat sum despite the 180° per-filter reading |
| Butterworth, odd order (3rd, 5th, …) | **90°** | Follows from the same family: each additional pole rotates phase by 90° per order at the corner, and odd orders split the LP/HP phase difference to a quarter-cycle rather than a half |

This table is exactly the one already recorded in `docs/dsp/2026-08-30-sweep-ir-l4a.md`'s "Owner decisions recorded" — this pass independently confirmed it against Rane's primer rather than re-deriving it, and found no source that contradicts it. **Bessel and other families are out of scope**: the fixed premise only names LR/BW, and Rane's own note on Bessel crossovers (Note 147) states its phase behavior does not reduce to a fixed constant the way LR/BW's does — a fourth table row cannot be added without a fourth research pass.

---

## Virtual processor (G11): the operation set

All four operations are pure functions on a complex transfer function
`H: freq → ℂ`, sampled at the trace's existing bin frequencies. None of them
know about JUCE, a device, or a wizard — they belong in `core/`.

| Operation | Formula | Closed-form check |
|---|---|---|
| Apply delay `τ` (seconds) | `H'(f) = H(f) · e^{-j2πfτ}` | Starting from `H≡1` (flat, unity trace), `arg(H'(f))` must equal `−2πfτ` exactly (mod 2π) at every bin, and `\|H'(f)\| = 1` unchanged. This is the same identity `docs/dsp/2026-08-28-dual-fft.md` §5's pure-delay fixture already uses for `DualFftEngine` (`g·e^{−j2πkD/N}`) — reusable verbatim |
| Apply polarity | `H'(f) = −H(f)` | `\|H'\| = \|H\|` unchanged; `arg(H') = arg(H) + π` (mod 2π) at every bin, exactly, independent of frequency |
| Apply gain `g` (linear) | `H'(f) = g·H(f)` | `20·log10\|H'\| = 20·log10\|H\| + 20·log10 g`; phase unchanged |
| Apply a biquad cascade | `H'(f) = H(f) · H_biquad(f)`, `H_biquad` from the same `b0,b1,b2,a1,a2` `Biquad::Coeffs` this repo already has | Two routes, both cheap: (a) a golden vector from `scipy.signal.freqz` at the same coefficients (`tools/gen_*.py` convention, `core/tests/golden/`); (b) `attenuationDb` is *already* tested against a closed form for magnitude — the missing piece is only the **phase** half of the same numerator/denominator (`sectionAttenuationDb`'s `nRe,nIm,dRe,dIm` already exist as locals in `Biquad.h:114-117`, just not returned) |
| Complex sum of two sources | `H_sum(f) = H1(f) + H2(f)` | D5/D6 above: equal-magnitude in-phase → `+6.02 dB`; equal-magnitude opposite-phase → exact null (`\|H_sum\| < ε`); a real LR4 pair built from this repo's `ButterworthDesign.h` → flat magnitude across the crossover to within the filter's own design tolerance |

**Where the dB-magnitude+phase split matters.** `app/src/trace/Trace.h`
stores magnitude in **dB** and phase in **radians as separate arrays**, not a
complex pair (`Trace.h:75-84`). None of the four operations above are linear
in dB — they are linear in the complex (or linear-magnitude) domain. So the
virtual processor's app-side wrapper must convert dB→linear, build
`std::complex<double>`, call the `core/` function, then convert back,
exactly once per operation — the same "convert at exactly one point" rule
`docs/dsp/2026-08-30-sweep-ir-l4a.md` decision 7 already applies to radians
vs. degrees, extended to dB vs. linear.

**How a previewed trace is marked as virtual.** `CaptureMeta`'s own comment
is explicit that `Trace` describes "what the measurement WAS... fixed at
capture and never edited" (`Trace.h:22-24`). A virtual trace produced by G11
is not a capture at all — mutating a `Trace` in place, or constructing a new
`Trace` with a fabricated `CaptureMeta`, both lie about what field it is.
The candidate shape (app-level, not decided here) is a thin
`VirtualTrace { const Trace& source; std::vector<Op> chain; }` that computes
its complex data on demand and is never eligible for `TraceLibrary`
persistence — consistent with the standing rule that MTW and spatial-average
traces are **live-only** (`memory/MEMORY.md`'s L5/L6b amendments). This
keeps G11 from quietly inventing a second kind of "saved measurement."

---

## Crossover surface (G18): what is plotted

Three traces, magnitude and phase panes (reusing the Bode layer from L5c,
`docs/dsp/2026-08-29-display-layer-l5c.md`):

1. **H1** (main), as captured or as previewed with pending G11 operations.
2. **H2** (sub), same.
3. **H_sum = H1 + H2**, computed fresh every time either input changes — this
   is the one number the operator is actually trying to control.

Before/after: the wizard keeps the pre-alignment `H_sum` as a ghost trace
(same pattern L6b already ships for the average group's reference display —
`docs/dsp/2026-09-06-multichannel-l6b.md`) so the operator can see the
delta, not just the endpoint.

**Closed-form checks specific to the surface, not just the underlying ops:**
- Feed it two synthetic flat sources at a chosen relative phase φ: `\|H_sum\|`
  must trace `20·log10\|1+e^{jφ}\| = 20·log10(2\|cos(φ/2)\|)` exactly, which
  degenerates to D5's +6 dB at φ=0 and −∞ (null) at φ=180°. This one closed
  form covers the whole φ axis, not just the two endpoints, and is cheap to
  assert at a handful of intermediate angles too.
- Feed it a matched LR4 pair from `ButterworthDesign.h` (Butterworth-2
  cascaded with itself per channel, the standard LR4 construction) and
  assert the summed magnitude is flat (D6).

---

## Candidate wizard-flow designs

### Candidate A — single-point delay match (the obvious one, rejected as primary)

Ask for topology and crossover frequency; read the phase difference at that
one frequency bin; slide virtual delay until that one number equals the
topology's target; done.

**Why it looks right.** It is what the retail description of "phase
alignment" reduces to in one sentence, and it is the least code.

**Why it is rejected as the primary design.**
1. **Single-bin readings are the least trustworthy readings this codebase
   produces.** The coherence floor exists precisely because one frame's
   coherence is meaningless (`docs/dsp/2026-08-28-dual-fft.md` §3); a wizard
   that hangs a delay decision on one bin inherits that fragility even with
   many averages, because it still throws away every other bin's
   information.
2. **It cannot separate "wrong delay" from "wrong polarity."** At exactly
   one frequency, a residual 180° from a delay error of `Δτ = 1/(2f)` is
   indistinguishable from an actual polarity fault — both read as 180° at
   that bin. Only the *slope* across a band (does the offset stay near 180°
   as frequency moves, or does it rotate?) tells them apart, and candidate A
   never looks at slope.
3. **It matches the point but not the design.** A steep, high-order filter's
   phase can pass through the target value at the crossover frequency while
   diverging sharply a third of an octave either side — exactly the failure
   McCarthy's band-matching guidance exists to prevent.

### Candidate B — band-fit slope/intercept (recommended)

Ask topology, main/sub assignment, and (optionally) a starting guess for the
crossover frequency. Then:

1. Find the spectral crossover automatically — the frequency where `|H1|`
   and `|H2|` are closest (Smaart's own "within ~10 dB" heuristic,
   generalized to "closest"), restricted to bins both traces have
   coherence for.
2. Take the coherence-gated phase difference `Δφ(f) = φ1(f) − φ2(f)` over a
   window around that crossover (e.g. ±1/3 octave, a tunable, not a
   constant baked into `core/`).
3. Linear-regress `Δφ(f)` against `f`: the slope is a residual delay error
   (`τ_residual = −slope/2π`), the intercept mod 360° is the constant offset
   to compare against D1's table.
4. Suggest a virtual delay correction (seeded from L7-DELAY's GCC-PHAT
   estimate as the starting guess, since both answer "how far apart in
   time" — see D7's caveat about the two polarity signals disagreeing,
   which the wizard should surface rather than silently pick one).
5. Recompute the fit; when the slope is near zero, compare the intercept to
   the topology's target and report the delta directly, with a suggested
   polarity flip if the delta is near 180° away from target.
6. Show G18's crossover surface, before and after, with the two closed-form
   endpoints (D5) as reference lines on the summed-magnitude readout.

**Trade-off.** More code than A: a coherence-gated linear regression is a
new `core/` function, not a readout. It is the design this record
recommends because it is the only one of the three that can tell the
operator *why* a delay slider isn't converging (a rotating intercept says
"this isn't a delay problem, check polarity or topology"), which is the
actual failure mode a live show produces.

### Candidate C — maximize measured summation (rejected)

Skip the phase math. Sweep virtual delay (and optionally polarity) and pick
whichever setting maximizes `|H_sum|` at the crossover frequency, then
declare victory when summation is loud.

**Why it looks right.** It matches Smaart's own verification step
("+6 dB says aligned") and needs no topology table, no regression, no
`core/` complex math beyond the sum itself — the least design surface of the
three.

**Why it is rejected.** This is the forbidden move wearing a different hat.
"No topology table needed" is not a simplification, it is the wizard
quietly re-deriving topology from the measurement by treating "loudest sum"
as the ground truth — exactly what the G21 tier-3 ruling says a measurement
cannot do. It also gives the *wrong* answer whenever the target is not 0°:
a correctly-aligned BW2 crossover is a +3 dB bump, and a maximize-the-sum
search run past that point keeps climbing toward the same +6 dB a
0°-topology system would reach, walking the operator away from the design
the crossover was built to. Kept in this record, argued against rather than
silently dropped, because it is the second-most "obvious" design after A
and the reason to reject it is not obvious from its one-line description.

---

## Core / app boundary

| Belongs in `core/` (pure complex math, `std::span`/`std::complex` in and out) | Belongs in `app/` or `ui/` |
|---|---|
| Apply delay, apply polarity, apply gain, apply biquad (all four G11 ops) | Wizard state machine: which question is asked next, main/sub assignment, topology selection UI |
| Complex sum `H1+H2` | dB↔linear and Trace(mag,phase)↔complex conversion at the `VirtualTrace` boundary (pure math, but tied to the app-only `Trace` type, so it lives beside it, not inside `rta_core`) |
| Spectral-crossover finder (closest-magnitude frequency, coherence-gated) | Crossover-surface drawing (H1/H2/H_sum panes, before/after ghost trace) — reuses L5c's Bode layer |
| Band-limited linear regression of phase-difference vs. frequency (slope/intercept) | Comparing the fitted intercept against the topology table and phrasing the delta for the operator (a lookup + subtraction over an app-level enum, not DSP) |
| Biquad complex frequency response (extending `Biquad.h`'s existing magnitude-only path) | — |

The topology table itself (LR/BW2/BW-odd → 0/180/90) is a **constant lookup
over an enum the operator chose**, not a measurement — it can live in either
layer, but placing it in `app/` keeps `core/` from needing to know what a
crossover topology *is*, matching L4a's polarity gate, which also keeps its
"what does this mean for a loudspeaker" reasoning out of `core/` and states
only measurable quantities there.

---

## Open questions

- **Relative-polarity `ρ` is still unbuilt.** L4a recorded the formula
  (`ρ = |xcorr peak|/√(E1·E2)`) as a one-source number requiring
  re-derivation, and L4b's "what this record does not decide" section
  confirms it is still not done (`docs/dsp/2026-08-30-ir-decay-l4b.md`
  §9). The wizard's polarity suggestion (candidate B, step 4/5) needs
  *some* signal to seed the polarity question — this record uses the
  band-fit intercept, which sidesteps `ρ`, but a future session should not
  assume `ρ` exists just because L7-ALIGN needed something polarity-shaped.
- **Two disagreeing polarity signals (D7).** `findDelayPhat`'s `inverted`
  flag and L4a's `Polarity` class can answer differently because they are
  different measurements (broadband cross-correlation sign vs. first-arrival
  sign of an IR). No station-1 source in this project has reconciled them.
  This needs a human decision or a station-2 measurement pass, not a guess:
  which one (if either) feeds the wizard's polarity suggestion, and what
  the UI says when they disagree.
- **The regression window width (±1/3 octave in candidate B) is a proposed
  default, not a derived number**, and needs the same treatment L4a's band
  edges got — measured across filter orders before it ships as a constant,
  or exposed as a caller-supplied parameter the way `PhatOptions::minHz`/
  `maxHz` already are for the delay finder.
- **A live sub/main pair on a real system is the only way to close this
  out.** Every closed-form check above is synthetic (flat sources, designed
  filters). Whether the band-fit's intercept is *stable enough* on a real
  room capture — with real noise, real coherence gaps, a real crossover
  that is not exactly the topology the operator typed — needs a human with
  a real system, the same category `docs/HANDOFF.md` already lists open for
  EDT's B·T floor and the 5.5 s MTW fill question. Nothing in this record
  should be read as claiming otherwise.
- **Bessel and other crossover families are explicitly out of scope** (see
  D1) but will eventually be asked for by an operator's rack; the wizard's
  topology enum should be built so adding a family later is a table row and
  a station-2 pass, not an architecture change.

---

Sources consulted outside this repo: Rane Note 107/160 ("Linkwitz-Riley
Crossovers: A Primer") and Note 147 (Bessel); leaprofessional.com,
"Butterworth, Linkwitz-Riley, Bessel: Differences Explained"; Sound Design
Live, "Smaart Beta: Will the new filter control in the delay finder help
with your main+sub alignment?"; Sound Design Live, "How to phase align
main+sub in Smaart, REW, Open Sound Meter, SATlive, and Crosslite"; Bob
McCarthy, bobmccarthy.com ("Phase Alignment of Spectral Crossovers" —
figures only accessible, methodology summarized from the Sound Design Live
piece which cites McCarthy directly, flagged here as second-hand and worth
a direct read of *Sound Systems: Design and Optimization* before station 2
treats it as primary).
