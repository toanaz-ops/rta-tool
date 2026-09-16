# The alignment wizard, the virtual processor and the crossover surface (lane L7, sub-lane L7-ALIGN: G17 + G11 + G18, with relative polarity ρ)

*Decision record, station 2 of the pipeline in `docs/reports/README.md`.
Written 2026-09-06 on `1ad48da` from the station-1 report
`docs/research/2026-09-06-l7-alignment-wizard-station1-research.md`. Every
claim about the code below was re-read in the file it names; where the research
was wrong, §1 says so. This record consumes the L7-OUT interface
(`docs/dsp/2026-09-06-l7-output-path.md` §6, §11) and does not redesign it, and
it consumes `findDelayPhat` as shipped. No code is touched by this record.*

## 0. What L7-ALIGN is for, and the two rulings it is built under

An operator has a main and a subwoofer (or two sections of anything) that
overlap over a band. The wizard (G17) plays one, measures it, plays the other,
measures it, and tells the operator what delay and what polarity make the two
add up the way the crossover was **designed** to add up. The virtual processor
(G11) lets the operator see that result before touching the rack. The crossover
surface (G18) is the picture both of them draw on.

Two rulings fix the shape of everything below; neither is this record's to
revisit.

1. **G17 is ASK-not-infer** (owner, 2026-08-30, in the L4a building session;
   `docs/dsp/2026-08-30-sweep-ir-l4a.md` "Owner decisions recorded" and the
   amended tier 3 under "What G21 promises an operator about a subwoofer";
   relayed in `docs/HANDOFF.md` and the L7 row of the master plan). The
   correct phase offset at a crossover is set by a topology chosen at design
   time — 0° for Linkwitz-Riley, 180° for 2nd-order Butterworth, 90° for
   odd-order Butterworth — and **no measurement recovers the designer's
   intent**. L4a measured that a correctly wired pair reads the wrong sign at
   some crossover orders; "the order-dependence is itself the finding". The
   wizard asks the topology. It never derives it from the traces, and §6
   shows that "maximise the measured sum" is that derivation in disguise.
2. **Relative polarity ρ is folded into this sub-lane** (owner, 2026-09-06,
   relayed to this station by the orchestrator; not yet written into
   `docs/HUMAN-QA-QUEUE.md` — the closeout must add the line). L4a recorded
   `ρ = |xcorr peak| / √(E₁E₂)` with one-grid figures and required
   re-derivation before any threshold ships; L4b §9 declined to do it. This
   record designs ρ (§7), uses it to reconcile the two polarity signals the
   codebase already carries and never reconciles (§7), and writes the
   threshold plan (§8) **without shipping a number**.

## 1. Provenance: what the research established, and what this record corrects

Confirmed by re-reading: `DelayEstimate::inverted` is the sign of the PHAT
correlation at its peak (`core/src/dsp/DelayFinder.cpp:129`) and its only test
negates the *same* pink-noise signal (`core/tests/test_delay_finder.cpp:50-58`);
`findPolarity` is the sign of the first arrival reaching 0.5 of the window
peak, gated on measured band edges ≤ 100 Hz and ≥ 8 kHz, and **refuses a
subwoofer** with `BandTooHigh` (`core/src/ir/Polarity.cpp:241-244`);
`Trace` stores `magnitudeDb` and `phaseRadians` as separate `float` arrays
(`app/src/trace/Trace.h:100-101`); the live `TransferSnapshot` carries complex
`h` beside them (`core/include/rta/dsp/TransferEstimator.h:65-67`);
`Biquad.h:107-108` computes `|H|` from real/imaginary locals and says it does
so *to avoid a `<complex>` dependency*; `ButterworthDesign` offers only
`bandPass` (`ButterworthDesign.h:41-52`); `coherence_gate_is_not_bypassed`
greps `core/` for any write to a field literally named `coherence`
(`docs/dsp/2026-09-06-multichannel-l6b.md` §1).

Four things the research got wrong or left unsaid, corrected here so nobody
builds on them:

1. **D1/D6 have the Butterworth-2 sum backwards.** At fc each BW2 output is
   −3 dB and they are 180° apart, so the sum *without* inversion is a **null**,
   and the sum *with* one output inverted is the **+3 dB bump** (Rane Note
   160; closed form in §3). The research says the un-inverted sum peaks +3 dB.
   A wizard built on that sentence would tell an operator to leave a notch in.
2. **The topology table is a lookup in the research; it is one closed form
   here.** Butterworth order N: HP leads LP by **N·90° at every frequency**
   (§3). The three rows of the ruling are instances of it, and the sign of the
   odd-order 90° depends on N mod 4 — which the wizard must know, because it
   also asks *which* source is the high-pass side.
3. **The research's regression fits unwrapped phase.** This codebase already
   ruled that unwrap is a display operation, ambiguous where coherence is low,
   with one bad bin propagating to every bin above it (`docs/dsp/2026-08-28-
   dual-fft.md` §6), and L6b found that every product that averages phase does
   so through the complex mean. §4 fits in the complex domain instead.
4. **Two captures can carry different applied delays.** `CaptureMeta::
   appliedDelaySamples` is per trace and L6b §5 gives each position its own
   delay. A phase difference between two traces with different compensation
   contains `2πf(D₁−D₂)/fs` of pure bookkeeping. The research does not
   mention it; §4 removes it before fitting.

## 2. Decision: the wizard asks four things and computes three

**Asked, never inferred.** (a) Which source is the high-pass side and which
the low-pass side (main/sub). (b) The crossover family and order:
`LinkwitzRiley{N even}`, `Butterworth{N}`; Bessel and others are out of scope
(§12). (c) **Whether the processor already inverts one output** — a second
predetermined fact the measurement cannot recover, for exactly the reason the
first cannot: 180° of wiring and 180° of topology are the same 180°. Three
answers: yes, no, unknown. (d) A seed for the crossover frequency (optional)
and which output the operator's processor can delay (either, main only, sub
only).

