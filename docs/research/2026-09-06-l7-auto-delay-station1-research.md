# Auto-delay + suggestions — station-1 research

*2026-09-06. Lane L7-DELAY, station 1. Read-only pass over the existing L2
delay finder, the L4a IR/polarity lane, published competitor practice, and one
piece of open-source code read directly (not summarised secondhand). No code
in this repo changed to produce this record.*

## What already exists, and what that means for scope

`rta::dsp::findDelayPhat` (`core/include/rta/dsp/DelayFinder.h`,
`core/src/dsp/DelayFinder.cpp`) already **is** a complete GCC-PHAT delay
finder: linear (zero-padded) cross-correlation, relative regularisation floor,
integer peak search, parabolic sub-sample refinement, polarity flag, and a
normalised `peak` figure the operator can read as a trust indicator. It is
tested end-to-end against closed-form synthetic delays including a half-sample
case (`core/tests/test_delay_finder.cpp`), and the decision behind every one of
its choices is recorded in `docs/dsp/2026-08-28-dual-fft.md` §4 — PHAT over
Roth, relative over absolute regularisation, compensate-before-FFT ordering.
`GroupDelay.h` separately gives a per-bin phase-derivative delay from an
already-averaged coherent transfer function (§6 of the same record). L4a's
`Polarity` class (`core/src/ir/Polarity.cpp`,
`docs/dsp/2026-08-30-sweep-ir-l4a.md` decisions 6/6b) already solved a sibling
problem — "which arrival in a multi-arrival buffer is the one that matters" —
with a first-crossing-of-a-fraction rule and a measured, survey-based gate.

So **auto-delay is not a new correlator.** L7-DELAY's job is the layer above
the primitive that already exists: which of possibly several plausible
answers to trust, how to say so with a number rather than a vibe, and how to
turn a one-shot number into something that can run continuously without
duplicating machinery this repo already pays for once. That reframes every
question in the brief as "what wraps `findDelayPhat`", not "what replaces it".

## Decision → evidence table

