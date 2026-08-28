# Trace library and session persistence — design record

*2026-08-28. Lane **L5a**, written at station 2 from the L5 station-1 research
pass and a design conversation with the owner. This is the foundation the rest
of L5 serialises through, which is why it is specified before anything that
draws.*

## Why this is its own record

`docs/specs/2026-08-28-interactive-tuning-visuals.md` specifies the V1/V2/V3
judgement views well, but the master plan assigns L5 six further items the spec
never mentions: the trace library, multi-plot workspaces, the spectrograph,
cepstrum/wavelet, the Bode layout as a named layout, and session persistence. A
builder would have had to invent all six.

At the owner's direction (2026-08-28) L5 is therefore split into four records,
ordered by dependency:

| | Scope | Status |
|---|---|---|
| **L5a** | Trace model, library, session persistence, the repaint architecture | **this record** |
| L5b | Targets, corridor, coherence gate, match score | not written; partly blocked (see §8) |
| L5c | Bode layout, multi-plot workspaces | not written |
| ~~L5d~~ | Cepstrum, wavelet | **moved out of L5** — it is DSP, and belongs with L2/L3 |

Everything below is L5a. Where a decision constrains a later record, it says so.

---

## 1. A trace is one complete capture

**Decision.** A trace holds everything that capture produced. It is **not** a
tagged union of "an RTA trace" and "a transfer-function trace", and a view does
not ask a trace what type it is — it asks what fields it *has*.

This follows working practice with Smaart and Open Sound Meter, where one
capture carries the full measurement and the view mode selects which projection
of it you see.

Fields, each present or absent:

| Field | Present when |
|---|---|
| `magnitudeDb` | always |
| `phase` (wrapped, radians) | the capture was dual-channel |
| `coherence` (γ², see the dual-FFT record) | the capture was dual-channel |

A single-channel trace opened in a Bode layout leaves the phase and coherence
panes empty for that trace. That is the correct outcome, not an error, and it is
the reason the model is presence-based: **the file never has to fabricate a
phase of zero, so it never lies about what it contains.**

The frequency axis is not stored. It is derived from `sampleRate` and `fftSize`,
which are stored — one source of truth, and no way for a stored axis to disagree
with the data it indexes.

### 1.1 Metadata

| Group | Contents |
|---|---|
| Identity | `id` (UUID), capture timestamp |
| Provenance | device name, channel-role assignment, `sampleRate`, `fftSize`, window |
| Averaging | type, depth, and the **effective** average count (dual-FFT record §5) |
| Corrections | applied delay in samples; calibration offset **and its unit** |
| Schema | the record version this trace was written at |

**Where metadata lives, exactly once.** All of the above is written in
`session.xml` (§3) and nowhere else. The `.bin` header carries only what is
needed to interpret its own bytes without the index — point count, which fields
are present and in what order, and the trace UUID as a cross-check. Descriptive
metadata is never duplicated into the `.bin`, because two homes for one fact is
two facts that can disagree.

**The calibration unit is load-bearing.** A trace captured in dBFS and reloaded
as dBSPL is a silent lie: the numbers look plausible, they are wrong by the
calibration offset, and nothing on screen contradicts them. The unit travels
with the offset or neither is written.

### 1.2 Raw audio is optional and off by default

A capture may optionally retain the raw sample block that produced it, enabling
later recomputation with different parameters. This is **opt-in per capture,
default off**, at the owner's direction.

Default-off is a privacy decision, not a size decision. Recording a room full of
people is categorically different from recording a curve, and the tool must not
do the former as a side effect of the latter.

---

## 2. The library

Sizing decision from the owner: **dozens of traces overlaid**, with hide/show
and grouping. That number, not a smaller one, drives §4.

- **A flat list plus one level of groups.** Not a tree. One level already covers
  measurement position, source, and before/after, and stopping there removes an
  entire class of UI and serialisation problems that nothing in the workflow
  asks for.
- **Colour is assigned by group** — one hue per group, varied by lightness
  within it. With dozens of traces the legend is what distinguishes them; no
  palette yields thirty mutually distinguishable hues against a graphite
  background, and pretending otherwise produces a plot nobody can read.
- **Per trace, as library state** (editable, and distinct from what the capture
  recorded): `name`, visible flag, group membership, and a *shade index* — the
  position within the group's hue that gives this trace its lightness. The shade
  index is assigned automatically on capture and may be overridden; the hue
  always comes from the group, so regrouping a trace recolours it.
- **Solo** ("show only this") is hide-everything-else, not a separate mode.

**The line between the two.** Capture metadata (§1.1) is what the measurement
*was* — fixed at the moment of capture, never edited, because editing it would
make the file describe a measurement that never happened. Library state is how
the user has since chosen to *organise* it, and every part of it is editable.
`name` is library state for exactly this reason: renaming a trace changes
nothing about the measurement.

The library owns `revision` (§4), and only library state moves it.

---

## 3. The session on disk

**Decision.** A session is a **folder**, with an explicit "export .zip" action
for handing one to somebody else.

```
<session>/
  session.xml         index: trace metadata, groups, visibility, workspace, schema version
  traces/<uuid>.bin   per-bin field arrays, float32 little-endian, small header
  audio/<uuid>.wav    optional, absent unless the capture opted in
```