**Computed.** (1) The spectral crossover: the coherence-gated frequency where
`|H_A| = |H_B|`, interpolated between bins the way `Polarity.cpp:39-48` does,
the crossing nearest the seed if a seed was given, all crossings listed and
asked about if not. (2) The band fit of §4: a residual delay `τ*`, a constant
offset `φ₀`, and an agreement `R ∈ [0,1]`. (3) The comparison of `φ₀` against
the offset the asked topology predicts (§3), phrased as a delta with two
suggestions attached — "add τ* to the earlier source" and, when the delta is
within a stated tolerance of 180°, "one output is inverted relative to the
convention you named".

**The sequence, on the L7-OUT interface.** `soloOutput(A)` → capture `H_A`
past the coherence gate → `soloOutput(B)` → capture `H_B` → compute → show
the G18 surface with the predicted `H_A + H_B` → operator applies settings in
the rack → `routeOutput(A, true)`, `routeOutput(B, true)` (one signal on two
outputs is what §11 of L7-OUT promises) → capture the **measured** sum and
show it against the **predicted** sum. That last step is Smaart's "+6 dB says
aligned" reduced to what it can honestly be: a check that the linear
prediction holds, not an objective. A gap between predicted and measured sum
is drift, a level change or a wrong setting; it is never read as "the
topology was actually something else".

**Against the obvious flow — a single-bin phase read at fc with a delay
slider.** Rejected for the research's three reasons (a single bin is the least
trusted number this codebase produces; at one frequency a 180° residual from
`Δτ = 1/(2f)` and a polarity fault are indistinguishable; a steep filter can
pass through the target at fc and diverge a third-octave either side) and for
a fourth: it has no agreement figure, so it cannot say "this is not a
delay-plus-constant problem", which is the sentence a live show needs.

## 3. The topology → offset table, derived once and cited

**Closed form.** A Butterworth low-pass of order N is `L(s) = 1/D_N(s)` and
the matching high-pass is `H(s) = s^N/D_N(s)`, so `H/L = s^N` and on `s = jω`

    arg H(jω) − arg L(jω) = N · 90°,   for every ω, not only at fc.

A Linkwitz-Riley of order N is the Butterworth of order N/2 cascaded with
itself (Linkwitz 1976; Rane Note 160), so the same relation holds with the
same N. Every row below is this one line evaluated, and the owner's three
rows are the cases N = 2 (180°), N odd (quadrature) and LR after its
canonical inversion (0°).

| Topology (order N) | Raw HP−LP offset, all f | Designed convention | Designed sum at fc | Wizard's expected `φ₀` |
|---|---|---|---|---|
| LR-N, N ≡ 0 mod 4 (LR4, LR8) | 0° | no inversion | 0 dB, allpass (`(1+s⁴)/D²`, magnitude `(1+ω⁴)/(1+ω⁴) ≡ 1`) | 0° |
| LR-N, N ≡ 2 mod 4 (LR2, LR6) | 180° | **one output inverted in the canonical network** | 0 dB, allpass (`(1−s²)/(1+s)²` for LR2) | 0° if the processor inverts, 180° if it does not — question (c) |
| BW-N, N ≡ 0 mod 4 (BW4, BW8) | 0° | none | **+3 dB** bump (each −3 dB, in phase) | 0° |
| BW-N, N ≡ 2 mod 4 (BW2, BW6) | 180° | one output inverted, historically | null without inversion, +3 dB with | 180° or 0° per question (c) |
| BW-N, N ≡ 1 mod 4 (BW1, BW5) | +90° | either | 0 dB either polarity (`(1±s³)/((s+1)(s²+s+1))` is allpass both ways) | +90° or −90° |
| BW-N, N ≡ 3 mod 4 (BW3, BW7) | −90° | either | 0 dB either polarity | −90° or +90° |

Offsets are `arg(H_HP-side) − arg(H_LP-side)`, HP relative to LP, which is why
the wizard asks which source is which: 0° and 180° are symmetric under that
swap, ±90° is not. "Designed sum" is the electrical sum of the ideal filters —
a real pair with drivers, horns and placement will not read it exactly, and the
wizard never treats the designed figure as a pass mark, only as the line on
the plot (§6).

**Literature.** Rane Note 160, "Linkwitz-Riley Crossovers: A Primer"
(Bohn) and Note 107 "Linkwitz-Riley Active Crossovers Up to 8th Order" state
the in-phase property of LR4/LR8, the 180° of LR2 with the network inversion,
the BW2 null/+3 dB pair, and the odd-order allpass sum. Linkwitz, "Active
Crossover Networks for Noncoincident Drivers", JAES 24(1), 1976, is the
derivation. The station-1 research confirmed the ruling's rows against Rane
and found no contradiction; this record adds that the rows are one identity,
so a fourth row for a new order is arithmetic, and a row for a new *family*
(Bessel) is not — Bessel's HP/LP offset is not a constant in f, and adding it
is a station-2 pass, not a table edit (Rane Note 147).

**The L4a wrong-sign finding, read through this table.** L4a measured a
correctly wired pair reading the opposite sign at orders 2 and 4 and the
correct sign at 1 and 8, and noted the two sessions got different sets. The
identity predicts wrong-sign at N ≡ 2 mod 4, right-sign at N ≡ 0 mod 4, and an
*undefined* sign in quadrature for odd N (the cross-correlation at the aligned
lag is `cos 90° = 0`; the peak lands a quarter-cycle to one side with whichever
sign that side has). Order 4 wrong-sign is **not** reproduced by the identity
under the `s^N/D` convention; a processor whose "HP" is `(−s)^N/D`, or a main
that is itself band-passed, would reproduce it. This record does not know
which the L4a fixture was, and does not need to: the wizard asks question (c)
precisely because conventions differ.

