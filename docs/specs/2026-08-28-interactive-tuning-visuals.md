# Interactive tuning visuals — design record

*2026-08-28. Owner directive, developed at station 2. This is the differentiator
layer: the parity ledger already noted that no surveyed product ships a
match score — here it becomes the product's spine. Depends on: P2 transfer
function, P5 target curves/traces, G11 virtual processor, G25 IR gating,
G16 environment input.*

## The principle all three views share

Tuning has a goal state; the display's job is to show DISTANCE TO GOAL, not
raw curves the engineer must difference in their head. Every view below is
"target + live + judgement", where judgement is colour with meaning:

- **match** = az::ui `ok` (LED green) — within tolerance AND trusted
- **miss** = az::ui `danger` (red) — out of tolerance, worst offenders ranked
- **untrusted** = dimmed + hatched, judged by NOBODY — where coherence is low
  or a band is under-resolved, the view must refuse to judge rather than
  judge on garbage. A wrong verdict destroys trust in every right one.
- Ice blue stays reserved (SODIUM RACK rule); it is NOT used for "match".

Colours bind in app/src/view/MeasureColours.h from az::ui tokens; az_ui itself
learns no tuning vocabulary.

## V1 — Target-match view (owner's #1)

Target trace (standard curves: flat, X-curve, house presets, user-drawn) +
live measured response. The engineer EQs — on our virtual EQ (G11) or on an
external desk/DSP — and watches regions turn green.

The machinery that makes it honest and smooth:

1. **Auto level offset**: least-squares fit of the offset over a user-chosen
   anchor band (default 400 Hz–4 kHz) so overall gain never masquerades as
   spectral match. Offset shown numerically (one-decimal dB), lockable.
2. **Tolerance corridor**: ±X dB around target (default ±3, per-region
   overridable — tighter mids, looser extremes). Drawn as a translucent band;
   judgement is against the corridor, not the line.
3. **Coherence gating**: bins below the coherence threshold are hatched and
   excluded from both judgement and score.
4. **Smoothing choice** decoupled from judgement display (judge on 1/6 oct by
   default; show 1/24 if the user wants detail).
5. **Time window / gating control** (G25): the same drag-gate that yields the
   quasi-anechoic view feeds this one, so the engineer chooses "judge the
   direct sound" vs "judge the room".
6. **AUTO EQ** (owner upgrade, 2026-08-28 — promoted from suggestion to
   solver): one action fits a full correction — N parametric filters
   (fc/gain/Q, N user-capped, default 6) optimised against the corridor over
   the coherence-trusted region, REW-class but live. The result lands on the
   VIRTUAL EQ (G11) with the predicted post-EQ ghost drawn before anything is
   committed; from there: accept, trim by hand, or export to external DSP.
   Manual mode keeps the ranked max-3 suggestions for engineers who tune by
   hand. Boundary unchanged: the solver writes to the virtual layer and to
   exports — never directly into the live chain.
7. **Match score**: per-zone (LF/MF/HF) and overall, 0–100, from
   coherence-weighted in-corridor fraction. This is the "am I done?"
   readout — the differentiator the market sweep confirmed nobody ships.
8. **Freeze-ghost on touch**: the moment an EQ move is detected (trace
   changes while target doesn't), freeze the pre-move trace as a ghost with
   a delta readout — the engineer sees what their hand just did.

## V2 — Phase-alignment view (owner's #2)

Two sources (two captures, or capture vs live), for sub/main and box-to-box
alignment. Same judgement grammar:

1. **Δφ trace** across frequency, wrapped/unwrapped toggle; green where
   |Δφ| < threshold (default 45°) AND both coherences pass — judged only in
   the user-selected crossover region, because whole-band phase match is
   neither achievable nor needed.
2. **AUTO DELAY** (owner upgrade, 2026-08-28): the solver computes the
   delay (and polarity flip if it wins) that maximises in-threshold fraction
   over the region — delay-finder + group-delay difference (P2) — and applies
   it to the virtual alignment in one action, summation ghost updating live.
   One-decimal ms plus whole samples; environment input (G16) annotates
   expected drift. Export carries the number to the external DSP; the live
   chain is never driven directly.
3. **Predicted summation ghost** (G11): live preview of the combined
   magnitude at the current delay/polarity BEFORE committing — including the
   dreaded combing when it's wrong. Slider scrubs delay; ghost re-draws.
4. **Alignment score**: in-threshold fraction over the crossover region — the
   wizard (G17) is this view plus "accept the solver's number".

## V3 — Proposed additions (station-2 proposals, owner to trim)

| # | View | What it shows | Rides on |
|---|---|---|---|
| V3a | **Stability/variance envelope** | min–max band envelope of the live trace over the last N seconds (or over walked mic positions) behind the live trace — a wide envelope says "this region is not tunable from one point, move on or average" | P2 averaging, G14 |
| V3b | **Feedback hunter overlay** | fast-attack peak-hold ridge markers on narrow sustained peaks during soundcheck, ranked candidates with note names | P1 RTA (near-free) |
| V3c | **A/B delta strip** | store A, tune, toggle B: a signed difference trace + "what changed" summary per zone; pairs with the freeze-ghost | P5 traces |
| V3d | **Drift watch** | after tuning, a small HUD chip re-measuring periodically: match score trend + delay drift vs temperature (G16); goes amber when the 3 pm tune has drifted by evening | G16, G7 alarms |
| V3e | **Done-ness HUD** | the V1+V2 scores as three small chips (EQ match, phase, stability) always visible during tuning — tuning gets a definition of done | V1, V2, V3a |

## Auto-solver honesty rules (both solvers)

- A solver only optimises over coherence-trusted, resolved data; it states
  what it refused to judge.
- Every auto result is shown as prediction-vs-measured after apply; if the
  next measurement disagrees with the prediction beyond tolerance, the view
  says so instead of silently re-solving.
- Auto-EQ never boosts into a null (a dip the predicted-summation ghost
  attributes to cancellation gets flagged "phase problem, not EQ problem" and
  routed to V2).

## Phase placement

- **P5**: V1 without suggestions (target+corridor+offset+gating+score), V3b,
  V3c. The score enters here.
- **P7**: V1 auto-EQ solver, V2 complete (auto-delay + summation ghost = the
  G17 wizard), V3a, V3e.
- **P6+**: V3d (needs logging/alarms plumbing).

## Rejected while developing this

- Auto-APPLYING suggestions to external hardware: the non-goal line holds —
  simulate and export, never drive the live chain.
- Continuous full-range red/green per-pixel colouring: judged at band
  resolution with hysteresis (a bin flickering across the corridor edge must
  not strobe); colour changes are debounced ~300 ms.
- A single global score without zones: hides "perfect mids, broken top" —
  zones are the minimum honest granularity.