| Question | What the evidence says | Source |
|---|---|---|
| Which peak is "the" delay when there are several? | Smaart's Delay Locator takes **the single highest peak of the impulse response**, no reflection handling stated. OSM's delay finder (`Deconvolution::maxIndex()`) does the same — plain argmax, no PHAT-style flattening. Neither is designed for a reflection-heavy capture; both assume the direct path is also the loudest. | Rational Acoustics support art. 150000191499; `psmokotnin/osm` `src/source/measurement.cpp:638-639`, `src/source/measurement.h:175` (`Deconvolution m_delayFinder`) |
| Sub-sample precision | Parabolic interpolation on the 3-sample peak neighbourhood is the standard cheap method; this repo already ships it and already states its unverified edge case (sign treatment near a correlation zero-crossing) in the header comment. Phase-slope (linear fit of unwrapped phase vs. frequency) is the frequency-domain alternative, already available in this repo as `groupDelaySeconds` per-bin, not yet reduced to one scalar. | `core/include/rta/dsp/DelayFinder.h:19-40`; `core/include/rta/dsp/GroupDelay.h`; general TDE literature (arXiv:2203.09723, "Estimation of Consistent Time Delays in Subsample...") |
| Confidence / trustworthiness of a suggestion | No surveyed product publishes a delay-specific confidence number. Smaart exposes coherence and "Magnitude Thresholding" (a per-bin gate on the reference channel, default −70 dBFS) as *general* TF-quality tools, not delay-specific ones (already documented in this repo's L6b research, Part A, G15/G20). GCC-PHAT's own `peak` (normalised correlation magnitude) is this repo's existing, unused-for-this-purpose proxy. | `docs/research/2026-09-06-l6b-station1-research.md` Part A (Smaart Magnitude Thresholding, Coherence Blanking); `DelayEstimate::peak` |
| One-shot vs. continuous | Both are real, named practices, not this project's invention. Smaart: a one-shot **Delay Locator** button plus a documented "measure twice" workflow (find delay un-compensated, apply, re-measure) — a genuinely discrete operation. Smaart also lists a **Delay Tracker** among its averaging-buffer protections (background drift watch). OSM: `m_delayFinder` re-transforms **every 25th accumulated block** (`(++m_delayFinderCounter % 25) == 0`) and only updates `m_estimatedDelay` when it changes — a real continuous-tracking implementation, read directly from source. | Rational Acoustics support art. 150000191499; `docs/research/2026-09-06-l6b-station1-research.md` Part A (G20, "Delay Tracker"); `psmokotnin/osm` `measurement.cpp:545-547,638-639` |
| Reflection / sub-vs-main separation | Nobody surveyed here does anything special for it in the delay finder itself. L4a's `Polarity` class solved an adjacent problem (first arrival vs. a louder later lobe) with a **first-crossing-of-a-fraction** rule, band-edge gating, and a bandwidth-based refusal — not a peak-height rule. The master plan already routes the *guided* sub/main workflow (G17) through G11's summation prediction rather than through the delay finder answering it alone. | `docs/dsp/2026-08-30-sweep-ir-l4a.md` decisions 6/6b; `docs/plans/MASTER-EXECUTION-PLAN.md` line 47 ("G17 ... P7 (UI front end over G11's summation prediction)") |
| Regularisation / robustness in reverberation | Already decided at L2: PHAT flat-weighting over Roth because it is robust in reverberation, with an optional band limit as PHAT's one mitigation for low-SNR narrowband content. L7 inherits this and does not re-litigate it. | `docs/dsp/2026-08-28-dual-fft.md` §4 |
| What needs playback | Nothing in the delay *math*. The workflow some products describe — "measure once undelayed, apply, measure again" (Smaart) — needs a repeatable capture, which is a sequencing concern, not a DSP one. Live tracking that must stay synchronised with an active source touches the output-path sub-lane only insofar as it needs a signal playing at all; the estimate itself is still a pure function of two captured spans. | Rational Acoustics support art. 150000191499; this lane's fixed premises |

## Where sources disagree

1. **What counts as "the" answer when the correlation has more than one
   candidate peak.** Smaart and OSM both silently assume the direct path is
   the loudest arrival and take the global maximum. Neither publishes a
   fallback for the case where it isn't (a subwoofer/main pair where the main
   arrives later but louder at the mic; a strong early reflection off a
   nearby wall). This project's own L4a lane already rejected an equivalent
   "take the biggest thing" rule for polarity, for a documented reason: a
   loud, late, opposite-signed lobe can outrank the true first arrival. There
   is no reason to expect delay estimation to be exempt from the same
   failure mode, and no competitor's public documentation says they checked.
2. **Whether delay estimation needs a confidence number at all.** Smaart
   ships one for polarity/coherence-adjacent judgements but not for the
   delay reading specifically — the Delay Locator button just moves a
   number, with no displayed trust figure. OSM ships none either — it
   updates `m_estimatedDelay` silently whenever `maxIndex()` changes,
   with nothing in the reviewed code path gating that update on peak height.
   This repo's own `DelayEstimate::peak` already carries the information
   competitors omit; the disagreement is that nobody in the survey treats
   the omission as a defect worth fixing.
3. **One-shot vs. continuous is presented by competitors as one feature with
   two buttons, not two designs.** Smaart's Delay Locator is a discrete,
   user-triggered action; its Delay Tracker (named in the averaging-buffer
   protection list, not separately documented) appears to be a background
   drift *watch*, which is a different job from "give me a number to apply
   right now". OSM collapses both into one mechanism — periodic
   re-transform, silent update — which conflates "keep tracking in case the
   room changed" with "tell me the number so I can commit it to
   `referenceDelaySamples`". Which of these to build (or both, cleanly
   separated) is exactly the ambiguity the L6b research's own lesson warns
   about: a name reused across products ("RMS average", "SSA") turns out to
   mean different things depending which one you read.