**SETTLED 2026-09-15 by an independent probe** —
`docs/research/2026-09-15-l7-align-order4-probe.md`, script
`tools/probe_align_order4.py`, CI lock `core/tests/test_align_order4_identity.cpp`.
The identity is exact analog and digital at every order 1–8 (worst deviation
`0.00e+00°` in the s-plane, `1.16e-11°` through second-order sections at
48 kHz), and the paragraph above guessed the mechanism correctly: a **pair of
band-pass boxes** — the sub band-passed below as well as the main band-passed
above — reproduces the L4a report at all four orders it names (right at 1,
wrong at 2, wrong at 4, right at 8) **under decision 6b's un-whitened
`ρ = |peak| / √(E₁E₂)` rule** (`docs/dsp/2026-08-30-sweep-ir-l4a.md:1181` on `main`, an
estimator this repo does not ship — `relativePolarity()` exists in no file),
while no matched-cutoff pair does at any of them. The offset is no longer
constant across the overlap (62.9° of spread at order 4 instead of 0°), which
stretches the correlation envelope until an oppositely-signed neighbouring lobe
outgrows lag 0 — the order-4 value *at lag 0* stays positive in every geometry,
exactly as the identity says. **The correlator this repo does ship — PHAT
`findDelayPhat`, `DelayFinder.cpp:34` — does NOT reproduce the L4a pattern**: on
the identical pair it reads the mirror of it, right at order 4 and wrong at
order 8, because whitening reweights the overlap band and moves the winning
lobe. Two correlators disagreeing about polarity on one unchanged pair is why
§13's ruling bans reading a topology sign off **any** correlation peak rather
than preferring one of them. A phase-sign or `conj`-placement
convention is ruled out as the cause: conjugation negates the offset, and
−0° = 0° and −180° = 180°, so **no even-order reading is reachable by a
convention flip**. §8 step 4 keeps the cross-system cells as expected refusals,
but it no longer has an attribution to settle.

## 4. Decision: the band fit is a complex-domain delay search with a circular-mean intercept, weighted by the summation cross-term

**Decision.** Over the bins `k` in a window about the spectral crossover, form
the unit relative response

    R_k = H_A,k · conj(H_B,k) / (|H_A,k| |H_B,k|)  = e^{j(φ_A,k − φ_B,k)}

after first multiplying `H_B` by `e^{+j2πf_k(D_B−D_A)/fs}` to undo the
difference in `appliedDelaySamples` (§1.4). With weights `w_k` (below), find

    τ* = argmax_τ | Σ_k w_k R_k e^{−j2πf_kτ} |     over a bounded τ range,
    φ₀ = arg( Σ_k w_k R_k e^{−j2πf_kτ*} ),
    R  = | Σ_k w_k R_k e^{−j2πf_kτ*} | / Σ_k w_k   ∈ [0, 1].

`τ*` is how much later the LP side arrives than the HP side (positive → delay
the HP side by `τ*`); `φ₀` is the constant offset left once that delay is
removed, compared against §3; `R` is the agreement — 1 when the band is exactly
"one delay plus one constant", falling as the band stops looking like that.
The search is a grid over τ followed by a parabolic refinement of the peak,
bounded by a caller-supplied range; local maxima at `τ* + n/f̄` (the cycle
ambiguity, below) are returned as ranked candidates with their own `R`.

**Sign convention, stated so it can be pinned.** If B arrives later by τ then
`H_B` carries `e^{−j2πfτ}`, `arg R_k = φ_A − φ_B` rises with f at slope
`+2πτ`, and the correction factor `e^{−j2πf_kτ}` flattens it. A pure-delay
fixture with a known sign is the test (§10.4); `memory/dual-fft-
conventions.md` item 2 records what a flipped delay sign costs.

**The weights carry no threshold.** `|H_A + H_B|² = |H_A|² + |H_B|² +
2|H_A||H_B| cos(φ_A − φ_B)`. The coefficient of the cosine — the only term the
relative phase can change — is `|H_A||H_B|`. So

    w_k = g_k · |H_A,k| |H_B,k|,     g_k = min(γ²_A,k, γ²_B,k) or 0 where either is absent.

This weight is largest where the two are equal in level and vanishes where one
dominates, which is Smaart's "within about 10 dB" heuristic with the 10 dB
removed: the weighting *is* the summation's own sensitivity to relative phase,
so no constant is read off any grid. The coherence gate `g_k` is L6b §3's
`u·γ²` shape with the trim set to 1; where either snapshot's coherence is
`nullopt` (below `minimumEffectiveAverages`) the bin contributes nothing, and
when every bin in the window is absent the fit is **refused with a reason**,
not computed over zero weight (`memory/a-placeholder-for-an-absent-result-
erases-its-state.md`).

**Against the obvious alternative — least-squares slope and intercept on
unwrapped `Δφ_k`.** It is the research's D2/D3 and it is what a textbook
suggests. It is rejected because it needs the unwrap this codebase has twice
ruled out of the engine (§1.3); because a least-squares intercept of angles is
not an angle (two bins at +179° and −179° average to 0°, the wrap failure
`dual-fft.md` §1 names); and because it produces no bounded agreement figure —
a residual variance in degrees has no ceiling and no meaning across windows.
The complex form is the same estimator when the phase is clean and a correct
one when it is not, at the cost of a bounded search instead of a closed-form
slope. That cost is a few hundred complex multiply-adds per candidate τ.

