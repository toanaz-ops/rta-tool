# Bode layout and multi-plot workspaces — decisions (L5c)

*2026-08-29. Lane L5c, station 2. Written from the station-1 research pass;
sources are cited where a decision rests on them, and marked unverified where
station 1 could not confirm them. Nothing in `app/` implements any of this yet —
the existing `TransferFunctionPreview.cpp` is a mockup with synthetic curves,
and this record exists so the real implementation cannot silently inherit the
mockup's accidents as decisions.*

Each decision records what the rejected option would have cost, because a
decision without its reasoning is a decision the next session reverses by
accident. Where a decision is a judgement rather than a derivation or a
standard, the text says so.

The temptation this record must resist is named up front: **the obvious choice
is to copy the mockup.** It already renders, it already looks like the product,
and a reviewer will ask why any decision below differs from it. The answer,
decision by decision, is that the mockup contains four things that are accidents
of being a mockup — a fixed 380 px magnitude pane, an untreated phase trace
under low coherence, a pen-lift scheme that only works because it samples an
analytic function, and a coherence strip whose meaning is undefined once more
than one trace exists. Where the mockup made a real choice (stacked panes,
wrapped ±180 phase, a thin trust ribbon), it is kept, on arguments, not on
authority.

---

## 1. Magnitude and phase are stacked panes on one shared frequency axis

**Decision.** The Bode layout is a vertical stack — coherence ribbon, magnitude
pane, phase pane — all three built on the **same** log-Hz x mapping: one
function computes the shared `left`/`right`/`fLowHz`/`fHighHz`, and every
pane's `PlotGeometry` is constructed from it. The frequency label strip is
drawn once, under the bottom pane, as the mockup already does.

**The alternative that must be argued.** REW puts phase on the *same* graph as
magnitude, as a second trace against a second y-axis on the right. That is not
a fringe choice — REW is the most-used free analyser in the field — and it
buys full vertical resolution for both quantities.

It is rejected on one defect that is specific to this codebase: **L5a already
committed to dozens of traces overlaid** (trace-library record §2, an owner
sizing decision). Dual-axis Bode is readable with one or two traces; with
dozens, every wrapped phase trace crosses every magnitude trace many times per
decade, and the two families are only distinguishable by remembering which axis
each one belongs to. REW's own workflow is few-traces-at-a-time; ours, by prior
decision, is not. Smaart, Open Sound Meter, SysTune and the repo mockup all
stack, and for this reason stacked panes also keep `PlotGeometry` exactly what
it is — one y mapping per pane — instead of growing a second y axis that every
other view would then carry.

**What stacking costs.** Half the vertical resolution per quantity, and a
feature that spans both quantities (a magnitude dip and its phase swing at the
same frequency) requires eye travel between panes. The shared x mapping is the
mitigation: the dip and the swing sit on the same pixel column, and the cursor
(when one exists) reads out both. This is why the shared mapping is stated as
part of the decision, not an implementation nicety — panes with independently
computed x mappings that disagree by a pixel would silently destroy the only
compensation stacking has.

---

## 2. Pane heights are proportional; the mockup's fixed pixels are rejected

**Decision.** The coherence ribbon keeps a **fixed height** (34 px, as the
mockup — it is furniture, like an axis strip, and scaling it with the window
buys nothing). The remaining height splits **magnitude : phase = 5 : 3**,
proportionally, at every window size.

**Why the mockup is wrong here.** `TransferFunctionPreview.cpp` sets
`kMagnitudeHeight = 380` and gives phase the remainder. At the 1100×760 default
that happens to yield roughly 62/38 — a reasonable proportion — but the rule
producing it is not a proportion, it is a constant. On a shorter window the
phase pane collapses toward zero; on a taller one, phase grows without bound
while magnitude stays fixed. The mockup's *outcome at one size* was acceptable;
its *rule* was an accident of that size. 5 : 3 (= 62.5/37.5) is chosen to keep
the default-size snapshot essentially what the owner has already seen, while
replacing the rule that produced it.

**This is a judgement.** No standard allocates Bode pane heights. Magnitude
gets the larger share because it is where the 0.1 dB readout rule (CLAUDE.md)
has to remain resolvable, and because phase at ±180 fixed scale needs less
resolution per pixel to be read to the nearest few degrees. Smaart and OSM let
the user drag the split; that is compatible with this decision — 5 : 3 is the
default, and if a splitter ships later it moves the weight, not the rule.

---

## 3. Coherence: a ribbon for the live capture, a continuous fade for every trace

**Decision.** Two mechanisms, doing two different jobs:

1. **The ribbon** (the mockup's per-column alpha strip) shows the coherence of
   the **live capture only**. With one strip and dozens of traces, a ribbon
   that showed "the" coherence would be showing an undefined quantity; the
   mockup never had to answer this because it had one synthetic trace. The
   ribbon is drawn in the live trace's own colour (the amber accent), so the
   strip visually belongs to the trace whose trust it reports.
2. **Every trace — magnitude and phase, live and stored — fades per column**
   with its own coherence: drawn alpha is a monotone function of γ² with a
   floor of 0.25, so an untrusted region dims but never vanishes. When
   coherence is decimated to a pixel column for this purpose, the column takes
   the **minimum** γ² of its bins — trust shown never exceeds trust measured.

**The defect in the mockup this fixes.** The mockup does *nothing* to the phase
trace under low coherence. The phase of uncorrelated noise is uniformly random,
so a low-γ² phase region is the single most misleading stretch of pixels on the
screen — it draws with full confidence a value that means nothing, in the exact
place (a null, the LF end) where an engineer is deciding whether to move a
delay. Open Sound Meter fades its phase and magnitude traces per point below a
coherence threshold (`SeriesRenderer::coherenceSpline`); the mechanism is
adopted, the threshold is not (next paragraph).

**Why not Smaart's overlay or blanking, here.** Smaart overlays the coherence
*curve* on the magnitude graph at half height, and offers "coherence blanking"
— magnitude below a user threshold is hidden from view, data untouched. The
overlay is rejected for the same dozens-of-traces reason as decision 1: dozens
of coherence curves overlaid on dozens of magnitude curves is the dual-axis
spaghetti again. Blanking — and any *thresholded* judgement about coherence —
is **L5b's coherence gate**, deliberately not decided here. L5c fixes the
continuous display mechanism (fade, floor, per-column minimum); L5b owns every
threshold, including whether a hard gate blanks phase as well as magnitude
(station 1 could not confirm what Smaart blanks beyond magnitude — that stays
unverified and must not be cited as prior art either way).

**What the fade costs.** An alpha strip and a faded line cannot be read as a
number — "0.75" is not recoverable from a dimness. The cursor readout carries
that: coherence displays 0..1 to two decimals (CLAUDE.md), per trace, at the
cursor frequency. The alpha floor of 0.25 is a judgement — zero would make the
display silently delete data, which is the gate's job to do loudly, and a floor
much higher than 0.25 stops being visible as a fade at all.

---

## 4. Phase axis: wrapped ±180 by default; unwrap is a toggle; no cursor re-referencing

**Decision.** The phase pane defaults to a **fixed ±180° axis with wrapped
phase**, pen lifted at wrap discontinuities. An explicit **unwrap toggle**
exists as a view-model operation — the dual-FFT record §6 already fixed that
unwrapping is a display operation and never feeds the engine; this record
inherits that and adds only the presentation: when unwrapped, the axis extends
in whole multiples of 360° to fit the trace. Whole-degree labels, per the
mockup's reading of the readout rules (degrees are not dB; no forced decimal).

**Why wrapped is the default.** Nothing surveyed defaults to unwrapped — OSM
and the mockup are fixed ±180, REW wraps by default with an explicit toggle.
The deeper reason is in the dual-FFT record: unwrap is ambiguous wherever
coherence is low, and one bad bin steps every bin above it by 360°. A default
display must not be one bad bin away from moving the entire top half of the
trace; the wrapped view degrades locally, the unwrapped view degrades globally.

**The REW behaviour that is rejected, and what rejecting it costs.** REW, even
when unwrapped, re-references the trace by multiples of 360° so the value at
the *cursor frequency* stays within ±180. It is a genuinely clever readability
device, and it is rejected: it makes the drawn trace a function of the mouse
position, so the same data paints differently as the cursor moves, which
breaks the cached-layer architecture (§6 of the L5a record — stored traces
rasterise once per revision, not per mouse move) and makes two screenshots of
one measurement disagree. The cost of rejecting it is real: an unwrapped trace
with a long delay drifts thousands of degrees below zero at HF, and the axis
must follow it into visually large numbers. That cost is accepted; a display
that depends on cursor state costs more.

---

## 5. Phase decimation: unwrap along the bin axis at cache build, wrap at draw

**Decision.** `decimateToColumns` as it stands is **wrong for wrapped phase and
must not be reused for it**: min/max of +179° and −179° manufactures a ~358°
extent that was never measured. Phase gets its own decimation path in the
cached layer:

1. At cache build, the trace's phase bins are unwrapped **along the bin axis**
   (a running unwrap, each bin adjusted by the multiple of 360° that brings it
   within 180° of its predecessor). This is a view-side, per-rebuild operation
   on a local copy — permitted precisely because dual-FFT §6 put unwrap in the
   view and out of any accumulator.
2. Min/max per column is taken on the **unwrapped** values, where extent is a
   true extent.
3. At draw, in the default wrapped view, each column's extent is mapped back
   modulo 360 into ±180; where a column's drawn position jumps more than 180°
   from its neighbour, the pen lifts. A column whose *unwrapped* extent exceeds
   360° draws as a full-height band — phase is rotating faster than one pixel
   column can resolve, and a full band is the honest rendering of that.

**Why not the two simpler options.** Raw wrapped min/max is the fake-358°-span
defect above. One representative bin per column (dropping min/max for phase
only) avoids that but aliases: at fftSize 32768 the top decade packs tens of
bins per column, and a delay of a few milliseconds rotates phase through
multiple full turns inside one column — a single sample lands anywhere,
frame-to-frame, and the trace shimmers. The mockup's pen-lift never met this
problem because it samples a closed-form `phaseDeg(hz)` exactly once per pixel;
that composes with nothing and is the fourth mockup accident named in the
preamble.

**What this costs.** A second decimation path where L5a had one, and a running
unwrap whose one-bad-bin fragility (dual-FFT §6) is now inside the cache
rebuild. Both are contained: the path is JUCE-free and testable on bare arrays
like the existing decimator, and a bad unwrap corrupts one rebuild of one
cached image — the next revision bump rebuilds from the stored wrapped values,
which are never modified.

---

## 5a. Coherence bridges interior gaps, and stored phase is hidden while unwrapped

*Added 2026-08-29 during implementation (station 4). Both are interactions
decision 3 and decision 4 did not decide, found by building them.*

**Coherence is bridged across interior column gaps, exactly as magnitude is.**
Below roughly 2 kHz an FFT has fewer bins than the plot has pixel columns, so
`decimateToColumns` correctly reports most low columns as empty. `bridgeGaps`
already fills those for the magnitude *extents*, on the stated argument that a
spectrum is continuous and its bins are samples of it, so joining them asserts
LESS than leaving holes. Coherence was not bridged, and the alpha for an empty
column fell to the 0.25 floor — so at the LF end a bridged trace alternated
near-opaque and floor-dim column by column, a barcode. The ribbon showed the
same, more visibly.

The fix follows the same argument: an empty column means "not sampled at this
resolution", not "measured and found untrustworthy", and painting it as the
latter is precisely the false assertion the alpha floor exists to avoid.
Interior gaps interpolate; **leading and trailing runs stay at the floor**,
because outside the measured range there is nothing to interpolate between and
inventing trust there would be the assertion this whole mechanism refuses to
make.

*What this costs:* the "a column with no bins reports no trust" property is now
narrower than it was — it holds at the ends, not in the middle. That is the
correct trade, but it means a reader can no longer assume a dim column means a
measured-untrustworthy one anywhere on the axis.

**Stored phase traces are hidden while the phase pane is unwrapped, and the
pane says so.** Decision 4 made unwrap a view toggle; decision 5 put the running
unwrap inside the cached-layer rebuild. Neither says what a *stored* trace does
when the live one is unwrapped, and the three options are not equal: drawing
stored traces still-wrapped onto an extended axis puts their ink at literal
degree rows on an axis that no longer means that, which is actively misleading;
unwrapping them too is architecturally available (`StoredTraceLayer`'s cache key
already contains `dbTop`/`dbBottom`, so an extended axis already forces a
rebuild) but raises a question this record cannot answer alone — whether the
axis should then *enclose* stored traces, whose different delays could stretch
it over thousands of degrees.

So: hidden, **with a visible note in the pane**. The display must never silently
delete data — that is stated here for the alpha floor and applies with more
force to an entire trace. Hiding loudly is honest; hiding quietly is the failure
mode. Making unwrap show stored traces properly stays open, and is an owner
question rather than an implementation detail, because it is really a question
about whether one pane can hold two delays.

---

## 6. Workspace: a vertical stack of at most three panes, one workspace per session

**Decision.** A workspace is an **ordered vertical stack of 1–3 panes**, each
pane an instance of a named view type with a fractional height weight. The
initial type vocabulary is `rta` (the band-bar view that exists) and
`transfer` (the Bode composite of decisions 1–5). Splitting is vertical only.
There is **one workspace per session** — no named, switchable layouts.

On disk it is the `workspace` slot that `session.index` reserved with no shape
(L5a §3), in the index's own line format:

```
workspace.panes=2
workspace.pane.0.view=rta
workspace.pane.0.weight=0.5
workspace.pane.1.view=transfer
workspace.pane.1.weight=0.5
```

Weights are normalised on read. An **unknown view name** does not refuse the
session: that pane falls back to `rta` and the failure is reported. This is a
deliberate asymmetry with L5a's refuse-newer-schema rule, and the line between
them is what is at risk: a guessed *measurement* is a plausible lie, a guessed
*layout* is a wrong arrangement of true data. Refusing a whole session of real
captures over a layout word would destroy value to protect nothing.

**Per-pane trace visibility is not persisted, because it does not exist.**
Visibility is library state (L5a §2) — one flag per trace, global. Neither OSM
nor Friture was found to persist which traces show in which pane either; here
it is not an omission but a consequence of keeping one source of truth. If
per-pane trace filtering is ever wanted, it is a new library concept first and
a workspace field second.

**Against the alternatives, which are genuinely four-way.** Friture's uncapped
docking grid (rowCount = ceil(sqrt(n))) needs a dock framework `az_ui` does not
have, and buys freedom the use case does not ask for: this tool is read from
six feet away, one screen, seconds at a time (Palette.h's own design brief).
SysTune's two fixed panes is a cap with no room for RTA + transfer + one more.
Smaart's unlimited tabs-and-windows is a whole windowing model. OSM's shape —
vertical SplitView, hard cap 3, per-pane type, persisted count/type/height —
is the one adopted, because it is the smallest one that covers the workflow.
**The cap of 3 is a judgement**, matching OSM's; below roughly a third of a
760 px window a dB pane stops resolving what the readout rules promise.
Vertical-only is also load-bearing on decision 1: every current pane type puts
frequency on x, so a vertical stack keeps 2 kHz on the same pixel column in
every pane; any future pane type that joins the stack accepts that constraint
(a spectrograph pane, if built, therefore scrolls in y).

**Named workspaces rejected for now.** The only lead for genuinely named,
switchable layouts is SysTune's "User Layouts", which station 1 could not
verify (manual unparseable, article 403'd) — it stays unverified and cannot
carry a design. One-per-session also means the session folder remains the unit
of "a saved setup", which is what it already is. The cost: a user who wants an
"alignment view" and a "verification view" of one session must toggle by hand.
If that cost turns out real, named layouts are an additive index change
(`workspace.<name>.pane.0.view=…`) that does not invalidate this shape.

**The one `az_ui` addition.** The stack needs a tile/split container; `az_ui`
has none. What is promoted is a **generic weighted vertical-split primitive**
— no pane-type vocabulary, no "measurement" in any identifier — consistent
with the module rule that `ui/` knows nothing about audio measurement. The
pane registry mapping `rta`/`transfer` to components stays in `app/`.

---

## 7. Spectrograph: not designed here, but three traps are closed now

The spectrograph has no home in the L5a lane table and this record does not
design it. It does fix three constraints, because station 1 found each one is a
trap another project has already fallen into, and a later record that never
reads Friture's source would fall in again:

1. **"Spectrograph" is two features, and the name must never conflate them.**
   The live wall-clock scroller (Smaart, OSM, SysTune, Friture) and the
   post-capture decay view around an impulse peak (REW, ARTA's sonogram)
   answer different questions. The scroller is display-lane work; the decay
   view belongs with the impulse-response/RT60 lane, next to Schroeder
   integration, and must not be scheduled as "the spectrograph, part 2".
2. **History stores numbers, not pixels.** OSM bakes the colour into stored
   points (raw dB discarded — history can never be re-mapped, capped at 51
   rows with a `//TODO` above the line); Friture keeps only a pixel ring
   buffer whose depth is the widget's current width, so a resize destroys
   history. REW keeps values and can re-colour after the fact. The rule here:
   per-frame dB values are the stored thing, colour is applied at draw — the
   same values-then-presentation split every other decision in this codebase
   makes.
3. **`az_ui` gains a generic continuous colour-ramp primitive, or nothing.**
   The palette has 12 discrete tokens and no ramp; a ramp named for a
   measurement would break the module rule. The primitive is a generic
   position→colour ramp. Its default should be perceptually ordered — REW
   ships cubehelix and Friture ships cmrmap, two independent projects
   rejecting rainbow on perceptual grounds, against OSM's unjustified
   blue→green→red — but *which* ramp fits SODIUM RACK is an owner taste call
   (§ owner questions).

---

## 8. How the decisions are proven

The project rule: a claim needs a command and its output. What each decision's
proof looks like, and which genuinely cannot be tested without a person:

**Pinned by JUCE-free unit tests** (headers under the
`measure_has_no_framework_deps` guard, like `TraceDecimator.h` — and when the
new files are registered, check the guard's scanned-file count moves, per
L5a §5):

- **Phase decimation (decision 5), closed form.** Feed the synthetic pure
  delay's phase, `φ(f) = −2πfD/fs`, wrapped. Assert every column's unwrapped
  extent brackets the true line; assert no adjacent drawn pair spans a wrapped
  jump ≤ 180° with the pen down; assert the fake-358°-span input (+179/−179
  alternating) produces pen-lifts, not a band. This is the test that catches
  the min/max-on-wrapped-phase bug — listed explicitly for the same reason
  dual-FFT §7 lists its identically-1.0 test.
- **Coherence column minimum (decision 3).** Per column, emitted trust equals
  the true minimum of its bins — closed-form, no golden file.
- **Workspace round trip (decision 6).** Write, read back: pane count, types,
  weights identical; weights renormalised; unknown view name yields the
  fallback pane and a reported error, not a refused session.
- **Layout math (decision 2).** The pane-split function is pure
  (height → rects): assert 5:3 at several heights, ribbon fixed, no pane
  negative, x mapping identical across panes.
- **Repaint cost (decisions 1–6 jointly).** Per-pane cached layers keyed on
  revision + geometry: assert `rebuildCount` does not move when only the live
  sequence advances — the O(1)-in-trace-count property L5a §4 bought must
  survive multiplication by panes.

**Pinned by `app/tests_juce/` and `rtatool_snapshot`.** The composite renders:
snapshot the transfer layout at two window sizes and assert the phase pane's
height tracks 3/8 of the shared area (geometry read back from the component,
not from pixels); snapshot with a low-coherence synthetic trace and assert the
drawn alpha differs between high- and low-γ² columns (readable from the
image's pixels).

**Genuinely needs a person.** Whether a 0.25-alpha faded trace is
*distinguishable but clearly subordinate* against graphite; whether 5:3 reads
right at six feet; whether the full-height phase band reads as "unresolvable"
rather than "broken". These join L5b's hatched-vs-ghost check (L5a §8) as
eyes-only items — the snapshot makes the looking cheap and repeatable, but the
judgement is human, and no test output will be pasted as proof of it.

---

## What this record does not decide

- **Every coherence threshold** — blanking, gating, the 0.95-style default —
  is L5b's coherence gate. This record fixed only the continuous mechanisms.
- **The spectrograph's design** — orientation details, history depth, frame
  rate. §7 fixed three constraints; the feature needs its own record when
  scheduled, and the decay-view half belongs to the IR/RT60 lane.
- **A group-delay pane.** Core computes group delay (dual-FFT §6); whether it
  is a pane type, an overlay, or a readout is undecided, and adding
  `groupdelay` to the pane vocabulary later is additive.
- **A draggable splitter and a cursor/crosshair.** Decisions 2–4 assume a
  cursor readout exists eventually and a splitter may; neither is specified
  here.

---

## Questions for the owner (collect into the batch)

1. **Pane cap and named workspaces (taste).** Cap 3, vertical-only, one
   workspace per session — is that the product, or are named/switchable
   layouts wanted from the start? The disk shape survives the change either
   way, but the UI does not. (If it matters: SysTune's "User Layouts" is the
   only surveyed precedent and is unverified; a look at a running SysTune
   would settle it, but nothing here requires it.)
2. **Spectrograph ramp (taste).** A perceptual multi-hue ramp
   (cubehelix-family, the REW/Friture-justified default) or a single-hue
   SODIUM-amber intensity ramp that keeps the rack aesthetic at the cost of
   less discriminable levels? The `az_ui` primitive is generic either way;
   this only chooses its default.
3. **Unwrapped phase and stored traces (product).** While the phase pane is
   unwrapped, stored traces are hidden and the pane says so (§5a). Showing them
   means unwrapping each stored trace too, and then deciding whether the axis
   stretches to enclose them — with several stored captures at different
   delays that range can run to thousands of degrees, at which point the live
   trace is a flat line at the middle. Is "unwrap shows only the live trace"
   the product, or should unwrap be per-trace, or should the axis clamp and let
   stored traces run off it?
4. **No purchase is required for L5c** — recorded so this lane is not held
   waiting on the ISO 2969 / SMPTE ST 202 / IEC 60268-16 purchases that block
   parts of L5b.