4. **Roth vs. PHAT for the correlator itself is already settled in this
   repo** (§4 of the dual-FFT record) but the survey confirms the split is
   real in the field: Smaart and REW use the IR peak (Roth-family,
   deconvolution-based), OSM's delay finder is also a `Deconvolution`
   object, not the `Union`'s coherence-aware code path. This project is
   already the outlier by using PHAT for this purpose, and station 1 found
   nothing that contradicts why (dual-fft.md §4's reverberation argument),
   only that nobody else in the survey does it this way.

## Candidate approaches

### The obvious one, and why it is rejected as written

**Copy OSM's shape: a second, independent correlator that re-runs
periodically on raw audio.** OSM's `m_delayFinder` is a second
`Deconvolution` instance with its **own** `2^16`-point FFT and its own
window function, entirely separate from the measurement engine's own
transfer-function accumulator, re-transformed every 25 blocks. It is the
most directly transplantable design — the source is public and the counter
logic is three lines — which is exactly why it is worth naming as the
default choice before rejecting it.

**Why it does not fit here.** This project already pays once, every hop,
for an accumulated `Sxy` inside `DualFftEngine` — that is the whole point of
the class. A second correlator duplicates the FFT, the windowing, and (per
the L6b research's own finding on N-stream duplication) the ring-buffer
bookkeeping, for a computation this project's engine has already done the
hard part of. It is also a second regularisation constant, a second window
choice, and a second place `subSample`'s known-unverified sign edge case can
surface — cost for no capability OSM's own version does not already lack
(no sub-sample, no confidence, no reflection handling). Transplanting the
*shape* of a competitor's feature without asking whether this codebase
already has the ingredient it re-derives is the mistake worth naming
explicitly, not just avoiding.

### Approach A — thin policy layer over the existing primitive (one-shot suggestion)

Add a `core/` function that calls `findDelayPhat` once over a caller-supplied
capture window and returns a **verdict**, not just a number: the existing
`DelayEstimate` plus an accept/refuse decision built from `peak` against a
threshold and (optionally) an explicit search-window bound so a physically
implausible lag cannot win. No new correlator, no new FFT, no new state.

- **Cost.** A threshold on `peak` is exactly the kind of scalar gate this
  project's own memory warns about (`a-threshold-read-off-a-grid-is-that-grids-floor.md`):
  whatever value a first survey picks will be the floor of that survey's own
  grid, not a law. It needs the same "measure across many synthetic cases,
  including a deliberately adversarial one" treatment L4a's polarity gate
  got, not a number chosen once and shipped.
- **What it does not solve.** Peak height alone does not distinguish "a
  clean, single, correct arrival" from "a clean, single, WRONG arrival" —
  a loud early reflection correlates just as tightly as the direct path
  does. This approach answers "is this delay measurement trustworthy" but
  not "is this the delay you actually wanted".

### Approach B — bounded/first-arrival search, mirroring L4a's polarity rule

Instead of (or alongside) A, restrict the correlation search to a lag window
the caller states is physically plausible (max mic-to-source distance in
samples), and within that window prefer the **first** peak crossing a
fraction of the window's own maximum over the tallest one — the same shape
L4a's `Polarity::arrivalFraction` already uses for a structurally identical
problem (first real arrival vs. a later, louder lobe).

- **Cost.** A second tunable fraction, and L4a's own history is the warning
  label: its equivalent fraction moved twice under survey (arrivalFraction
  0.2 → 0.5, band-edge gate replacing a bandwidth-in-octaves gate) before it
  stopped moving, and the record explicitly flags that a threshold read off
  one filter family climbs when a steeper one is tried. Nothing here says
  the direct-path/reflection boundary in delay estimation is any less
  filter-shape-dependent than the arrival-sign boundary was.
- **What it buys.** It is the only approach here that addresses disagreement
  #1 above rather than sidestepping it, and it reuses a rule this project has
  already survey-tested once, rather than inventing an unrelated one.

### Approach C — continuous tracking from the existing coherent phase, not a second correlator

For the "live" mode, do not re-run GCC-PHAT on raw audio on a timer (OSM's
shape, rejected above). Instead, fit a line to the **unwrapped phase of the
already-averaged, coherence-gated `H`** — the scalar reduction of
`groupDelaySeconds` this repo does not yet have, restricted to bins above the
coherence gate that already exists (`TransferSnapshot::coherence`,
`docs/dsp/2026-08-28-dual-fft.md` §3). Because `DualFftEngine` already
accumulates `Sxy` every hop, this mode costs one linear fit per publish, not
a second FFT engine.

- **Cost, stated plainly.** A phase-slope fit assumes one dominant, roughly
  linear-phase arrival across the fitted band. Multipath breaks that
  assumption directly — two arrivals of comparable strength produce a
  non-linear phase-vs-frequency curve, and a linear fit through it reports
  neither delay correctly. GCC-PHAT's broadband correlation, by contrast,
  is a real search and finds *a* peak even when the phase response is not
  a straight line; it degrades more gracefully under exactly the condition
  (reverberation) this project already chose PHAT to be robust against
  (§4 of the dual-FFT record). So this mode is well-matched to a clean,
  mostly-single-path capture (a loudspeaker measured close, or a
  post-alignment confirmation) and poorly matched to the reverberant,
  multi-arrival case the correlator was built for.
- **What it buys.** No second correlator, no second window, no second
  regularisation constant, and a number that updates every publish instead
  of every 25 blocks by construction, because it rides the accumulator that
  already runs. It also reuses the coherence gate rather than inventing a
  delay-specific one, closing part of disagreement #2 above for the
  continuous case specifically (though not for a genuine one-shot capture,
  which the gate's `minimumEffectiveAverages == 8` floor makes deliberately
  slow to arm from a cold start).

**Recommendation for the decision-record station.** A and B are not
mutually exclusive — B is A's peak-selection policy made more careful — and
both are one-shot. C is the "live" mode's natural DSP basis and is *cheaper*
than the obvious transplant, not merely different, which is the argument
that should carry the decision rather than taste. Whether C's fragility
under multipath is acceptable for a "live" number that a caller can always
re-derive with a one-shot A/B pass is exactly the kind of trade-off this
record should hand to station 2 rather than pre-empt.

## Proposed golden-vector strategy

The existing `test_delay_finder.cpp` already closed-form-pins the primitive
(exact integer delay across five values, sign, polarity, a half-sample
sub-sample case to 0.1-sample margin, noise degrading `peak` without moving
it, band-limit behaviour, relative-regularisation level-independence). L7's
own tests should not repeat that ground; they should pin the layer this
lane actually adds:

1. **Reflection-rejection closed form (Approach B).** Synthesise
   `y[n] = x[n-D1] + a·x[n-D2]` with `D2 > D1` and `a` chosen so the
   reflection's correlation peak is taller than the direct path's (this is
   constructible exactly, not measured — the two peaks' relative height is
   a function of `a` alone for uncorrelated-noise `x`). Assert the policy
   returns `D1`, not `argmax`. This is the identity that would have caught
   the failure mode disagreement #1 names.
2. **Confidence gate, both directions (Approach A).** Reuse the existing
   noise-degrades-peak fixture (`core/tests/test_delay_finder.cpp`'s own
   "noise degrades the peak height" case) and assert the gate refuses below
   whatever threshold station 2 adopts, and accepts a clean case at the
   same threshold — the survey-then-argue treatment L4a's polarity gate
   received, not a number picked once.
3. **Continuous mode against the same closed form the dual-FFT engine
   already uses.** `docs/dsp/2026-08-28-dual-fft.md` §7 states `y[n] =
   g·x[n-D]` makes `H` exactly `g·e^{-j2πkD/N}` and group delay exactly
   `D/fs` — already the basis for `test_group_delay.cpp`. A scalar
   phase-slope delay reduction has the same exact closed form available:
   assert it converges to `D/fs` as `DualFftEngine::process()` accumulates
   averages, gated by the same coherence threshold. No golden file needed —
   this is provable, not measured.
4. **Sub-sample accuracy claim, restated honestly.** The 0.1-sample margin
   already shipped is real but narrow (one delay, one interpolation
   kernel). Before citing a sub-sample accuracy figure in a decision
   record, repeat the existing linear-interpolation construction at several
   fractional offsets (not just 0.5) and note where the parabola's known
   unverified sign-edge case (documented in `DelayFinder.h`'s own comment)
   would first become visible, rather than re-asserting the one case
   already covered.
5. **Golden vectors only where none of the above applies** — e.g. a
   reverberant room-impulse-response-shaped multi-arrival capture, which is
   not cheaply closed-form. `tools/gen_golden.py` (NumPy/SciPy) can
   synthesise a Fixed multi-tap "room" and record scipy's own
   `scipy.signal.correlate` / a reference GCC-PHAT implementation's answer
   for comparison, remembering the venv for that lives in the main checkout,
   not this worktree (`memory/build-toolchain-on-this-machine.md`).

## What "both modes" means here, stated plainly

Not two DSP methods — one method (GCC-PHAT, already built) plus a phase-slope
alternative for the continuous case (Approach C) — but two **operating
modes** a caller chooses between, named the way competitors name them:

- **Suggested delay (one-shot).** Smaart's Delay Locator: capture, correlate
  once, present a number with a trust signal, let the operator apply it.
  Maps to Approach A/B over the existing `findDelayPhat`.
- **Live tracking.** OSM's periodic re-estimate / Smaart's Delay Tracker:
  a number that keeps updating while the engine keeps accumulating, so a
  moved mic or a drifting clock shows up without a new manual capture. Maps
  to Approach C, riding the accumulator `DualFftEngine` already runs, with
  GCC-PHAT available as a fallback re-check rather than the continuous
  engine itself.

## Where the boundary runs

- **Pure `core/` delay math** (this lane's actual deliverable): the existing
  `findDelayPhat`; a peak-selection/reflection-rejection policy over it
  (Approach B); a confidence/accept-refuse wrapper (Approach A); a scalar
  phase-slope reduction over `groupDelaySeconds` gated by
  `TransferSnapshot::coherence` (Approach C). All take spans or
  already-accumulated spectra and return numbers. None needs a device, a
  clock, or a thread — the same pull-based shape §7 of the dual-FFT record
  already established as what makes CI proof possible with no sound card.
- **Output-path sub-lane (assumed interface, not built here).** Anything
  that must *play* a signal to get the capture in the first place: the
  "measure once un-delayed, apply, measure again" workflow Smaart's own
  documentation describes, and any live-tracking mode that needs to keep a
  generator running while it watches for drift. L7-DELAY assumes this
  exists and produces the two aligned spans; it does not build the
  playback path.
- **`app/` presentation.** The "Apply" action that writes the accepted
  delay into `DualFftEngine::Config::referenceDelaySamples` and resets;
  converting samples to milliseconds and (with G16's environment
  compensation, not this lane) to a distance in air; showing the trust
  signal as something other than a raw `peak` float; the UI difference
  between a one-shot button and a live-tracking toggle. None of this is
  DSP and none of it belongs in `core/`.

## Open questions for station 2

1. Which of Approach A/B/C actually ships, and whether B's fraction
   threshold needs the same multi-family survey L4a's polarity gate got
   before being trusted, or whether a bounded search window alone (no
   fraction rule) already closes the disagreement #1 failure mode cheaply
   enough.
2. What threshold on `peak` (or on a genuinely new figure) counts as
   "trustworthy" for a one-shot suggestion, and whether that number should
   be exposed to the caller as a scalar to gate on or left as a display-only
   figure — mirroring the L6b research's own finding that a scalar gate
   calibrated on one grid gets beaten by the next filter family it meets.
3. Whether the continuous mode (Approach C) should silently fall back to a
   fresh GCC-PHAT pass when coherence collapses (a mic moved, the signal
   dropped), and if so how that boundary is detected without re-deriving
   the "identically 1.0 with too few averages" trap the coherence gate
   already exists to prevent.
4. Whether G16 (environment compensation — temperature/humidity → speed of
   sound, delay-drift warning) is a consumer of this lane's live-tracking
   number or a separate input that modifies the *suggested* delay before
   it is shown; the master plan files it as "P2 (delay finder gains an
   optional environment input)", which reads as the latter but is not
   decided here.
5. Whether G17's guided sub/main alignment wizard (already routed through
   G11's summation prediction, not through this lane, per the master plan)
   needs anything from L7-DELAY beyond the raw suggested-delay number, or
   whether it is entirely G11's problem once the number exists.
6. Whether Approach B's "first plausible peak" policy and L4a's `Polarity`
   arrival rule should become one shared primitive (they solve the same
   shaped problem) or stay separate because delay's failure mode
   (a reflection at a different lag) and polarity's (an opposite-signed
   lobe at nearly the same lag) are different enough in practice —
   unexamined here, and worth fifteen minutes at the start of station 2
   before either is built twice.