**The cycle ambiguity is stated, not hidden.** A phase fit over a band of
width `Δf` cannot distinguish `τ*` from `τ* + n/f̄` once `|n/f̄| > 1/(2Δf)`.
For a 40–160 Hz window that is ±4 ms unambiguous, and a sub/main pair can be
12 ms apart. Two honest sources of the missing integer: an IR arrival from a
sweep capture of each source (L4a's `Deconvolution::originIndex`, coarse and
unambiguous), or the operator. The wizard uses the first when both sources
have a sweep capture and asks otherwise. **It does not use `findDelayPhat` on
the sub's capture to pick the cycle:** PHAT on a band-limited signal is the
one weakness its own record names (`dual-fft.md` §4), and a narrowband
correlation peaks every `1/f_c` — the same ambiguity, moved.

**AMENDED 2026-09-16 by the Wave 3a build (branch `l7/align-wave3a-core`, tasks
A–F; case C5b in `core/tests/test_crossover_band_candidates.cpp`). The competing
delays this fit returns do NOT sit at `τ* ± n/f̄`, and the paragraph above should
not be read as saying they do.** The fit maximises

    |S(τ)| = | Σ_k w_k R_k e^{−j2πf_kτ} |

Write `f_k = f̄ + δ_k`. The mean factors out as `e^{−j2πf̄τ}`, a rotation of
modulus 1, which `|·|` discards — so **`|S(τ)|` depends on the SPREAD `δ_k` alone
and `f̄` cannot appear in the answer.** What is left is the Fourier transform of
the weight, so for a flat N-bin window the envelope is the Dirichlet kernel and
its local maxima sit near `(m + ½)/(NΔ)` — 8.26 ms apart for 121 bins of 1 Hz,
not the 10.0 ms `1/f̄` would give.

The two explanations differ by only 21% on this record's own 40–160 Hz fixture,
which no tolerance distinguishes, so C5b holds the WIDTH and moves the CENTRE:
40–160 Hz and 240–360 Hz are both 121 bins of 1 Hz, and the first competitor
measures **0.0118209 s in both** while `1/f̄` changes from 10.0 ms to 3.33 ms.

The `1/f_c` ambiguity is real — for a **time-domain correlation of a narrowband
signal**, which is a different estimator. The sentence above imported that
intuition into a complex band fit over a contiguous band. What survives
unchanged is the conclusion the paragraph was written for: the fit cannot
resolve the cycle on its own, the integer comes from an IR arrival or from the
operator, and `findDelayPhat` is not used to pick it. Only the SPACING was
wrong. See `memory/an-ambiguity-spacing-comes-from-the-bands-width.md`.

**The window width is a parameter, not a constant.** ±1 octave about the
spectral crossover is the proposed default, chosen because the cross-term
weight has already fallen by 20 dB or more there for any 4th-order pair. It
must be measured across the §3 table before it ships as a default, the same
treatment `PhatOptions::minHz/maxHz` and L4a's band edges received; a constant
baked into `core/` is refused by this record.

## 5. Decision: the virtual processor is five pure complex operations in `core/`, and a `VirtualTrace` in `app/` that is not a `Trace`

**Operations**, each `std::span<const std::complex<double>> in`, frequency axis
in, `std::span<std::complex<double>> out`, no state:

| Op | Formula | Closed-form check |
|---|---|---|
| delay τ (s, real, fractional) | `H'(f) = H(f)·e^{−j2πfτ}` | from `H ≡ 1`: `|H'| = 1`, `arg H' = −2πfτ` mod 2π at every bin — the `dual-fft.md` §7.1 identity |
| polarity | `H' = −H` | `|H'| = |H|`, `arg H' = arg H + π` at every bin |
| gain g (linear) | `H' = g·H` | `20log₁₀|H'| − 20log₁₀|H| = 20log₁₀ g`, phase unchanged |
| biquad cascade | `H' = H · Π_i H_i(e^{jω})` | at ω=0: `(b₀+b₁+b₂)/(1+a₁+a₂)`; at ω=π: `(b₀−b₁+b₂)/(1−a₁+a₂)`; and `20log₁₀|H_i|` equals **`−sectionAttenuationDb`** to 1e-12 (a consistency lock between two spellings of one formula, labelled as such — the field is an ATTENUATION, positive = down, so the sign is not optional; W0-R3 locked it and `core/tests/test_biquad_response.cpp:56-80` is where) |
| sum | `H_Σ = H_A + H_B` | two unit sources at relative phase φ: `|H_Σ| = 2|cos(φ/2)|` — +6.02 dB at 0°, +3.01 dB at 90°, 0 dB at 120°, null at 180°; §3's BW2 (null / +3.01 dB), LR4 (`≡ 0 dB`), BW3 (`≡ 0 dB` both polarities) from the analytic prototypes evaluated in the test itself |

Positive τ means this source arrives later — the sign `referenceDelaySamples`
and `DelayEstimate::delaySamples` already share. The frequency-domain delay is
exact for fractional τ; no interpolation is involved, so no interpolation
error is either.

**The biquad response lives in a new header.** `Biquad.h` avoids `<complex>`
on purpose and says so; putting a complex return in it contradicts a stated
design, and re-deriving the numerator/denominator in the wizard would be a
second spelling of the same formula. `core/include/rta/dsp/BiquadResponse.h`
computes `H_i(e^{jω})` from `Biquad::Coeffs`, includes `<complex>`, and the
consistency lock above keeps the two spellings from drifting.

**The lock's number, as planned and as shipped (noted 2026-09-15, test not
touched).** The plan's W0-R3 and its T3 row both say **1e-12**
(`docs/plans/2026-09-06-L7-wave0-impl-plan.md:39`, `:88`), and 1e-12 is what the
lock actually achieves: rebuilt at that tolerance it passes **384/384**
assertions, and at 1e-13 three of the 384 fail. The **shipped** test asserts
**1e-9** (`core/tests/test_biquad_response.cpp:78`, since `b2172b3`) with no
recorded justification — three orders of magnitude of slack the measurement does
not need. This record keeps the planned figure because that is what the lock
proves; tightening the test back is a `core/` change and belongs to whoever
owns that file next, not to a docs pass.