**Why a folder and not a single container.** Capturing during a show appends one
`.bin` and rewrites a small index. A container format would rewrite the whole
session on every capture — wasteful, and a crash mid-write could lose the entire
session rather than one trace. The tool is used live; that is the deciding
argument, and it outweighs the better hand-it-over ergonomics of a single file,
which the export action restores anyway.

**Per-bin, not decimated.** At `fftSize` 32768 a field is 16385 floats, so all
three are roughly 197 KB per trace and fifty traces are about 10 MB. That is
nothing, and the alternative discards resolution that cannot be recovered —
a show does not happen twice. Decimation belongs to drawing (§4), not storage.

**Writing is atomic.** `session.xml` is written to a temporary file and renamed
over the previous one, so an interrupted write leaves the previous index intact
rather than a truncated one.

**Schema version is the first thing read.** A session written by a newer version
of the tool is **refused with a clear message**, never partially interpreted.
Guessing at an unknown schema produces a plausible wrong measurement, which is
worse than refusing to open it.

---

## 4. Drawing: the repaint gate is currently broken for this

`RtaView` repaints only when `Snapshot::sequence` changes
(`app/src/view/RtaView.h`). A trace library breaks that gate **in both
directions**:

- stored traces carry no sequence, so every live frame redraws all of them —
  O(N × points) per frame, with N in the dozens;
- editing a stored trace (rename, hide, recolour, regroup) produces no sequence
  change, so the plot **never repaints** and the edit appears not to have
  happened.

**Decision — a two-part gate and a cached layer.**

1. The gate becomes `snapshot.sequence` **or** `library.revision`, where
   `revision` is a monotonic counter bumped by every library mutation.
2. Stored traces render into a **cached image**, rebuilt only when `revision` or
   the plot geometry changes. The live trace draws over it each frame.
3. This returns the live repaint to **O(1) in trace count**, which is what makes
   the owner's "dozens" workable rather than aspirational.

**Decimation happens when the cache is built**, not at capture. Each pixel
column takes the **min and max** of the bins falling in it, drawn as a vertical
extent. Taking a single representative sample per column instead would let a
narrow peak or null vanish between columns — and a null is exactly what the
engineer is looking for.

---

## 5. Module boundaries

`ui/` (`az_ui`) must contain no measurement vocabulary (CLAUDE.md).

- **To `az_ui`:** a generic list/legend row primitive, if one is needed. Nothing
  else in L5a is vocabulary-free enough to promote.
- **Stays in `app/`:** the trace model, the library, the session reader/writer,
  the revision gate, the cached layer, group-to-colour assignment.

The pure, JUCE-free parts — the trace model, the session encoder/decoder, the
decimator, the revision rules — live in headers registered under the existing
`measure_has_no_framework_deps` ctest, alongside `PlotGeometry.h` and
`Readouts.h`. When adding them, **check the scanned-file count moves**: that
guard silently stops guarding a path it cannot resolve
(`memory/core-must-not-include-frameworks.md`).

---

## 6. How CI proves this without a sound card

| Test | Assertion |
|---|---|
| Session round trip | Write, read back: every field and every metadata item identical — **including the calibration unit** |
| Newer schema | A session claiming a higher version is refused with a distinct error, not partially read |
| Interrupted write | A temporary index left behind does not corrupt or shadow the previous session |
| Decimation bounds | For every pixel column, the emitted min/max **brackets** the true min/max of the bins in that column — a closed-form assertion, no golden file |
| Revision gate | Every library mutation increments `revision`; no non-mutating call does |
| Presence model | A single-channel trace reports no phase and no coherence, and a consumer asking for them gets absence, not zeros |

---

## 7. Out of scope for L5a

Targets, corridors, the coherence gate and the match score (L5b); the Bode
layout and multi-plot workspaces (L5c); the spectrograph; cepstrum and wavelet
(moved to the DSP lanes). L5a stores what those will need and draws traces; it
judges nothing.

---

## 8. Known blockers recorded here so L5b does not rediscover them

- **The X-curve tolerance table is paywalled.** The shape is publicly
  documented — flat to roughly 2 kHz, then −3 dB/oct to 10 kHz and −6 dB/oct
  above, with small-room and large-room variants — but the tolerance band is
  only in **ISO 2969:2015** and **SMPTE ST 202:2010**. No X-curve tolerance may
  be written from a guess. This joins IEC 60268-16 (STI) on the purchase list.
- **`az_ui` has no ice-blue token** although the tuning-visuals spec invokes the
  house rule that ice blue is reserved for exactly one meaning. Reconcile
  against `PROJECT005-AZ-handsfree/src/gui/theme/AzTheme.h` before L5b's
  judgement colours are written.
- **Four judgement semantics share two colour tokens** in
  `app/src/view/MeasureColours.h` — `untrusted`/`underResolved` are both
  `az::ui::faded`, and `target`/`ghost` are both `az::ui::dim`. That file argues
  for the sharing deliberately, and the argument holds; but it means the
  distinguishing signal must be **hatching or alpha**, and L5b must verify a
  hatched untrusted region and a ghost curve are actually distinguishable when
  both appear in one plot.
