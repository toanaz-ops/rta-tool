# L7-EQ: auto-EQ + suggestions — station 1 research

*2026-09-06. Station 1 research, lane L7-EQ. Scope: the two auto-EQ modes,
filter allocation strategy, target definition without the blocked ISO 2969
corridor, null-avoidance, core/app split, and golden-vector strategy.*

## Corrections to the brief (read this first — per `the-parity-table-is-a-hypothesis`)

1. **Smaart/SysTune "EQ suggestion behavior"** — the brief asked to research
   this for comparison. No reachable source (AFMG's own SysTune page, Sound
   Design Live's tuning-workflow articles, prosoundtraining.com) attests an
   automated filter-allocation feature in either product. What SysTune ships
   is a **Virtual EQ**: the operator draws filters by hand against a captured
   measurement and sees the predicted result — a manual predictive tool, the
   same shape as this project's own G11 virtual processor, not a solver.
   Treat "Smaart/SysTune auto-EQ" as **UNVERIFIED — likely does not exist**,
   not as a competitor pattern to match. REW is the only surveyed product with
   a documented, automated filter-allocation algorithm.
2. **The two modes are already named in this repo.** `docs/specs/2026-08-28-interactive-tuning-visuals.md`
   (V1 §6, V2 §2) already specifies both and this research treats that as the
   binding naming, refined below rather than invented fresh:
   - **Auto EQ** ("Auto-solve"): one action, N filters (default 6, user-capped)
     computed and committed together, ghost-previewed before commit.
   - **Manual mode**: "ranked max-3 suggestions" the engineer accepts one at a
     time — "not a fallback... how an engineer keeps their own judgement in
     the loop." Below this is called **Suggest mode** for clarity against
     "manual EQ" (hand-drawing a filter with no solver at all, which is a
     third, solver-free workflow this tool already has via the virtual EQ).
3. **core/ has no peaking/shelf filter design yet.** Grepping `core/` for
   `peaking|shelf|allpass|cookbook` found nothing. `core/include/rta/dsp/Biquad.h`
   has the DF2T section and cascade math; `core/include/rta/dsp/ButterworthDesign.h`
   has closed-form Butterworth *band-pass* design for the IEC 61260 filter
   bank. The RBJ-cookbook peaking/shelf coefficient formulas the allocator
   needs are new work, not reuse.

## Decision → evidence table

| Decision | Answer | Evidence |
|---|---|---|
| Two-mode naming | **Auto EQ** (one-shot, N filters, default 6, capped) vs **Suggest mode** (ranked, top-3, accept-one-at-a-time, re-ranks) | `docs/specs/2026-08-28-interactive-tuning-visuals.md` L46-54, L76-81 (owner directive, already adopted — this research confirms rather than proposes it) |
| Allocation algorithm | **Hybrid**: greedy peak-picking for filter *placement* (fc, Q) + linear least-squares for filter *gain*, not global nonlinear optimization | REW help (`roomeqwizard.com/help/help_en-GB/html/eqwindow.html`) confirms REW itself is greedy, not global; AutoEq wiki (`github.com/jaakkopasanen/AutoEq/wiki/How-Does-AutoEq-Work%3F`) confirms it is global SLSQP with a penalty term — see "Where sources disagree" below for why hybrid, not either pure form |
| Target without ISO 2969 | Flat / auto-offset, tilt (dB/oct), user-drawn, reference-trace — all buildable now; only a **named X-curve tolerance table** is blocked | L5b block note in this lane's brief; `docs/specs/2026-08-28-interactive-tuning-visuals.md` §1 already lists "user-drawn" as a target type independent of any standard |
| Null avoidance | Two gates, not one: (1) coherence threshold — already the V1 machinery — excludes untrusted bins from the residual; (2) minimum-phase/excess-phase test (G24) — excludes bins whose dip is a cancellation artifact, not a correctable resonance | `docs/specs/2026-08-28-interactive-tuning-visuals.md` §"Auto-solver honesty rules"; `docs/dsp/2026-08-28-competitive-parity.md` G24 row ("feeds G11's 'EQ can fix this / EQ cannot' verdicts"); Dirac Research whitepaper "On Room Correction and Equalization of Sound Systems" (dirac.com) and REW's own Minimum Phase help page confirm the physical basis |
| core/ vs app split | core/: peaking/shelf coefficient design (RBJ cookbook), the two-stage allocator over plain arrays (freq, deviation dB, coherence, min-phase flag), ghost-response evaluation (reuses `BiquadCascade::attenuationDb`, already exists). app/: suggestion-chip UI, accept/reject/re-rank interaction, N-cap control, writing accepted filters to the virtual EQ, export (G10) | `core/include/rta/dsp/Biquad.h`, project CLAUDE.md's "core/ must never include a framework" rule |
| Golden strategy | Tier 1 (closed form) for coefficient design and greedy peak location; tier 3 (committed golden vector vs NumPy `lstsq`) only for the linear gain-solve step, following the existing `tools/gen_golden.py` convention | See "Golden-vector strategy" below |

## The two modes, precisely

**Auto EQ.** One action. The allocator runs to completion — greedy placement
of up to N filters (default 6, owner-configurable cap), then one linear
least-squares gain solve over the placed set — and the *entire* result lands
on the virtual EQ at once, with the predicted post-EQ trace drawn as a ghost
before anything commits. The engineer accepts the whole set, trims individual
filters by hand afterward, or discards it. This matches the brief's "full
auto" mode and is explicitly benchmarked in this repo's own docs as
"REW-class but live" (`interactive-tuning-visuals.md` §6).

**Suggest mode.** Not REW's shape. REW's "Match response to target" also runs
to completion and hands back a full filter table you prune afterward — it is
Auto EQ with a review step bolted on, not a distinct algorithm. This project's
Suggest mode is architecturally different: it runs **one greedy step**, shows
the top 1-3 ranked candidate moves (biggest residual deviation first) as
suggestion chips, and *stops*. Accepting a chip commits that one filter,
recomputes the residual, and re-ranks — the next suggestion reflects what the
engineer just did. This is a live, interactive top-of-queue view of the same
greedy sequence Auto EQ would run to completion unattended. The design intent
recorded in `interactive-tuning-visuals.md` ("keeps their own judgement in the
loop... same math underneath, one flag") is best served by this incremental
form, not by REW's batch-then-prune form — pruning after the fact requires the
engineer to reason about interactions between filters that have already been
computed together; accepting one-at-a-time means every suggestion is judged
against the room as it actually now measures.

## Filter allocation: three candidates

### A. Greedy residual-reduction (REW's family)

Repeat up to N times: find the frequency bin (within the coherence-trusted,
min-phase-passed region) with the largest |deviation| from target; place a
peaking filter at that frequency; choose Q from the local width of the
deviation (narrow deviation → high Q, capped — REW caps boost-filter Q so the
60 dB decay time stays under ~500 ms, a stability-motivated bound, not an
audio one); choose gain to zero the deviation at that frequency, clamped by a
max-boost setting; subtract the filter's modeled response from the residual;
repeat.

*Pro:* deterministic, explainable ("this filter targets that peak"),
naturally incremental — each step is exactly one Suggest-mode chip. *Con:*
order-dependent and locally greedy — placing filter 1 can create a new local
max near filter 2's future site that a globally-aware method would have
avoided, and it can require more filters than a jointly-fit set to reach the
same flatness target.

### B. Global constrained optimization (AutoEq's family)

Fix N, then minimize a weighted-MSE-plus-penalty objective (AutoEq's penalty
term punishes high-Q/high-gain filters by their maximum slope, using linear
regression on the slope for speed rather than a true derivative) over all N
filters' (fc, Q, gain) simultaneously, via a general nonlinear solver
(AutoEq uses SciPy's `fmin_slsqp`).

*Pro:* better global fit for a fixed filter count; the penalty term is a
principled way to discourage ringing. *Con — and this is the one to argue
against, because it is the "obvious" answer given the brief points at AutoEq
by name:*

1. **It fights the project's own verification standard.** A general nonlinear
   constrained optimizer has no closed form. Its result depends on the
   specific solver, its convergence tolerance, and its initial guess. Proving
   it "correct" means either (a) freezing a golden vector against one
   specific SciPy version's SLSQP output — fragile, and the kind of "whatever
   the code printed" pattern CLAUDE.md explicitly forbids unless justified —
   or (b) writing and proving a *custom* nonlinear solver in `core/` from
   scratch, which is a large undertaking with its own correctness burden, not
   a small filter-allocation feature.
2. **AutoEq's domain has no room modes.** AutoEq corrects headphone frequency
   responses: near-field, no reflections, effectively minimum-phase-clean.
   Its target-curve and anti-ringing machinery transfers to this project, but
   its "don't chase every dip" problem is a *smoothness* problem, not a
   *null-avoidance* problem — AutoEq never has to decide "is this dip real or
   a cancellation artifact" because its input data structurally can't produce
   that artifact. Copying AutoEq's optimizer buys none of this project's
   actual hard problem.
3. **A global solve doesn't suit Suggest mode.** There is no natural "first
   suggestion" from a joint N-filter optimization — you get all N or none.
   Retrofitting an incremental UI onto a batch optimizer means re-solving the
   whole problem after every accepted chip, which is both slower (must run on
   the analysis thread per the RT-safety rule, but still a live-interaction
   latency budget) and throws away the "what did my last move do" mental
   model the honesty-rules section is built around.

### C. Hybrid: greedy placement + linear gain-solve (recommended)

Use greedy residual-reduction (A) to pick each filter's *(fc, Q)* — cheap,
deterministic, and it is exactly the Suggest-mode step. Once a set of N
(fc, Q) pairs is fixed (either mid-sequence for a Suggest-mode preview, or
the full N for an Auto EQ run), solve for the N *gains jointly* by linear
least squares: for a fixed fc/Q, a peaking filter's attenuation-in-dB is
*not* exactly linear in gain-in-dB, but the standard RBJ-cookbook
approximation (linearizing around each band's own contribution) reduces the
gain solve to `A·g = d` where `A` is an N×M matrix of each filter's unit-gain
response sampled at M frequency bins, `d` is the residual deviation vector,
and `g` is solved by the normal equations `g = (AᵀA)⁻¹Aᵀd` — a closed-form
linear-algebra step with no iteration.

*Pro:* keeps A's determinism and Suggest-mode fit, but recovers some of B's
"filters not fighting each other" benefit for the *gains*, which is where
independent per-filter greedy solving is weakest (each greedy step zeros its
own bin without accounting for what the other N-1 filters already contribute
there). *Con:* the linear-least-squares gain step is only exact for filters
that don't overlap much in frequency; heavily overlapping bands need a
damping/regularization term (ridge regression) to avoid an ill-conditioned
`AᵀA` — a known, provable extension, not a new research problem.

This is the recommended approach. It is smaller to build, smaller to prove
correct against the project's own verification standard, and it produces the
two modes as one algorithm run to two different stopping points rather than
two separate implementations.

## Where sources disagree

- **REW vs AutoEq on algorithm family.** REW: greedy, order = largest
  deviation first, individual/overall max-boost caps, Q capped by a 500 ms
  60 dB decay-time rule, shelf filters allowed only where the response clears
  the target by ≥0.5 octave at a band edge (`roomeqwizard.com/help/help_en-GB/html/eqwindow.html`).
  AutoEq: global SLSQP with a slope-penalty regularizer, no documented
  boost/Q hard caps beyond the penalty term and configured bounds
  (`github.com/jaakkopasanen/AutoEq/wiki/How-Does-AutoEq-Work%3F`). These are
  genuinely different algorithm families solving visibly different problems
  (live PA/room correction vs offline headphone correction), not two
  descriptions of the same method.
- **REW vs DRC-FIR on what "don't boost a null" means.** REW's guidance
  (community-documented, not the algorithm itself) is behavioral: cap
  individual/overall boost so a null-chasing filter can't run away. DRC-FIR
  (`drc-fir.sourceforge.net/doc/drc.html`) bakes this into the math itself —
  "frequency response dip limiting of the minimum phase component to prevent
  numerical instabilities during the inversion step" — a hard mathematical
  clamp, not an operator-set ceiling. DRC's approach is closer in spirit to
  this project's coherence/min-phase gating (a structural refusal to judge),
  but DRC is a FIR-inversion engine producing arbitrary-length filters, not a
  small set of parametric biquads a console can carry — not directly portable
  to G10's export requirement.
- **Smaart/SysTune claim in the brief vs what's attested.** See "Corrections
  to the brief" above — this is the sharpest disagreement: the brief assumed
  a comparison point that does not appear to exist in reachable sources.

## Target definition without ISO 2969

The blocked resource is specifically the **X-curve tolerance table** (ISO
2969 / SMPTE ST 202) — a *cinema calibration* corridor. It is not the only
way to define an EQ target, and this lane does not need it to ship a working
Auto-EQ:

**Available now, no purchase needed:**
- **Flat.** 0 dB reference, or the V1 auto-offset already specified (least-
  squares fit of overall gain over a user-chosen anchor band) applied before
  judging deviation. No new research needed — this is arithmetic.
- **Tilt.** A single dB/octave slope parameter around a pivot frequency (the
  house-curve shape many PA engineers dial in by ear — "pink slope",
  "smiley", etc.) — a closed-form target, one number.
- **User-drawn.** Freehand or point-based target curve, already named in
  `interactive-tuning-visuals.md` §1 as a V1 target type independent of any
  standard.
- **Reference trace.** Use a previously captured measurement (another mic
  position, an earlier tune, a reference loudspeaker) as the target — this is
  P5's trace library (`docs/dsp/2026-08-29-display-layer-l5c.md`) doing
  double duty, no new machinery.

**Blocked, and must stay explicitly labeled as blocked in the spec:**
- Any target claiming conformance to the ISO 2969 / SMPTE ST 202 X-curve
  tolerance table specifically (G5 in the parity ledger).
- Any named commercial curve (e.g. "Harman target") shipped as a first-class
  preset — the published numbers live behind an AES paper/purchase, and
  shipping an approximation while calling it "Harman" repeats exactly the
  mistake `the-parity-table-is-a-hypothesis` records: a claim about a
  standard that nobody in this project has independently verified against
  the primary source. If a Harman-style preset is wanted, it needs its own
  acquisition decision, not a guess baked into L7.

Auto-EQ's target input should therefore be typed as "any trace" (flat, tilt,
drawn, or reference), which is exactly what the allocator needs — it never
has to know whether the target came from a standard.

## Null-avoidance rule

Two independent gates, and the brief conflates them into one question when
they are structurally different:

1. **Coherence gate** (already built, V1 machinery): a bin below the
   coherence threshold is a measurement the tool does not trust *at all* —
   could be noise floor, could be a real dip, unknown. Exclude from both
   judgment and the allocator's residual. This is a data-quality refusal, not
   a physics claim.
2. **Minimum-phase / non-minimum-phase test** (G24, `docs/dsp/2026-08-28-competitive-parity.md`
   — "feeds G11's 'EQ can fix this / EQ cannot' verdicts"): even a
   *coherent, trusted* dip may be a cancellation artifact — the sum of a
   direct path and a delayed reflection producing a comb notch. Boosting it
   raises both paths' power without closing the notch (the physical
   literature is unambiguous on this: Dirac Research's whitepaper and REW's
   own Minimum Phase help page both describe room/reflection nulls as
   non-minimum-phase and therefore not invertible by any EQ, because if a
   response passes through zero it has no stable inverse, and because the
   null is position-dependent — a different measurement point sees the notch
   at a different frequency or not at all). The excess-phase decomposition
   already scoped for G24 is what distinguishes "this dip is minimum-phase
   and genuinely correctable" from "this dip is a comb artifact — flag it
   'phase problem, not EQ problem' and route to V2" exactly as
   `interactive-tuning-visuals.md`'s honesty rules already state.

**Sequencing implication, not previously flagged in the roadmap:** the
allocator's null-avoidance rule has a real dependency on G24 landing first
(it is scoped to P4; Auto-EQ solver work is scoped to P7, so the ordering
already works — but this is a *load-bearing* dependency, not an optional
enhancement. Shipping Auto-EQ before G24 exists means it can only use the
coherence gate, which does not catch every non-minimum-phase dip a coherent
signal can still produce.)

## core/ vs app/ split

**core/ (new work for this lane):**
- Peaking/shelf/(possibly all-pass, see open questions) biquad coefficient
  design — the RBJ Audio EQ Cookbook formulas (Robert Bristow-Johnson), in
  the same closed-form style as `ButterworthDesign.h`. Pure math, no
  framework, testable against the cookbook algebra directly.
- The allocator: takes `std::span<const float>` frequency bins, deviation-
  from-target in dB, coherence values, and a minimum-phase-pass flag per bin
  (all already-existing or already-scoped data shapes) and returns a list of
  `(fc, Q, gainDb, type)` filter descriptors. No knowledge of UI, of "chips",
  or of acceptance state.
- The ghost/predicted-response evaluation: already exists as
  `BiquadCascade::attenuationDb` (`core/include/rta/dsp/Biquad.h`) — reused,
  not rebuilt.

**app/ (presentation, interaction, RT-safety boundary):**
- Suggestion-chip rendering, ranking display, accept/reject/re-rank
  interaction loop for Suggest mode.
- The N-cap control and "commit all" action for Auto EQ.
- Writing accepted filters into the virtual EQ / trace model (P5's trace
  library) and wiring the ghost overlay.
- Export to external DSP formats (G10) — format-specific serialization has
  no place in core/.
- The allocator runs on the analysis thread (per this project's RT-safety
  rule — no allocation/FFT/locks in the audio callback), same as every other
  analysis-thread computation already in this project; nothing new here.

## Golden-vector strategy

Per the project's three-tier verification standard:

1. **Closed-form identity (tier 1).** Peaking/shelf coefficient design: the
   RBJ cookbook gives exact algebraic formulas from `(fc, Q, gainDb, fs)`;
   assert `Biquad::Coeffs` fields against that algebra directly, the same
   pattern `test_biquad.cpp` already uses for the DF2T impulse response
   (`core/tests/test_biquad.cpp` lines 20-46) and `ButterworthDesign` uses
   for its zpk chain. Greedy placement's *bin selection* is also tier 1: for
   a synthetic deviation curve with a single closed-form peak (a parabola or
   a known Gaussian), the argmax is analytically known — assert the greedy
   step lands on it exactly.
2. **Published standard (tier 2).** Not directly applicable to the allocator
   itself (there is no IEC/ISO standard for "how to pick EQ filters"); tier 2
   applies transitively through the inputs the allocator consumes — the
   coherence values and dB deviations it reads are already governed by
   IEC 61672-1/61260 elsewhere in this codebase.
3. **Golden vector (tier 3), narrowly.** Only the linear least-squares
   gain-solve step needs this: generate a synthetic deviation curve and a
   fixed set of (fc, Q) pairs in Python, solve `g = lstsq(A, d)` with NumPy,
   commit the inputs and NumPy's answer under `core/tests/golden/` following
   the existing `tools/gen_golden.py` convention (same directory, same
   generator-script pattern as the rest of the repo). **Caution from memory
   `a-gen-script-runs-the-moment-you-invoke-it`**: any new `tools/gen_*.py`
   for this must gate on `if __name__ == "__main__"` / argparse before this
   lane builds it — the existing scripts do not, and `--help` has overwritten
   a golden before.
4. **Explicitly labeled regression locks.** Anything that can't be reduced to
   1-3 (e.g., "does the full greedy-then-lstsq pipeline converge to a
   flatter response than the input, for a representative multi-peak
   deviation curve") should be committed as a labeled regression lock per
   CLAUDE.md's instruction, not dressed up as a correctness proof.

## Open questions (for the owner / next station)

1. **All-pass filters.** The brief's filter list says "parametric/shelf/etc." —
   should the Auto-EQ *EQ* solver (V1) ever propose an all-pass move, or does
   phase-only correction belong exclusively to V2 (phase-alignment, auto-
   delay)? This research found no competitor precedent for an all-pass move
   inside an *amplitude*-target EQ solver — leaning toward "V2 only," but the
   brief's "etc." leaves this open.
2. **N default and cap.** `interactive-tuning-visuals.md` says "default 6,
   user-capped" but doesn't say the cap's range or where it lives (session
   setting? per-target?). Needs an owner decision before implementation.
3. **Suggest-mode re-ranking granularity.** Confirmed above that accepting a
   chip should re-run the greedy step rather than serve from a precomputed
   list of 3 — but does declining/skipping a chip also trigger a re-rank, or
   only acceptance? Affects whether "skip" and "not yet reviewed" are
   distinguishable states in app/.
4. **Regularization strength for the lstsq gain step.** Candidate C's ridge
   term needs a default; no standard governs this, it is a judgment call to
   record with reasoning in `docs/dsp/`, not silently hardcoded.
5. **Harman-style (or any named) preset target.** Flagged above as blocked on
   sourcing, not on engineering — needs an owner decision on whether to
   pursue a license/purchase or drop the named preset and keep only
   flat/tilt/drawn/reference.
6. **Where does the min-phase test's threshold live?** G24 is out of this
   lane's scope, but the allocator consumes its output — the exact shape of
   that per-bin "pass/fail" signal (boolean? confidence score?) should be
   settled with whoever picks up G24, not assumed here.

## Sources cited

- REW help, EQ window / Match Response to Target: https://www.roomeqwizard.com/help/help_en-GB/html/eqwindow.html
- REW help, Minimum Phase: https://www.roomeqwizard.com/help/help_en-GB/html/minimumphase.html
- AutoEq wiki, "How Does AutoEq Work?": https://github.com/jaakkopasanen/AutoEq/wiki/How-Does-AutoEq-Work%3F
- AutoEq repository: https://github.com/jaakkopasanen/AutoEq
- DRC (Digital Room Correction) documentation: https://drc-fir.sourceforge.net/doc/drc.html
- Dirac Research, "On Room Correction and Equalization of Sound Systems": https://www.dirac.com/wp-content/uploads/2021/09/On-equalization-filters.pdf
- AFMG SysTune product page: https://www.afmg.eu/en/afmg-systune
- Sound Design Live tuning-workflow articles: https://www.sounddesignlive.com/get-started-with-sound-system-tuning/
- This repo: `docs/specs/2026-08-28-interactive-tuning-visuals.md`, `docs/dsp/2026-08-28-competitive-parity.md`, `docs/dsp/2026-08-28-dual-fft.md`, `docs/dsp/2026-08-29-display-layer-l5c.md`, `core/include/rta/dsp/Biquad.h`, `core/include/rta/dsp/ButterworthDesign.h`, `core/tests/test_biquad.cpp`, `memory/the-parity-table-is-a-hypothesis.md`, `memory/a-gen-script-runs-the-moment-you-invoke-it.md`