**The conversion boundary, once, in `app/`.** A stored `Trace` is dB + wrapped
radians; the live `TransferSnapshot` carries complex `h`. The app-side
`VirtualTrace { sources; std::vector<Op> chain; }` builds `H =
10^{dB/20}·e^{jφ}` from a `Trace` (or takes `h` from a live snapshot), runs
the chain in `core/`, and converts back for display — the "convert at exactly
one point" rule L4a decision 7 applies to degrees, applied to dB. A bin at the
`−120 dB` floor becomes `1e-6`, harmless in a sum. `VirtualTrace` has no
`CaptureMeta`, is not a `Trace`, and cannot reach `TraceLibrary` because the
library's API takes `Trace` — the type system, not a naming convention, keeps
a preview out of the file (research D8; `Trace.h:22-24`). The wizard's
persistent output is a **set of processor settings**, not a trace; an operator
who wants the aligned response on file captures it for real after applying
them. Where those settings persist is §12.

**Trust, not coherence.** Delay, polarity and gain do not change how much a
trace is trusted, so a `VirtualTrace` built from one source carries that
source's coherence unchanged. The **sum** has no coherence: it is not an
estimate of anything a cross-spectrum defines (L6b §4 makes the same point for
the spatial average). The sum carries a per-bin *trust* `min(γ²_A, γ²_B)` for
the L5c fade, under a field that is **not named `coherence`**, so
`coherence_gate_is_not_bypassed` stays true by construction rather than by
exemption.

## 6. Decision: the crossover surface plots four traces and one asked line; "maximise the sum" is refused

**Plotted**, on the L5c Bode layout (`docs/dsp/2026-08-29-display-layer-
l5c.md` §1, §4): `H_A`, `H_B` (as captured, or as previewed through pending
G11 ops), `H_Σ = H_A + H_B` recomputed on every change, and a **relative-phase
trace `arg(H_A·conj H_B)`** over the fit window with a horizontal line at the
`φ₀` §3 predicts for the *asked* topology and inversion answer. "At the target"
is visible as a property of a trace — flat, and on the line — rather than as
one number. The pre-alignment `H_Σ` stays as a ghost (the L6b reference-display
pattern), and the measured sum from §2's last step is drawn over the predicted
one. Reference lines at +6.02 dB and the topology's designed sum (§3) sit on
the summed-magnitude pane as marks, never as pass criteria.

**Against "maximise the measured summation" as the objective.** It is Smaart's
verification sentence promoted to a search, and it is the forbidden move with
the topology table deleted:

1. It re-derives topology. "Loudest sum" is a claim about what the designer
   intended; the ruling says the measurement cannot make that claim.
2. It is wrong whenever the target is not 0°. A BW2 pair correctly aligned
   sums to +3 dB; a search that keeps climbing walks the operator to +6 dB by
   undoing the inversion the network was built with. For any odd-order BW the
   sum is 0 dB at every delay that keeps quadrature, so the search's objective
   is flat and its answer is noise.
3. At one frequency it has the cycle ambiguity of §4 with no agreement figure
   to expose it.
4. It optimises one microphone position. The designed offset is what makes the
   sum hold across the room; the loudest sum at one seat is not.

"Minimise the sum's ripple across the band" is the same move in different
clothes and is refused for the same first reason. The surface *shows* the sum;
the operator reads it against the line the topology question drew.

## 7. Decision: ρ is a bounded, unwhitened same-system similarity; across a crossover no time-domain sign is authoritative, and the intercept is

**Definition.** For two time-domain spans `a`, `b` (deconvolved IRs from L4a,
or two captures of one source), over an arrival window `W` of each:

    r(l) = Σ_{n∈W} a[n]·b[n+l],   E_a = Σ_{n∈W} a²,   E_b = Σ_{n∈W} b²,
    l* = argmax_l |r(l)|,   ρ = |r(l*)| / √(E_a E_b) ∈ [0, 1],   sign = sgn r(l*).

The bound is Cauchy–Schwarz, and it is the one property the three dead L4a
gate variables lacked (`memory/a-threshold-read-off-a-grid-is-that-grids-
floor.md`, answer kind 1). The window is the arrival, not the whole capture,
because L4a's one-grid figure already shows the room tail is what pulls ρ down
(D/R −6 dB). Scale-invariant by construction; `ρ = 1` for `b = c·a` at any
`c ≠ 0`, with the sign of `c`.

**ρ is not `DelayEstimate::peak`, and its correlator is not PHAT.** `peak` is
the height of a *whitened* correlation: every in-band bin weighted to unit
magnitude, so a fully populated band reads near 1 and "a narrow analysis band
erodes it towards 0 without moving the peak" (`DelayFinder.h:42-46`). It
measures how much of the band agrees on a delay, not how alike two waveforms
are; nothing bounds it the way Cauchy–Schwarz bounds ρ. `inverted` is the sign
of that whitened peak, and with the default relative floor of `1e-10` every
out-of-band bin is amplified toward unit weight too, so on a band-limited
source `inverted` is the sign of a correlation dominated by bins that carry
noise unless `minHz/maxHz` were set. Its only test negates the same signal
(§1), which is the case L4a proved linearity covers and the case that says
nothing about two different systems.

**What each polarity signal may answer.**

| Question | Authoritative | Witness, shown greyed with its reason | Never |
|---|---|---|---|
| Absolute polarity of one full-range box | `findPolarity` when its gate passes (L4a 6b) | `inverted`, only with ρ (between reference and capture) above the §8 bar | — |
| Absolute polarity of a subwoofer | none — `findPolarity` refuses `BandTooHigh` by design | — | `inverted` promoted to fill the gap |
| Same system, before vs after; two units of one model | **ρ-gated sign**, new `relativePolarity()` | `findPolarity` on both, if both answer | `inverted` (same question, unbounded weighting) |
| LP side vs HP side across a crossover (the wizard) | **the fitted intercept `φ₀` against the asked topology** (§3, §4) | ρ and `inverted`, greyed, labelled "sign undefined across a crossover" | any time-domain sign as the answer |

**Why ρ does not arbitrate the wizard's question, stated as the failure it
would have.** Across a BW2 or LR2 crossover the two IRs are 180° apart at
every frequency by design; their unwhitened cross-correlation at the aligned
lag is negative with correct wiring, ρ is high, and the sign is confidently
wrong. Across any odd-order crossover the aligned-lag correlation is `cos 90°
= 0`; the peak falls a quarter-cycle to one side, the sign is whichever side
won, and ρ is depressed — undefined, not wrong. L4a measured the first case
and this record derives both. No re-weighting fixes it, because the
information that decides the sign is the designer's convention, which is
question (c). So ρ's reconciling role is bounded to where the two signals ask
the *same* question: for a same-system comparison ρ's sign **replaces**
`inverted` (a bounded version of the same measurement), and for a full-range
absolute reading ρ tells the UI whether `inverted` is a witness worth showing
beside `findPolarity`. When two eligible signals disagree the UI shows both
with their reasons and asks; it never picks silently (research D7).

**AMENDED 2026-09-16 by the Wave 3a build (case E8 in
`core/tests/test_relative_polarity.cpp`): ρ across a BW2 crossover is LOW, not
"high".** Measured on a correctly wired BW2 pair at fc = 100 Hz, 48 kHz:
**ρ = 0.0676**, sign negative, lag 0.

The closed form says why, and the test asserts it rather than the measurement.
`arg(H) − arg(L)` is 180° at every frequency, so `Re(L·conj(H)) = −|L||H|` at
every bin and, by Parseval,

    ρ = Σ_k w_k |L_k||H_k| / √( Σ_k w_k |L_k|² · Σ_k w_k |H_k|² )

which is a Cauchy–Schwarz ratio between two magnitude responses that barely
OVERLAP: `|L|` lives below 100 Hz while `|H|` spans 100 Hz to 24 kHz. The ratio
is of order `√(fc/(fs/2)) = √(100/24000) = 0.065`. Computed from
`cascadeResponse` in the fixture it predicts **0.0675611** against a measured
**0.0675611**.

That six-figure agreement is a property of THIS repo’s arithmetic, not a
portable invariant, and should not be read as one. Both sides of the comparison
run through `rta::dsp::BiquadCascade` in double precision over the same
coefficients, so they agree to nearly the last bit. An INDEPENDENT
implementation does not: a float32 Direct-Form-I cascade of the same filters
gives **0.0675563**, agreeing to about four significant figures. What the
closed form pins is the VALUE — order `√(fc/(fs/2))`, i.e. 0.0676 and not
“high” — and the test’s tolerance is set at **1e-3** for exactly that reason,
which is the right width for a claim about the physics rather than about one
cascade’s rounding.

**This strengthens the paragraph's conclusion rather than weakening it.** Where
the text above gives one reason ρ may not arbitrate the wizard's question (the
sign is confidently wrong), there are two: across a crossover ρ is **both low
AND wrong**. Nothing else in §7 changes — ρ still replaces `inverted` for a
same-system comparison, and the §8 survey still ships no threshold.

**One primitive, not two.** L7-DELAY's open question 6 asks whether its
"first plausible peak" and L4a's arrival rule should share code. ρ's window
`W` is L4a's arrival window (`originIndex`, `searchSeconds`), so
`relativePolarity()` takes two `Deconvolution`s and reuses that definition
rather than inventing a third notion of "the arrival".

## 8. The ρ threshold plan: two independent grids, one bounded variable, no number here

L4a's ρ figures — survives 20 dB SNR, a 10 ms offset, unit spread, D/R down to
−6 dB, sign correct throughout — came from one grid and one measurer, and L4b
§9 and the master plan both forbid shipping a threshold from them. The plan:

1. **Two grids, two authors, no shared script.** Each builds a NumPy model of
   two IRs and computes ρ and sign; neither reads the other's code first
   (`memory/a-threshold-read-off-a-grid-is-that-grids-floor.md`, last
   section). Scripts under `tools/probe_rho_*.py`, beside L4a's
   `probe_polarity_*.py`, with argparse (`memory/a-gen-script-runs-the-moment-
   you-invoke-it.md`).
2. **Axes**, all swept, none fixed at L4a's single values: SNR 10–40 dB;
   D/R −12 to +12 dB with a synthetic exponential tail; offset 0–30 ms; the
   L4a family/order grid (`butter`/`cheby1`/`ellip`/`bessel` orders 2–16,
   linear- and minimum-phase FIR); unit spread ±2 dB, ±10° between the two
   "identical" units; sample rates 44.1/48/96 kHz.
3. **Measured, not chosen.** The distribution of ρ over cells whose sign is
   correct and over cells whose sign is wrong. The candidate threshold is the
   lowest ρ above which **both grids** show zero wrong signs, published with
   the fraction of correct cells it refuses — L4a's margin gate was zero-lie
   and refused 74–88% of good boxes, which is the failure to check for.
4. **Cross-system cells are in the grid as expected refusals.** Each §3 row
   is built as a pair and its ρ/sign recorded, so the order-4 discrepancy of
   §3 is settled by measurement, and the record of what ρ does across a
   crossover is data rather than this record's derivation alone.
5. **Before adopting: what stops ρ from moving?** The bound stops it running
   away; nothing stops the threshold *inside* [0, 1] from being a grid floor.
   So it ships labelled **observed**, with both grids named, and the header
   comment says "moving this invalidates two surveys; re-run both".

Until step 3 is done twice, `relativePolarity()` returns ρ and the sign and
**no verdict**; the UI shows ρ as a figure and does not gate on it.

## 9. Boundary: what each layer owns

- **`core/`** (`rta::dsp`, spans and `std::complex<double>` in, numbers out):
  the five G11 ops; `BiquadResponse.h`; `spectralCrossover()`;
  `crossoverBandFit()` returning `{τ*, φ₀, R, candidates[]}` or a refusal
  reason; `relativePolarity()` returning `{ρ, sign, lag}` with no verdict.
  Nothing in `core/` knows what a topology is: the table of §3 is a lookup
  over an enum the operator chose, and it lives in `app/` for the same reason
  L4a kept "what this means for a loudspeaker" out of `findPolarity`.
- **`platform/`** — untouched; the wizard calls the L7-OUT §6 sequence.
- **`app/`** — the wizard state machine and its four questions; the topology
  enum and §3 lookup including question (c); `VirtualTrace` and the dB/complex
  conversion; the `appliedDelaySamples` reconciliation and the refusal when
  the two traces disagree on `sampleRate`, `fftSize` or reference (L6b §5's
  "positions must share one engine configuration", applied to a pair); the
  cycle-ambiguity question; the disagreement display of §7; `soloOutput` as
  L7-OUT §9 already places it.
- **`ui/`** — the relative-phase pane and the target line are drawn with
  primitives `az_ui` already has; no measurement vocabulary enters the module.

## 10. How CI proves this with no sound card

Closed forms first; one consistency lock, labelled; no new golden vector.

1. **Sum identity across φ.** Two unit sources at φ ∈ {0, 30, 60, 90, 120,
   150, 180}°: `20log₁₀|H_Σ| = 20log₁₀(2|cos(φ/2)|)` to 1e-9 in double
   (these are constructed values, not FFT-derived — `memory/float32-fft-
   precision.md` does not apply).
2. **The §3 table from analytic prototypes.** BW2 LP/HP evaluated at `s =
   jω/ω_c`: un-inverted sum at fc `< 1e-12`, inverted sum `+3.0103 dB`; LR4
   sum `0 dB ± 1e-9` at 64 log-spaced frequencies; BW3 sum `0 dB` for both
   polarities; raw HP−LP offset equals `N·90°` mod 360 at every frequency for
   N = 1..8.
3. **Each G11 op alone** against its table row, including the two biquad
   endpoints and the `−sectionAttenuationDb` lock at 1e-12 (the sign is
   W0-R3's; the shipped test's looser 1e-9 is noted in §5).
4. **Band fit, exact case.** `H_A ≡ 1`, `H_B = e^{j(φ₀ − 2πfτ₀)}` with
   `τ₀ = +3.7 ms`, `φ₀ = 180°`, unit coherence: `τ* = τ₀` within the parabolic
   refinement's own bound, `φ₀` within 1e-9 rad, `R = 1 − 1e-12`. Then with
   `τ₀ = −3.7 ms`: the sign flips — the fixture that makes a flipped
   convention fail (`dual-fft-conventions.md` item 2).
5. **Band fit, `appliedDelaySamples` reconciliation.** Same fixture with
   `D_B − D_A = 48` samples recorded in the metas: `τ*` unchanged after the
   correction, and off by exactly `48/fs` without it — made red once.
6. **Band fit refusals.** All coherence absent → refusal, not a fit; one bin
   present → refusal (a fit needs at least two distinct frequencies, and the
   record says so rather than letting one bin produce `R = 1`).
7. **Cycle candidates.** A 40–160 Hz window with `τ₀ = 9 ms`: the candidate
   list contains `τ₀` and `τ₀ ± 1/f̄` with `R` values that the test *reports*,
   not asserts — the fixture cannot know which is highest without asserting
   the implementation's own output.
8. **ρ identities.** `b = a`: `ρ = 1`, sign +; `b = −2a`: `ρ = 1`, sign −;
   `b = a[n−D]`: `ρ = 1`, `lag = D`; `b = a + n` at SNR `S` (power ratio):
   `ρ → √(S/(1+S))`, the same form as `dual-fft.md` §7.3's `γ² = S/(1+S)`,
   with a tolerance of the same shape.
9. **ρ across a synthetic BW2 pair, correctly wired** → sign negative, ρ high:
   asserted as the **documented failure**, so a future session that "fixes"
   ρ into agreeing with the wiring goes red and reads why.
10. **`VirtualTrace` cannot reach the library** — a compile-time check that
    `TraceLibrary` has no overload accepting it, and a runtime check that
    round-tripping dB → complex → dB through an empty chain is identity to
    float precision at every bin including the −120 dB floor.
11. **Guards.** `core_has_no_framework_deps` green with the new headers;
    `coherence_gate_is_not_bypassed` green with the sum's trust field present
    — and made red once by renaming that field `coherence`, then renamed back.

## 11. Rejected options and what each would have cost

| Rejected | Cost that decided it |
|---|---|
| Single-bin phase read at fc | no slope, no agreement; polarity and `Δτ = 1/(2f)` indistinguishable (§2) |
| Maximise measured sum; minimise band ripple | re-derives topology; +6 dB on a BW2 pair undoes its inversion; flat objective for odd orders (§6) |
| Least-squares fit on unwrapped phase | needs the unwrap two records keep out of the engine; angle mean wraps wrong; no bounded agreement (§4) |
| A "within 10 dB" band definition | a constant read off nothing; the cross-term weight expresses the same thing with none (§4) |
| Topology as a three-row lookup | the odd-order sign and the LR2/LR6 inversion are invisible to it; one identity generates every row (§3) |
| One topology question, no inversion question | 180° of convention and 180° of wiring are the same 180° (§2) |
| `inverted` as the wizard's polarity seed | whitened, unbounded, untested across systems; wrong-sign across BW2/LR2, undefined across odd orders (§7) |
| ρ as the arbiter across a crossover | same failure as `inverted` with a bound attached; the bound does not supply the designer's convention (§7) |
| Shipping L4a's ρ figures with an "observed" label | one grid, one measurer; the label did not save three L4a thresholds (§8) |
| Complex return added to `Biquad.h` | contradicts its stated reason for existing without `<complex>` (§5) |
| `Trace` with a fabricated `CaptureMeta` for previews | lies about what field it is; reaches the library (§5) |
| `findDelayPhat` on the sub capture to pick the cycle | PHAT's named weakness on band-limited signals; same ambiguity relocated (§4) |
| Feeding the measured sum back as an objective | a check of linear prediction is honest; an objective infers (§2) |

## 12. What this record does not decide

- Bessel and any non-Butterworth-derived family (§3).
- The fit window default (±1 octave proposed) and the τ search range default
  — both measured before shipping, both exposed as parameters regardless.
- The ρ threshold value (§8 owns the procedure; station 3 owns the number).
- Where the wizard's output — delay, polarity, gain per output — persists.
  It is processor state, not a trace; L7-OUT §12 already leaves the
  member→output map to schema 4, and this belongs beside it.
- Whether the LF fit runs on the fixed-FFT trace or the MTW trace when both
  exist (MTW has more bins below 200 Hz; MTW traces are live-only).
- How L7-DELAY's own station-2 trust figure for a suggested delay, once
  written, is displayed beside this record's `R`; this record consumes
  `DelayEstimate` as shipped and adds no field to it.
- A per-processor table of which presets carry the LR2/LR6/BW2 inversion —
  useful, a document, not code.

## 13. Open questions for a human

1. ~~**The order-4 wrong-sign attribution.**~~ **CLOSED 2026-09-15 by an
   independent probe — no owner input needed.**
   `docs/research/2026-09-15-l7-align-order4-probe.md`. The identity holds
   exactly; the L4a reading is a fixture artefact of applying decision 6b's
   **un-whitened** correlation-peak-sign rule across two systems with different
   passbands, and no sign convention in this engine can produce it at an even
   order. The shipped PHAT correlator does not reproduce it either — it fails
   at a *different* order on the same pair. Two consequences for the builder:
   keep §3's table, and never read a topology sign off **any** correlation
   peak, whitened or not — §4's complex band fit with its bounded `R` is the
   estimator, and `R` collapsing is how it reports "these two are not a matched
   pair". See §3's SETTLED note.
2. **A real sub/main pair.** Every check in §10 is synthetic. Whether `R`
   stays high enough on a real room capture for the intercept to be read,
   and whether ±1 octave is the right window on a real 24 dB/oct pair, need
   a person with a rack and a microphone. Same category as the EDT floor and
   the MTW fill question already open in `docs/HANDOFF.md`.
3. **Question (c)'s "unknown" branch.** When the operator does not know
   whether the processor inverts, the wizard can show both candidate lines
   (0° and 180°) and let the measured intercept sit on one — which *looks*
   like inferring. This record's reading is that it is not (the operator
   still chooses which line to believe and the wizard says so), but it is the
   one place the ruling's edge is close, and the owner should see it before
   it is built.
4. **The ρ ruling in `HUMAN-QA-QUEUE.md`.** Fold-in was relayed, not
   recorded. The closeout writes the line; the owner confirms it says what
   was meant.

## Sources

This repo: `core/include/rta/dsp/{DelayFinder,Biquad,ButterworthDesign,
GroupDelay,DualFftEngine,TransferEstimator}.h`, `core/src/dsp/DelayFinder.cpp`,
`core/include/rta/ir/{Polarity,Deconvolver}.h`, `core/src/ir/Polarity.cpp`,
`core/tests/test_delay_finder.cpp`, `app/src/trace/{Trace,TraceLibrary}.h`,
`docs/dsp/2026-08-28-dual-fft.md` §1, §4, §6, §7, `docs/dsp/2026-08-29-
display-layer-l5c.md` §1, §3–5a, `docs/dsp/2026-08-30-sweep-ir-l4a.md` 6b,
7, "Owner decisions recorded", "What G21 promises", `docs/dsp/2026-08-30-ir-
decay-l4b.md` §9, `docs/dsp/2026-09-06-multichannel-l6b.md` §1, §3–5,
`docs/dsp/2026-09-06-l7-output-path.md` §6, §9, §11, §12,
`docs/research/2026-09-06-l7-alignment-wizard-station1-research.md`,
`docs/research/2026-09-06-l7-auto-delay-station1-research.md`,
`docs/plans/MASTER-EXECUTION-PLAN.md` (L4, L7 rows), `docs/HANDOFF.md`,
`memory/{dual-fft-conventions,a-threshold-read-off-a-grid-is-that-grids-floor,
a-placeholder-for-an-absent-result-erases-its-state,float32-fft-precision}.md`.
External: Linkwitz, S., "Active Crossover Networks for Noncoincident
Drivers", JAES 24(1), 1976; Rane Note 160 "Linkwitz-Riley Crossovers: A
Primer" and Note 107 (Bohn), Note 147 (Bessel); McCarthy, *Sound Systems:
Design and Optimization*, phase-alignment chapter — as reached by station 1,
second-hand through Sound Design Live, and still to be read directly before
it is cited as primary. The Butterworth identities in §3 and §5 were worked by
hand for this record and are asserted by §10.2, not by citation.
