# L5c — Bode layout, workspaces, and the display finally wired: implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: use `superpowers:subagent-driven-development`
> (recommended) or `superpowers:executing-plans` to implement this plan task by
> task. Steps use checkbox (`- [ ]`) syntax for tracking. Station 5 (adversarial
> verify, reviewer with **no Write tools**) is not optional — see
> `docs/reports/README.md`.

*2026-08-29, lane L5c, station 3. Written from
`docs/dsp/2026-08-29-display-layer-l5c.md` (station 2) after reading the real
files, not the record's description of them.*

**Goal:** the transfer function reaches the screen — a stacked Bode composite
fed by a real dual-FFT measurement, stored traces drawn beneath it with their
own coherence, and a 1–3 pane workspace that survives a session reload.

**Architecture:** three JUCE-free reduction layers (pane layout math, phase
column decimation, coherence→alpha) sit under two JUCE components
(`TransferView`, `WorkspaceView`), and a new app-side bridge drives
`rta::dsp::DualFftEngine` from `Analyser` so the composite draws a measurement
rather than a closed-form curve. Nothing in `core/` is touched by this lane.

**Tech stack:** C++20, JUCE 9.0.1, Catch2, CMake. `rta_core` is consumed, never
edited.

---

## Scope, and the one thing the record did not cover

The decision record covers decisions 1–6. This plan implements all six, plus one
thing the record could not have known and no lane owns:

> **The L2 dual-FFT engine is entirely inside `core/`, and nothing in `app/`
> calls it.** `core/include/rta/dsp/{DualFftEngine,TransferEstimator}.h` are
> built and tested; `grep -rn "DualFftEngine" app/` returns nothing.
> `rta::measure::Snapshot` carries band levels and a raw spectrum and has no
> transfer-function field at all.

Without that bridge, a "transfer function view" draws synthetic curves — which
is the mockup again, the exact failure the record's preamble exists to prevent.
**The owner ruled on 2026-08-29 that the bridge lands inside L5c** (tasks 5 and
6), so the pane shows a measured H, a measured coherence, and a delay the
engine actually found.

**Out of scope, deliberately:**

- **The spectrograph.** Record §7 fixes three constraints and explicitly does
  not design it. No `az_ui` colour-ramp primitive is added here either — its
  default is owner question 2, still open.
- **Every coherence threshold** — blanking, gating, a match verdict. Record §3:
  that is L5b, which is separately blocked on a standards purchase. This lane
  ships only the *continuous* mechanisms: fade, floor, per-column minimum.
- **A draggable splitter and a cursor/crosshair.** Record §"What this record
  does not decide".
- **Named, switchable workspaces.** Record decision 6 takes one workspace per
  session; owner question 1 may reopen it later, and the disk shape survives
  that change.

---

## Global constraints

Every task's requirements implicitly include this section.

- **Licence header.** Every new file starts with
  `// SPDX-License-Identifier: AGPL-3.0-or-later` and a second line naming its
  directory and the record section it implements.
- **`core/` is not modified by this lane.** Not one line. This is what makes
  L5c parallel-safe with L4 under the master plan's session-collision rule 1
  (`core/CMakeLists.txt` is the contention point). If a task appears to need a
  core change, stop and report it rather than making it.
- **`ui/az_ui/` must contain no measurement vocabulary.** No `coherence`, no
  `trace`, no `magnitude`, no `phase` in any identifier, comment, or file name
  under `ui/`. The split primitive in task 1 is generic geometry or it does not
  belong there. Project CLAUDE.md, "Module boundaries".
- **File length: hard cap 400 lines, aim 300.** Headers included. Each task
  below names the split to make if its file crosses 300.
- **Reading out numbers** (project CLAUDE.md): frequency is a whole number of
  hertz; dB keeps one decimal; coherence is 0..1 to two decimals; **degrees are
  whole numbers** — degrees are not dB, and the one-decimal rule is a dB rule.
- **Every JUCE-free file must be registered** in the
  `measure_has_no_framework_deps` guard's `GLOBS` list in
  `app/tests/CMakeLists.txt`, **and the guard's scanned-file count must be seen
  to move.** Trap from L5a §5 and from the handoff's "green that proves
  nothing" table, entry 1: that guard globs a caller-supplied path list and only
  FATALs when the *whole* list resolves to nothing, so one mistyped path makes
  the guard silently stop watching a file while still reporting OK. The count is
  21 today, and this lane adds eight JUCE-free files, so it should finish at
  29. **Run the guard and read the
  number** — do not write the expected number anywhere as though it were
  measured.
- **Build directory for this lane is `build-l5c`**, and it needs
  `-DRTA_BUILD_APP=ON` — most of this lane is JUCE components, and the
  `RTA_BUILD_APP=OFF` configure does not create `az_ui`, `rtatool_view_tests`,
  or `rtatool_snapshot`. Configure both ways at least once before reporting a
  number, and say which configuration each number came from. The handoff's
  baseline block explains why the two counts do not add up.
- **Never assert a value the implementation produced.** Every expectation below
  is a closed form, a geometric identity, or a round trip. Where a test is a
  regression lock rather than a correctness test, this plan says so in the test
  body's comment and the comment must survive into the code.
- **Do not weaken a guard to make a change compile.** Six guards exist
  (`core_has_no_framework_deps`, `measure_has_no_framework_deps`,
  `platform_types_has_no_framework_deps`, `filter_design_has_no_polynomial_form`,
  `core_makes_no_class_1_claim`, `coherence_gate_is_not_bypassed`). Cross-track
  biting is a feature.

---

## Naming, fixed here so tasks cannot disagree

Task implementers see only their own task. These are the exact spellings.

| Thing | Name | Header |
|---|---|---|
| Integer pane rectangle | `rta::view::PaneRect` | `view/BodeLayout.h` |
| The three stacked panes | `rta::view::BodePanes` | `view/BodeLayout.h` |
| Shared x mapping | `rta::view::FrequencyAxis`, `frequencyAxis()` | `view/BodeLayout.h` |
| Pane splitter | `rta::view::bodePanes()` | `view/BodeLayout.h` |
| Pane → geometry | `rta::view::paneGeometry()` | `view/BodeLayout.h` |
| Unwrapped column extent | `rta::view::PhaseColumn` | `view/PhaseDecimator.h` |
| Drawable column | `rta::view::DrawnPhaseColumn` | `view/PhaseDecimator.h` |
| Bin-axis unwrap | `rta::view::unwrapAlongBins()` | `view/PhaseDecimator.h` |
| Phase decimation | `rta::view::decimatePhaseToColumns()` | `view/PhaseDecimator.h` |
| Wrap for drawing | `rta::view::wrapForDrawing()` | `view/PhaseDecimator.h` |
| Trust → alpha | `rta::view::alphaForCoherence()`, `columnAlpha()` | `view/CoherenceAlpha.h` |
| Alpha floor | `rta::view::kUntrustedAlphaFloor` = `0.25f` | `view/CoherenceAlpha.h` |
| Transfer payload | `rta::measure::TransferBlock` | `measure/Snapshot.h` |
| Paired feed | `rta::measure::Analyser::pushPair()` | `measure/Analyser.h` |
| Lock-step hop count | `rta::measure::pairedHopCount()` | `measure/PairedDrain.h` |
| Synthetic impairment | `rta::measure::applyDelay()`, `addNoise()` | `measure/SyntheticImpairment.h` |
| Stroking helpers | `rta::view::strokeMagnitudeExtents()`, `strokePhaseColumns()` | `view/TraceStroke.h` |
| Bode composite | `rta::view::TransferView` | `view/TransferView.h` |
| Pane on disk | `rta::trace::PaneSpec`, `normalisePanes()` | `trace/Workspace.h` |
| Name → pane type | `rta::view::PaneView`, `resolvePaneView()` | `view/PaneRegistry.h` |
| Pane container | `rta::view::WorkspaceView` | `view/WorkspaceView.h` |
| Generic splitter | `az::ui::splitVertically()` | `ui/az_ui/theme/Layout.h` |

**Units at each seam, fixed once:** `rta::dsp::TransferSnapshot::phaseRadians`
is radians (core's choice). **`rta::measure::TransferBlock::phaseDeg` is
degrees**, converted exactly once, in `Analyser::publish`. Everything in
`view/` is degrees, because `PlotGeometry`'s y axis for the phase pane runs
`dbTop = 180.0` to `dbBottom = -180.0` and a second unit crossing that boundary
is a bug waiting for a tired evening.

---

## File structure

**New, JUCE-free** (compiled into `rtatool_analysis_tests`, registered in the
guard):

| File | Responsibility |
|---|---|
| `app/src/view/AxisMetrics.h` | axis-furniture constants, split out of `PlotAxes.h` so a framework-free caller can read them |
| `app/src/view/BodeLayout.h` | pane split + the ONE shared frequency axis (decisions 1, 2) |
| `app/src/view/PhaseDecimator.h` | bin-axis unwrap, column min/max, wrap-for-draw (decision 5) |
| `app/src/view/CoherenceAlpha.h` | γ² → alpha, per-column minimum (decision 3) |
| `app/src/measure/PairedDrain.h` | how many hops two rings can advance in lock-step |
| `app/src/measure/SyntheticImpairment.h` | integer delay + seeded noise, for a non-unity synthetic H |
| `app/src/trace/Workspace.h` | `PaneSpec`, weight normalisation, `kMaxPanes` (decision 6) |
| `app/src/view/PaneRegistry.h` | pane-name → type, with the fallback the codec must not make |

**New, JUCE** (compiled into `rtatool_view_tests` and `rtatool`):

| File | Responsibility |
|---|---|
| `ui/az_ui/theme/Layout.h` + `.cpp` | generic weighted vertical split — no measurement words |
| `app/src/view/TraceStroke.h` + `.cpp` | stroke one field's columns with per-column alpha |
| `app/src/view/TransferView.h` + `.cpp` | the Bode composite (decisions 1–5) |
| `app/src/view/WorkspaceView.h` + `.cpp` | the 1–3 pane vertical stack (decision 6) |

**Modified:**

| File | Change |
|---|---|
| `app/src/measure/Snapshot.h` | `std::optional<TransferBlock> transfer` |
| `app/src/measure/Analyser.{h,cpp}` | owns a `DualFftEngine`; `pushPair`; fills `transfer` |
| `app/src/measure/AnalysisThread.{h,cpp}` | lock-step paired drain |
| `app/src/measure/SyntheticInput.{h,cpp}` | delay + noise on the measurement channel |
| `app/src/measure/SyntheticSnapshot.{h,cpp}` | `makeSyntheticTransfer` |
| `app/src/view/StoredTraceLayer.{h,cpp}` | draws a chosen `Field`, with per-column alpha |
| `app/src/view/PlotAxes.h` | includes `AxisMetrics.h` instead of declaring the three constants |
| `app/src/trace/SessionCodec.{h,cpp}` | `[pane]` sections; `kSchemaVersion` → 2 |
| `app/src/trace/SessionStore.cpp` | stamps the schema version on write |
| `app/src/MainComponent.{h,cpp}` | owns the library + workspace; wires them in |
| `tools/snapshot.cpp` | `transfer.png`, `workspace.png` |
| `app/tests/CMakeLists.txt` | new test files, new sources, **new guard paths** |
| `app/tests_juce/CMakeLists.txt` | new test files and sources |
| `ui/CMakeLists.txt`, root `CMakeLists.txt` | `az_ui_tests` target |

---

## The one deviation from the record, and why

Record decision 6 illustrates the on-disk workspace as dotted top-level keys:

```
workspace.panes=2
workspace.pane.0.view=rta
```

**The implementation uses a `[pane]` section instead**, one per pane:

```
[pane]
view=rta
weight=0.5
```

Reason, from reading `app/src/trace/SessionCodec.cpp` rather than the record's
description of it: the index is not a flat key=value file. It is a
**sectioned** one — `decodeIndex` dispatches every key on the current section
and **returns `Malformed` for any key seen outside a section** (the final
`else` at the end of the parse loop). Dotted top-level keys would need a new
parser mode; a `[pane]` section is the format the file already has, and matches
`[capture]` and `[entry]` exactly. The record's decision is the *shape* — an
ordered list of `{view, weight}`, normalised on read, unknown name tolerated —
and that shape is implemented in full. Nothing has ever written a workspace to
disk, so there is no compatibility to preserve. The handoff's trap 7 applies:
a literal in a planning document is a suggestion; the test asserts the
behaviour.

**The asymmetry the record demands lands in the layer that can express it.**
`decodeIndex` refuses unknown *values* everywhere else it has them
(`calibrationUnit`, `visible`) because a guessed measurement is a plausible
lie. A guessed layout is not. So `SessionCodec` stores `PaneSpec::view` as a
**verbatim string and validates nothing about it**, and `resolvePaneView()` in
`view/PaneRegistry.h` does the fallback-and-report. The codec keeps its "refuse
what you do not understand" rule intact; the view layer keeps the record's
"never refuse a session over a layout word" rule. Neither has to bend.

---

## Task 1 — A generic weighted vertical split, in `az_ui`

Record decision 6, "The one `az_ui` addition". This is the module's first piece
of layout math, and it is the task most likely to quietly break the portability
rule — so it gets built first, alone, with its own test target.

**Files:**
- Create: `ui/az_ui/theme/Layout.h`, `ui/az_ui/theme/Layout.cpp`
- Create: `ui/tests/CMakeLists.txt`, `ui/tests/test_layout.cpp`
- Modify: `ui/az_ui/az_ui.h` (include), `ui/az_ui/az_ui.cpp` (compile the .cpp)
- Modify: root `CMakeLists.txt` (add `ui/tests` inside the `RTA_BUILD_APP` block)

**Interfaces:**
- Consumes: nothing.
- Produces: `az::ui::splitVertically(juce::Rectangle<int> area,
  std::span<const float> weights, int gapPx) -> std::vector<juce::Rectangle<int>>`.
  Used by task 10's `WorkspaceView`.

  **The parameter is `gapPx`, not `gap`.** `az::ui::gap` is already a
  namespace-scope constant in `theme/Metrics.h`, and a parameter of that name
  inside `namespace az::ui` hides it — MSVC C4459, which breaks this project's
  zero-warning-at-/W4 baseline. Callers passing `az::ui::gap` as the argument
  are the normal case, which is exactly why the collision is easy to write and
  easy to miss.

- [ ] **Step 1: Write the failing test**

`ui/tests/test_layout.cpp`:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- ui/tests. Tests az_ui's own layout math. This file must
// contain no measurement vocabulary either: az_ui is portable, and a test that
// only makes sense for an audio analyser is a portability claim with a hole
// in it.
#include <az_ui/az_ui.h>

#include <catch2/catch_test_macros.hpp>

#include <vector>

TEST_CASE("splitVertically gives every child the full width", "[layout]") {
    const juce::Rectangle<int> area(10, 20, 300, 400);
    const std::vector<float> weights{ 1.0f, 1.0f };
    const auto rects = az::ui::splitVertically(area, weights, 0);

    REQUIRE(rects.size() == 2u);
    for (const auto& r : rects) {
        CHECK(r.getX() == area.getX());
        CHECK(r.getWidth() == area.getWidth());
    }
}

TEST_CASE("splitVertically divides height in proportion to the weights",
          "[layout]") {
    // 3:1 over 400 px with no gap -> 300 and 100. Chosen so the exact answer
    // is an integer and the assertion needs no tolerance to hide a rounding
    // bug behind.
    const juce::Rectangle<int> area(0, 0, 100, 400);
    const std::vector<float> weights{ 3.0f, 1.0f };
    const auto rects = az::ui::splitVertically(area, weights, 0);

    REQUIRE(rects.size() == 2u);
    CHECK(rects[0].getHeight() == 300);
    CHECK(rects[1].getHeight() == 100);
}

TEST_CASE("splitVertically consumes the area exactly, gaps included",
          "[layout]") {
    // The property that actually matters on screen: no pixel row is left
    // unpainted between two panes, and no pane overhangs the bottom. Asserted
    // over heights that do NOT divide evenly, which is where a naive
    // round-each-independently implementation leaks a row.
    //
    // The origin is deliberately NOT (0, 0). With it at the origin, every
    // assertion below holds for an implementation that ignores area.getY()
    // entirely and stacks from zero -- the whole vertical placement of the
    // split would be asserted by nothing. A nonzero y costs one character and
    // makes every assertion in this case cover the origin too.
    for (const int height : { 199, 200, 201, 333, 761 }) {
        const juce::Rectangle<int> area(7, 13, 100, height);
        const std::vector<float> weights{ 5.0f, 3.0f, 2.0f };
        const int gap = 4;
        const auto rects = az::ui::splitVertically(area, weights, gap);

        REQUIRE(rects.size() == 3u);
        CHECK(rects.front().getY() == area.getY());
        CHECK(rects.back().getBottom() == area.getBottom());
        for (std::size_t i = 1; i < rects.size(); ++i) {
            CHECK(rects[i].getY() == rects[i - 1].getBottom() + gap);
        }
    }
}

TEST_CASE("splitVertically refuses to invent a pane out of nothing",
          "[layout]") {
    const juce::Rectangle<int> area(0, 0, 100, 400);

    CHECK(az::ui::splitVertically(area, std::vector<float>{}, 0).empty());

    // A non-positive weight is not a caller error worth dropping a pane over:
    // the caller asked for N children and must get N back, or its own indexing
    // into the result silently shifts by one. A zero-height child is a visible
    // nothing; a missing child is a wrong arrangement.
    //
    // NEGATIVE, not just zero: the implementation clamps with
    // std::max(0.0f, w), and with only a 0.0f in the vector that clamp can be
    // deleted with every test still green. A negative weight is also what a
    // normalised-on-read layout file can actually deliver.
    const std::vector<float> weights{ 1.0f, -1.0f, 1.0f };
    const auto rects = az::ui::splitVertically(area, weights, 0);
    REQUIRE(rects.size() == 3u);
    CHECK(rects[1].getHeight() == 0);
    // The two real panes still split the whole area between them -- a negative
    // weight must not leak height out of the total.
    CHECK(rects[0].getHeight() + rects[2].getHeight() == area.getHeight());

    // Every weight non-positive: nobody expressed a preference, so equal
    // shares. Untested, this branch is a comment with an implementation
    // attached.
    const std::vector<float> noPreference{ 0.0f, 0.0f };
    const auto equal = az::ui::splitVertically(area, noPreference, 0);
    REQUIRE(equal.size() == 2u);
    CHECK(equal[0].getHeight() == equal[1].getHeight());
    CHECK(equal[0].getHeight() + equal[1].getHeight() == area.getHeight());

    // Area shorter than the gaps alone: every child clamps to zero height and
    // none goes negative. juce::Rectangle happily holds a negative height and
    // draws nothing, so this would be invisible until a child component's own
    // resized() divided by it.
    const juce::Rectangle<int> tiny(0, 0, 100, 2);
    const std::vector<float> three{ 1.0f, 1.0f, 1.0f };
    for (const auto& r : az::ui::splitVertically(tiny, three, 10)) {
        CHECK(r.getHeight() >= 0);
    }
}
```

- [ ] **Step 2: Run it and watch it fail to compile**

```bash
cmake -S . -B build-l5c -G "Visual Studio 18 2026" -A x64 -DRTA_BUILD_APP=ON
```

Then build `az_ui_tests`. Expected: a compile error naming `splitVertically`.
A *link* error means the header was written but the `.cpp` is not being
compiled into the module — check `az_ui.cpp`.

- [ ] **Step 3: Write the header**

`ui/az_ui/theme/Layout.h`:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of az_ui -- the SODIUM RACK design system. Generic geometry only:
// nothing in this module may name a measurement quantity (project CLAUDE.md,
// "Module boundaries"). This file splits a rectangle; what goes in the pieces
// is the host application's business and is not describable from here.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <span>
#include <vector>

namespace az::ui {

/// Divide `area` into `weights.size()` stacked children, heights in proportion
/// to `weights`, separated by `gapPx` pixels.
///
/// Named `gapPx` rather than `gap` because `az::ui::gap` is a namespace-scope
/// metric in theme/Metrics.h and is the value most callers will pass here; a
/// parameter of the same name inside this namespace hides it (MSVC C4459), and
/// this project builds warning-free at /W4.
///
/// Positions are accumulated rather than each child being rounded on its own:
/// independent rounding leaves a one-pixel unpainted row between two panes at
/// most heights, which reads on a dark panel as a hairline nobody drew. Each
/// child's bottom is computed from the running fraction of the total, so the
/// last child ends exactly on `area.getBottom()` by construction rather than by
/// luck.
///
/// Returns exactly `weights.size()` rectangles, always. A non-positive weight
/// yields a zero-height child rather than a dropped one: the caller indexes
/// the result against its own child list, and a silently shorter result
/// misaligns every child after it.
///
/// One degenerate case is worth knowing about: when `area` is shorter than the
/// gaps alone, every child clamps to zero height but the y cursor still
/// advances one gap per child, so the trailing zero-height rectangles sit
/// BELOW `area.getBottom()`. Nothing draws -- they have no height -- but a
/// caller positioning something from a returned rectangle's y should not
/// assume the result is contained in `area` at sizes that small.
[[nodiscard]] std::vector<juce::Rectangle<int>> splitVertically(
    juce::Rectangle<int> area, std::span<const float> weights, int gapPx);

}  // namespace az::ui
```

- [ ] **Step 4: Write the implementation**

`ui/az_ui/theme/Layout.cpp`:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Layout.h"

#include <algorithm>
#include <cmath>

namespace az::ui {

std::vector<juce::Rectangle<int>> splitVertically(juce::Rectangle<int> area,
                                                  std::span<const float> weights, int gapPx) {
    std::vector<juce::Rectangle<int>> out;
    if (weights.empty()) return out;
    out.reserve(weights.size());

    const int gapTotal = gapPx * (static_cast<int>(weights.size()) - 1);
    const int usable = std::max(0, area.getHeight() - gapTotal);

    double weightTotal = 0.0;
    for (const float w : weights) weightTotal += std::max(0.0f, w);

    // Every weight non-positive is not a caller error to refuse -- it is
    // "nobody expressed a preference", and equal shares is the only answer
    // that does not privilege one pane for no stated reason.
    const bool equalShares = weightTotal <= 0.0;
    if (equalShares) weightTotal = static_cast<double>(weights.size());

    double consumed = 0.0;
    int y = area.getY();
    for (std::size_t i = 0; i < weights.size(); ++i) {
        const double w = equalShares ? 1.0 : static_cast<double>(std::max(0.0f, weights[i]));
        const double before = consumed;
        consumed += w;

        // Both edges from the running fraction, so child i's bottom and child
        // i+1's top are derived from the SAME accumulated number and cannot
        // disagree by a rounding step.
        const int top = static_cast<int>(std::llround(before / weightTotal * usable));
        const int bottom = static_cast<int>(std::llround(consumed / weightTotal * usable));
        const int height = std::max(0, bottom - top);

        out.emplace_back(area.getX(), y, area.getWidth(), height);
        y += height + gapPx;
    }
    return out;
}

}  // namespace az::ui
```

- [ ] **Step 5: Register the file and the test target**

In `ui/az_ui/az_ui.h`, add `#include "theme/Layout.h"` after `theme/Metrics.h`.
In `ui/az_ui/az_ui.cpp`, add `#include "theme/Layout.cpp"` — the module compiles
its implementation files through that one translation unit, and a `.cpp` not
listed there is simply never built.

`ui/tests/CMakeLists.txt`:

```cmake
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# az_ui's own tests. The module claims to be portable -- liftable into another
# JUCE project with one `juce_add_module` line -- and a portable module that can
# only be tested through this application's test targets is not portable, it is
# this application's GUI in a different folder with a promise attached.
#
# Registered inside root CMakeLists.txt's `if(RTA_BUILD_APP)` block: az_ui and
# the juce:: targets do not exist in the RTA_BUILD_APP=OFF configure at all.
#
# No `include(Catch)` here -- core/tests/CMakeLists.txt loads that module once
# for the whole configure run and root CMakeLists.txt orders it first. Same
# reasoning as app/tests_juce/CMakeLists.txt.
add_executable(az_ui_tests test_layout.cpp)

target_link_libraries(az_ui_tests PRIVATE
    az_ui
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
    Catch2::Catch2WithMain
)

target_compile_definitions(az_ui_tests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1
)

catch_discover_tests(az_ui_tests TEST_PREFIX "ui/")
```

In root `CMakeLists.txt`, inside the `if(RTA_BUILD_APP)` block, next to the
existing `add_subdirectory(app/tests_juce)`, add `add_subdirectory(ui/tests)`.

- [ ] **Step 6: Run the tests and see them pass**

```bash
ctest --test-dir build-l5c -C Release -R "^ui/" --output-on-failure
```

Expected: 4 cases, 0 failures. Paste the output.

- [ ] **Step 7: Prove the tests can fail**

The handoff's largest lesson: seven green signals in one day asserted nothing,
and reasoning did not catch a single one — running the test against a broken
implementation did. Break `splitVertically` by rounding each child
independently (`height = llround(w / weightTotal * usable)`, `y += height +
gap`) and re-run. The "consumes the area exactly" case must go red on at least
one of the five heights. Restore, re-run, paste both outputs.

- [ ] **Step 8: Check the portability rule held**

```bash
grep -rniE "coherence|trace|magnitude|phase|spectrum|decibel|hertz" ui/
```

Expected: no output. Any hit is the module boundary breaking, and it is easier
to fix now than after three more files depend on it.

- [ ] **Step 9: Commit**

```bash
git add ui/az_ui/theme/Layout.h ui/az_ui/theme/Layout.cpp ui/az_ui/az_ui.h ui/az_ui/az_ui.cpp ui/tests/CMakeLists.txt ui/tests/test_layout.cpp CMakeLists.txt
```

Then commit with message `feat(ui): a split that leaves no seam, and knows
nothing about audio`.

---

## Task 2 — Pane layout math, and the one shared frequency axis

Record decisions 1 and 2. JUCE-free so the geometry is testable with no
component, no peer and no screen — the same reason `PlotGeometry.h` is.

**Files:**
- Create: `app/src/view/AxisMetrics.h` (extracted, see step 0)
- Create: `app/src/view/BodeLayout.h`
- Create: `app/tests/test_bode_layout.cpp`
- Modify: `app/src/view/PlotAxes.h` (include the extracted header)
- Modify: `app/tests/CMakeLists.txt` (test file + two guard paths)

**Interfaces:**
- Consumes: `rta::view::PlotGeometry` (`view/PlotGeometry.h`) and the three
  axis-furniture constants, after step 0 moves them somewhere this file is
  allowed to include from.
- Produces: `PaneRect`, `BodePanes`, `FrequencyAxis`, `bodePanes()`,
  `frequencyAxis()`, `paneGeometry()`. Used by tasks 8 and 10.

- [ ] **Step 0: Extract the axis constants so nothing has to duplicate them**

`BodeLayout.h` needs `kFrequencyLabelHeight`, `kLevelLabelWidth` and
`kFrequencyLabelHalfWidth`. They live in `view/PlotAxes.h`, which includes
`juce_gui_basics` — an include this file's own guard forbids.

Copying the three values into `BodeLayout.h` with a test asserting they still
match would work, and it is what an earlier draft of this plan called for. It
is the wrong answer: two constants that must agree are two constants that will
eventually disagree, and a test that catches the disagreement is a smaller
version of the same problem. Move them instead.

Create `app/src/view/AxisMetrics.h` — JUCE-free, carrying the three constants
and **their existing comments verbatim** — and make `view/PlotAxes.h` include
it rather than declare them. `PlotAxes.h`'s own users are unaffected: the names
stay in `rta::view` and stay visible through the same include.

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. No JUCE: enforced by the
// measure_has_no_framework_deps ctest.
//
// The plot's axis furniture, split out of PlotAxes.h so a framework-free
// caller can have the numbers without the drawing. PlotAxes.h includes
// juce_gui_basics because it draws; BodeLayout.h only needs to know how much
// room the labels take, and the guard forbids it the framework. One
// definition, two readers -- not two definitions and a test hoping they agree.
#pragma once

namespace rta::view {

inline constexpr int kFrequencyLabelHeight = 18;
inline constexpr int kLevelLabelWidth = 40;
inline constexpr float kFrequencyLabelHalfWidth = 26.0f;

}  // namespace rta::view
```

Register `AxisMetrics.h` in the guard's `GLOBS` alongside `BodeLayout.h`, and
confirm the whole tree still builds — `PlotAxes.cpp`, `RtaView.cpp` and the
three preview mockups all read these names.

- [ ] **Step 1: Write the failing test**

`app/tests/test_bode_layout.cpp`:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/tests. Decisions 1 and 2 of
// docs/dsp/2026-08-29-display-layer-l5c.md.
#include "view/BodeLayout.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using rta::view::BodePanes;
using rta::view::PaneRect;

namespace {
constexpr int kGap = 6;
}

TEST_CASE("the coherence ribbon keeps a fixed height at every window size",
          "[bode-layout]") {
    // Decision 2: the ribbon is furniture, like an axis strip. Scaling it with
    // the window buys nothing, so it is the ONE pane whose height is a
    // constant -- and the mockup's mistake was making the magnitude pane one
    // too.
    for (const int height : { 300, 500, 760, 1200, 2000 }) {
        const PaneRect content{ 0, 0, 1000, height };
        const BodePanes panes = rta::view::bodePanes(content, kGap);
        CHECK(panes.ribbon.height == rta::view::kRibbonHeight);
    }
}

TEST_CASE("magnitude and phase split the remaining height 5:3", "[bode-layout]") {
    // The RULE, not the outcome at one size. The mockup's fixed 380 px
    // magnitude pane happened to give ~62/38 at 1100x760 and collapsed the
    // phase pane at any shorter height; this asserts the proportion holds
    // across a 6x range of window heights.
    //
    // 307 is in the list on purpose and must stay: it is the height at which
    // a CORRECT implementation produces |cross| = 7 (available = 243,
    // magnitude = 151, phase = 92, cross = -7). Every other height here lands
    // on a residue that keeps |cross| at 0 or 4, so without 307 the bound
    // below is asserted by nothing and can be quietly tightened back to a
    // number that fails on correct code.
    for (const int height : { 300, 307, 500, 760, 1200, 2000 }) {
        const PaneRect content{ 0, 0, 1000, height };
        const BodePanes panes = rta::view::bodePanes(content, kGap);

        REQUIRE(panes.magnitude.height > 0);
        REQUIRE(panes.phase.height > 0);

        // 5:3 as an integer cross-multiplication rather than a float ratio
        // with a loose epsilon, which would pass for 6:3 at small heights.
        //
        // The bound is 7, and that number is derived, not chosen. With
        // magnitude = floor(5a/8) and phase = a - magnitude over an available
        // height a:
        //     cross = 3*magnitude - 5*phase = 8*floor(5a/8) - 5a = -(5a mod 8)
        // which ranges over 0..-7. A CORRECT implementation therefore produces
        // |cross| = 7 whenever 5a is 1 (mod 8). A tighter bound is not a
        // stricter test, it is a test that fails on correct code at heights
        // nobody happened to pick -- an earlier draft used 5, which survives
        // only because these five heights all land on residues {0, 4}.
        //
        // Discrimination is unaffected: a 6:3 split gives |cross| near 76 at
        // height 300, two orders away from the bound.
        const int cross = panes.magnitude.height * 3 - panes.phase.height * 5;
        CHECK(std::abs(cross) <= 7);
    }
}

TEST_CASE("panes stack without overlapping and stay inside the content",
          "[bode-layout]") {
    for (const int height : { 300, 500, 760, 1200 }) {
        const PaneRect content{ 12, 34, 1000, height };
        const BodePanes panes = rta::view::bodePanes(content, kGap);

        CHECK(panes.ribbon.y >= content.y);
        CHECK(panes.magnitude.y >= panes.ribbon.y + panes.ribbon.height);
        CHECK(panes.phase.y >= panes.magnitude.y + panes.magnitude.height);
        // The frequency label strip belongs to the bottom pane and is reserved
        // BELOW the phase pane's plot area -- so the phase pane's own bottom
        // must clear the content bottom by at least that strip.
        CHECK(panes.phase.y + panes.phase.height
              <= content.y + content.height - rta::view::kFrequencyLabelHeight);
    }
}

TEST_CASE("a window too short for the furniture yields no negative pane",
          "[bode-layout]") {
    // A negative height is not caught by drawing -- juce::Rectangle holds it
    // and paints nothing -- so it surfaces later, as a divide-by-zero inside
    // whatever computes a dB-per-pixel scale. Catch it here.
    for (const int height : { 0, 10, 40, 60, 80 }) {
        const PaneRect content{ 0, 0, 1000, height };
        const BodePanes panes = rta::view::bodePanes(content, kGap);
        CHECK(panes.ribbon.height >= 0);
        CHECK(panes.magnitude.height >= 0);
        CHECK(panes.phase.height >= 0);
        // The ribbon is a FIXED height, which at these sizes means it must
        // still be clamped to what the content actually has. Without this,
        // `std::min(kRibbonHeight, ...)` can be deleted with every other
        // assertion here still green -- an unclamped 34 satisfies `>= 0` just
        // as happily as a correct 10 does.
        CHECK(panes.ribbon.height <= content.height);
    }
}

TEST_CASE("every pane maps frequency to the same pixel column", "[bode-layout]") {
    // Decision 1's load-bearing half. Stacking costs half the vertical
    // resolution per quantity, and the ONLY thing that buys back is a dip and
    // its phase swing landing on the same pixel column. Panes with
    // independently computed x mappings that disagree by a pixel would destroy
    // that silently -- nothing on screen would look wrong.
    const PaneRect content{ 12, 34, 1000, 760 };
    const BodePanes panes = rta::view::bodePanes(content, kGap);
    const auto axis = rta::view::frequencyAxis(content);

    const auto ribbon = rta::view::paneGeometry(axis, panes.ribbon, 1.0, 0.0);
    const auto magnitude = rta::view::paneGeometry(axis, panes.magnitude, 18.0, -18.0);
    const auto phase = rta::view::paneGeometry(axis, panes.phase, 180.0, -180.0);

    for (const double hz : { 20.0, 100.0, 1000.0, 2000.0, 20000.0 }) {
        CHECK(ribbon.xForHz(hz) == magnitude.xForHz(hz));
        CHECK(magnitude.xForHz(hz) == phase.xForHz(hz));
    }
}

TEST_CASE("paneGeometry takes its x mapping from the axis, never from the pane",
          "[bode-layout]") {
    // The mutation this exists to kill: deriving left/right from the pane
    // rectangle instead of the shared axis. That passes every assertion above
    // today, because all three panes currently share the content's x and
    // width -- and breaks the moment any pane gains an inset. Feed a pane a
    // deliberately different x and width and assert the mapping does not move.
    const PaneRect content{ 12, 34, 1000, 760 };
    const auto axis = rta::view::frequencyAxis(content);

    const PaneRect normal{ 12, 100, 1000, 200 };
    const PaneRect inset{ 212, 100, 600, 200 };

    const auto a = rta::view::paneGeometry(axis, normal, 18.0, -18.0);
    const auto b = rta::view::paneGeometry(axis, inset, 18.0, -18.0);

    CHECK(a.left == b.left);
    CHECK(a.right == b.right);

    // The pane still owns its own VERTICAL extent -- that is what a pane is.
    // Both panes above sit at the same y on purpose, so the x assertions are
    // the only thing the inset can move; a third pane at a different y proves
    // the y half is still read from the pane and not from the axis.
    const PaneRect lower{ 12, 400, 1000, 200 };
    const auto c = rta::view::paneGeometry(axis, lower, 18.0, -18.0);
    CHECK(c.top == static_cast<float>(lower.y));
    CHECK(c.bottom == static_cast<float>(lower.y + lower.height));
    CHECK(c.left == a.left);
}

TEST_CASE("the dB range belongs to the pane, not the axis", "[bode-layout]") {
    const PaneRect content{ 0, 0, 1000, 760 };
    const auto axis = rta::view::frequencyAxis(content);
    const PaneRect pane{ 0, 100, 1000, 200 };

    const auto magnitude = rta::view::paneGeometry(axis, pane, 18.0, -18.0);
    const auto phase = rta::view::paneGeometry(axis, pane, 180.0, -180.0);

    CHECK(magnitude.dbTop == Catch::Approx(18.0));
    CHECK(phase.dbTop == Catch::Approx(180.0));
    // Whole-degree readouts (project CLAUDE.md): +-180 with 90-degree ticks
    // divides exactly, so the axis never needs a fractional label.
    CHECK(phase.yForDb(0.0) == Catch::Approx((phase.top + phase.bottom) / 2.0f));
}
```

- [ ] **Step 2: Run it and watch it fail**

Build `rtatool_analysis_tests`. Expected: compile error, no such file
`view/BodeLayout.h`.

- [ ] **Step 3: Write the header**

`app/src/view/BodeLayout.h`:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. No JUCE: enforced by the
// measure_has_no_framework_deps ctest. Decisions 1 and 2 of
// docs/dsp/2026-08-29-display-layer-l5c.md.
#pragma once

#include "view/AxisMetrics.h"
#include "view/PlotGeometry.h"

#include <algorithm>

namespace rta::view {

/// An integer pixel rectangle, in the component's own coordinates.
///
/// Not `juce::Rectangle<int>`, which would drag juce_graphics into a file the
/// guard requires to be framework-free -- and not a float rectangle either: a
/// pane boundary lands on a pixel row or it draws a hairline seam, so integers
/// are the honest type for the thing being computed. The float-valued
/// `PlotGeometry` takes over once the DRAWING starts, where sub-pixel positions
/// are what a stroked path actually wants.
struct PaneRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    [[nodiscard]] constexpr int right() const noexcept { return x + width; }
    [[nodiscard]] constexpr int bottom() const noexcept { return y + height; }
};

/// The three stacked panes of the Bode composite, top to bottom.
struct BodePanes {
    PaneRect ribbon;
    PaneRect magnitude;
    PaneRect phase;
};

/// The shared log-frequency mapping. Decision 1: ONE function computes
/// `left`/`right`/`fLowHz`/`fHighHz`, and every pane's `PlotGeometry` is built
/// from it -- panes with independently computed x mappings that disagree by a
/// pixel would silently destroy the only compensation stacking has.
struct FrequencyAxis {
    float left = 0.0f;
    float right = 0.0f;
    double fLowHz = 20.0;
    double fHighHz = 20000.0;
};

/// Furniture, not data: the ribbon is an axis strip and does not scale with the
/// window (decision 2). Same value the mockup used, kept because 34 px is what
/// reads as a strip rather than a band at six feet -- the mockup's height was
/// never the thing that was wrong, its magnitude PANE was.
inline constexpr int kRibbonHeight = 34;

/// 5 : 3 = 62.5 / 37.5, which keeps the default-size composite essentially what
/// the owner has already seen while replacing the constant that produced it.
/// Magnitude takes the larger share because that is where the 0.1 dB readout
/// rule has to stay resolvable; phase at a fixed +-180 axis needs less
/// resolution per pixel to be read to the nearest few degrees. This is a
/// judgement -- no standard allocates Bode pane heights -- and if a draggable
/// splitter ever ships it moves the weight, not the rule.
inline constexpr int kMagnitudeWeight = 5;
inline constexpr int kPhaseWeight = 3;

/// Split `content` into ribbon / magnitude / phase.
///
/// The frequency label strip is reserved from the BOTTOM before the
/// magnitude-phase split, exactly as the mockup does: it belongs to the
/// bottom-most pane only, and taking it out first is what lets both panes keep
/// an identical plot height rule.
[[nodiscard]] inline BodePanes bodePanes(PaneRect content, int gap) noexcept {
    BodePanes panes;

    const int ribbonHeight = std::min(kRibbonHeight, std::max(0, content.height));
    panes.ribbon = { content.x, content.y, content.width, ribbonHeight };

    int y = content.y + ribbonHeight + gap;
    const int available =
        std::max(0, content.bottom() - kFrequencyLabelHeight - y - gap);

    // Both edges from one accumulated fraction, for the same reason
    // az::ui::splitVertically does it: rounding the two panes independently
    // leaves an unpainted row between them at most heights.
    constexpr int total = kMagnitudeWeight + kPhaseWeight;
    const int magnitudeHeight = available * kMagnitudeWeight / total;
    const int phaseHeight = available - magnitudeHeight;

    panes.magnitude = { content.x, y, content.width, magnitudeHeight };
    y += magnitudeHeight + gap;
    panes.phase = { content.x, y, content.width, phaseHeight };
    return panes;
}

/// The shared x mapping, computed ONCE per composite.
[[nodiscard]] inline FrequencyAxis frequencyAxis(PaneRect content, double fLowHz = 20.0,
                                                 double fHighHz = 20000.0) noexcept {
    FrequencyAxis axis;
    axis.left = static_cast<float>(content.x + kLevelLabelWidth);
    axis.right = static_cast<float>(content.right() - static_cast<int>(kFrequencyLabelHalfWidth));
    axis.fLowHz = fLowHz;
    axis.fHighHz = fHighHz;
    return axis;
}

/// A pane's full `PlotGeometry`: x from the shared axis, y from the pane.
///
/// `pane.x` and `pane.width` are deliberately unread. A pane that later gains a
/// horizontal inset must NOT get its own frequency mapping -- that is the exact
/// defect decision 1 is defended against, and `test_bode_layout.cpp` asserts
/// this function ignores those two fields.
[[nodiscard]] inline PlotGeometry paneGeometry(const FrequencyAxis& axis, PaneRect pane,
                                               double dbTop, double dbBottom) noexcept {
    PlotGeometry geometry;
    geometry.left = axis.left;
    geometry.right = axis.right;
    geometry.top = static_cast<float>(pane.y);
    geometry.bottom = static_cast<float>(pane.bottom());
    geometry.fLowHz = axis.fLowHz;
    geometry.fHighHz = axis.fHighHz;
    geometry.dbTop = dbTop;
    geometry.dbBottom = dbBottom;
    return geometry;
}

}  // namespace rta::view
```

- [ ] **Step 4: Register the file with the test target and the guard**

In `app/tests/CMakeLists.txt`, add `test_bode_layout.cpp` to
`add_executable(rtatool_analysis_tests ...)`, and append
`${CMAKE_CURRENT_SOURCE_DIR}/../src/view/BodeLayout.h` to the
`measure_has_no_framework_deps` `GLOBS` list.

- [ ] **Step 5: Run the tests and the guard**

```bash
build-l5c/app/tests/Release/rtatool_analysis_tests.exe "[bode-layout]"
```

```bash
ctest --test-dir build-l5c -C Release -R "framework_deps" -V
```

Expected: the bode-layout cases pass, and `measure_has_no_framework_deps`
reports **two more** files scanned than before (this task registers two:
`AxisMetrics.h` and `BodeLayout.h`). **If the number did not move, the
path in `GLOBS` is wrong and the guard has silently stopped watching your new
file while still printing OK.** Paste the scanned count.

- [ ] **Step 6: Prove the shared-axis test can fail**

Change `paneGeometry` to take `left` from `pane.x + kLevelLabelWidth`. Re-run.
The "takes its x mapping from the axis" case must go red; the other x cases
must stay green (they cannot see the difference — that is why the mutation test
exists). Restore, re-run, paste both.

- [ ] **Step 7: Commit**

```bash
git add app/src/view/BodeLayout.h app/tests/test_bode_layout.cpp app/tests/CMakeLists.txt
```

Message: `feat(app): three panes, one frequency axis, a proportion instead of a
constant`.

## Task 3 — Phase decimation: unwrap along the bin axis, wrap at draw

Record decision 5, and the highest-risk task in the lane. `decimateToColumns`
as it stands is **wrong for wrapped phase**: min/max of +179° and −179°
manufactures a ~358° extent that was never measured. This task builds phase its
own path and pins the defect with a test that would go red if anybody ever
reuses the magnitude decimator here.

**Files:**
- Create: `app/src/view/PhaseDecimator.h`
- Create: `app/tests/test_phase_decimator.cpp`
- Modify: `app/tests/CMakeLists.txt` (test file + guard path)

**Interfaces:**
- Consumes: `rta::view::ColumnExtent` idea only — this file does NOT include
  `TraceDecimator.h`; the two paths are deliberately separate types so a
  future edit cannot cross-wire them.
- Produces: `PhaseColumn`, `DrawnPhaseColumn`, `unwrapAlongBins()`,
  `decimatePhaseToColumns()`, `wrapForDrawing()`, `wrapTo180()`. Used by
  tasks 7 and 8.

- [ ] **Step 1: Write the failing test**

`app/tests/test_phase_decimator.cpp`:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/tests. Decision 5 of
// docs/dsp/2026-08-29-display-layer-l5c.md.
#include "view/PhaseDecimator.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

using rta::view::DrawnPhaseColumn;
using rta::view::PhaseColumn;

TEST_CASE("wrapTo180 uses core's own half-open convention", "[phase-decimator]") {
    // rta::dsp::TransferSnapshot::phaseRadians is documented as wrapped to
    // (-pi, pi]. Matching that here -- rather than [-180, 180) -- means a
    // value that survived the engine's wrap is a fixed point of this one, so a
    // trace can be wrapped twice with no drift at the boundary.
    CHECK(rta::view::wrapTo180(0.0f) == Catch::Approx(0.0f));
    CHECK(rta::view::wrapTo180(180.0f) == Catch::Approx(180.0f));
    CHECK(rta::view::wrapTo180(-180.0f) == Catch::Approx(180.0f));
    CHECK(rta::view::wrapTo180(190.0f) == Catch::Approx(-170.0f));
    CHECK(rta::view::wrapTo180(-190.0f) == Catch::Approx(170.0f));
    CHECK(rta::view::wrapTo180(725.0f) == Catch::Approx(5.0f));
}

TEST_CASE("unwrapAlongBins recovers a pure delay's straight line",
          "[phase-decimator]") {
    // Closed form, exact by construction. A pure delay of D samples has
    // phi(f) = -2*pi*f*D/fs; at bin k of an N-point transform, f = k*fs/N, so
    //     phi(k) = -360 * k * D / N  degrees,
    // with no reference to fs at all. Choosing D/N = 64/1024 = 1/16 makes that
    // -22.5 degrees per bin EXACTLY -- an exact float, so the assertion needs
    // no tolerance to hide a rounding bug behind, and every step is 22.5 deg,
    // comfortably under the 180 deg the unwrap needs to stay unambiguous.
    constexpr int kBins = 65;
    constexpr float kDegPerBin = -22.5f;

    std::vector<float> wrapped(kBins);
    for (int k = 0; k < kBins; ++k) {
        wrapped[static_cast<std::size_t>(k)] =
            rta::view::wrapTo180(kDegPerBin * static_cast<float>(k));
    }

    const auto unwrapped = rta::view::unwrapAlongBins(wrapped);
    REQUIRE(unwrapped.size() == wrapped.size());
    for (int k = 0; k < kBins; ++k) {
        CHECK(unwrapped[static_cast<std::size_t>(k)]
              == Catch::Approx(kDegPerBin * static_cast<float>(k)).margin(1e-3));
    }
}

TEST_CASE("a column of alternating +179/-179 spans two degrees, not 358",
          "[phase-decimator]") {
    // THE test this file exists for. Min/max taken on WRAPPED values reports
    // an extent of ~358 degrees -- a near-full-height band drawn across the
    // pane -- for data that never moved more than two degrees. It is listed
    // explicitly, like the dual-FFT record's identically-1.0 coherence test,
    // because it is the failure a plausible implementation actually makes.
    const std::vector<float> wrapped{ 179.0f, -179.0f, 179.0f, -179.0f, 179.0f };
    const std::vector<int> columnForBin{ 0, 0, 0, 0, 0 };

    const auto columns = rta::view::decimatePhaseToColumns(wrapped, columnForBin, 1);
    REQUIRE(columns.size() == 1u);
    REQUIRE(columns[0].hasData);
    CHECK((columns[0].maxDeg - columns[0].minDeg) == Catch::Approx(2.0f).margin(1e-3));

    // And at draw time it is a wrap straddle -- two short pieces at the top and
    // bottom edges of the pane -- never a full-height band.
    const auto drawn = rta::view::wrapForDrawing(columns);
    REQUIRE(drawn.size() == 1u);
    CHECK(drawn[0].straddlesWrap);
    CHECK_FALSE(drawn[0].fullBand);
}

TEST_CASE("a column rotating more than a full turn draws as a band",
          "[phase-decimator]") {
    // Decision 5: phase rotating faster than one pixel column can resolve is
    // honestly rendered as a full-height band. Built from -100 deg steps so
    // every individual step stays unambiguous to the unwrap while the column's
    // total extent reaches 400 deg.
    std::vector<float> wrapped;
    for (int i = 0; i < 5; ++i) {
        wrapped.push_back(rta::view::wrapTo180(static_cast<float>(-100 * i)));
    }
    const std::vector<int> columnForBin(wrapped.size(), 0);

    const auto columns = rta::view::decimatePhaseToColumns(wrapped, columnForBin, 1);
    REQUIRE(columns.size() == 1u);
    CHECK((columns[0].maxDeg - columns[0].minDeg) == Catch::Approx(400.0f).margin(1e-3));

    const auto drawn = rta::view::wrapForDrawing(columns);
    REQUIRE(drawn[0].fullBand);
    CHECK(drawn[0].minDeg == Catch::Approx(-180.0f));
    CHECK(drawn[0].maxDeg == Catch::Approx(180.0f));
}

TEST_CASE("the pen lifts exactly where the drawn trace wraps", "[phase-decimator]") {
    // A -50 deg/bin ramp, two bins per column, four columns. Unwrapped column
    // midpoints are -25, -125, -225, -325; wrapped they are -25, -125, +135,
    // +35. Only the -125 -> +135 step exceeds 180 degrees, so exactly one
    // interior pen lift is correct. Every number here is exact.
    std::vector<float> wrapped;
    for (int k = 0; k < 8; ++k) {
        wrapped.push_back(rta::view::wrapTo180(static_cast<float>(-50 * k)));
    }
    const std::vector<int> columnForBin{ 0, 0, 1, 1, 2, 2, 3, 3 };

    const auto drawn =
        rta::view::wrapForDrawing(rta::view::decimatePhaseToColumns(wrapped, columnForBin, 4));
    REQUIRE(drawn.size() == 4u);

    CHECK(drawn[0].penLift);        // nothing to the left to connect to
    CHECK_FALSE(drawn[1].penLift);
    CHECK(drawn[2].penLift);        // -125 -> +135 is the wrap
    CHECK_FALSE(drawn[3].penLift);
}

TEST_CASE("a column no bin lands in draws nothing", "[phase-decimator]") {
    // Same rule TraceDecimator states for magnitude: absence is not zero. A
    // phase of 0 degrees drawn where nothing was measured is a straight line
    // through the middle of the pane, which reads as a perfectly aligned
    // system.
    const std::vector<float> wrapped{ 10.0f, 20.0f };
    const std::vector<int> columnForBin{ 0, 2 };

    const auto columns = rta::view::decimatePhaseToColumns(wrapped, columnForBin, 3);
    REQUIRE(columns.size() == 3u);
    CHECK(columns[0].hasData);
    CHECK_FALSE(columns[1].hasData);
    CHECK(columns[2].hasData);

    const auto drawn = rta::view::wrapForDrawing(columns);
    CHECK_FALSE(drawn[1].hasData);
    // The column after a hole cannot connect across it either.
    CHECK(drawn[2].penLift);
}

TEST_CASE("out-of-range column indices are skipped, not crashed on",
          "[phase-decimator]") {
    // Same contract decimateToColumns documents: the mapping is the caller's,
    // and a bin outside the plotted range carries -1.
    const std::vector<float> wrapped{ 10.0f, 20.0f, 30.0f };
    const std::vector<int> columnForBin{ -1, 0, 7 };
    const auto columns = rta::view::decimatePhaseToColumns(wrapped, columnForBin, 2);
    REQUIRE(columns.size() == 2u);
    CHECK(columns[0].hasData);
    CHECK_FALSE(columns[1].hasData);
}

TEST_CASE("mismatched inputs produce nothing rather than a guess",
          "[phase-decimator]") {
    const std::vector<float> wrapped{ 10.0f, 20.0f };
    const std::vector<int> shortMapping{ 0 };
    CHECK(rta::view::decimatePhaseToColumns(wrapped, shortMapping, 2).empty());
    CHECK(rta::view::decimatePhaseToColumns(wrapped, std::vector<int>{ 0, 0 }, 0).empty());
}
```

- [ ] **Step 2: Run it and watch it fail**

Build `rtatool_analysis_tests`. Expected: no such file `view/PhaseDecimator.h`.

- [ ] **Step 3: Write the header**

`app/src/view/PhaseDecimator.h`:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. No JUCE: enforced by the
// measure_has_no_framework_deps ctest. Decision 5 of
// docs/dsp/2026-08-29-display-layer-l5c.md.
//
// This file deliberately does NOT include view/TraceDecimator.h and does not
// reuse ColumnExtent. Phase and magnitude need genuinely different reductions,
// and a shared type is the shortest path back to somebody calling the
// magnitude decimator on wrapped phase -- the exact defect this file exists to
// prevent.
#pragma once

#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

namespace rta::view {

/// Wrap to (-180, 180] -- the degree form of core's own (-pi, pi] convention
/// for `rta::dsp::TransferSnapshot::phaseRadians`. A value that already
/// survived the engine's wrap is a fixed point of this function, so wrapping
/// twice cannot drift at the boundary.
[[nodiscard]] inline float wrapTo180(float deg) noexcept {
    float w = std::fmod(deg + 180.0f, 360.0f);
    if (w <= 0.0f) w += 360.0f;
    return w - 180.0f;
}

/// One pixel column's phase extent, in UNWRAPPED degrees. `hasData` is not
/// redundant with a zero extent: a column no bin lands in must draw nothing,
/// and a phase of 0 degrees drawn where nothing was measured is a flat line
/// through the middle of the pane, which reads as a perfectly aligned system.
struct PhaseColumn {
    float minDeg = 0.0f;
    float maxDeg = 0.0f;
    bool hasData = false;
};

/// A column ready to draw, in wrapped degrees.
///
/// `straddlesWrap` means the extent crosses the +-180 boundary, so the column
/// is the union of [minDeg, +180] and [-180, maxDeg] -- two short pieces at the
/// two edges of the pane. `fullBand` means the UNWRAPPED extent reached a whole
/// turn: phase is rotating faster than one pixel column can resolve, and a
/// full-height band is the honest rendering of that (decision 5).
struct DrawnPhaseColumn {
    float minDeg = 0.0f;
    float maxDeg = 0.0f;
    bool hasData = false;
    bool straddlesWrap = false;
    bool fullBand = false;
    /// Do not draw a connecting line from the previous column to this one.
    bool penLift = false;
};

/// Running unwrap along the bin axis: each bin adjusted by the multiple of 360
/// that brings it within 180 degrees of its predecessor.
///
/// A view-side operation on a LOCAL COPY, permitted precisely because dual-FFT
/// record section 6 put unwrap in the view and out of every accumulator. The
/// stored wrapped values are never modified, so the one-bad-bin fragility this
/// carries corrupts one rebuild of one cached image and no more -- the next
/// revision bump rebuilds from the stored values again.
[[nodiscard]] inline std::vector<float> unwrapAlongBins(std::span<const float> wrappedDeg) {
    std::vector<float> out(wrappedDeg.size());
    if (wrappedDeg.empty()) return out;

    out[0] = wrappedDeg[0];
    for (std::size_t i = 1; i < wrappedDeg.size(); ++i) {
        float delta = wrappedDeg[i] - wrappedDeg[i - 1];
        delta -= 360.0f * std::round(delta / 360.0f);
        out[i] = out[i - 1] + delta;
    }
    return out;
}

/// Unwrap along bins, then take min/max per column ON THE UNWRAPPED VALUES,
/// where an extent is a true extent.
///
/// The two rejected alternatives, so nobody re-derives them: raw wrapped
/// min/max manufactures a ~358 degree span from +179 and -179; one
/// representative bin per column aliases, because at fftSize 32768 the top
/// decade packs tens of bins into a column and a few milliseconds of delay
/// rotates phase through several full turns inside one of them -- a single
/// sample lands anywhere, frame to frame, and the trace shimmers.
[[nodiscard]] inline std::vector<PhaseColumn> decimatePhaseToColumns(
    std::span<const float> wrappedDeg, std::span<const int> columnForBin, int columnCount) {
    if (columnCount <= 0 || wrappedDeg.empty() || wrappedDeg.size() != columnForBin.size()) {
        return {};
    }

    const std::vector<float> unwrapped = unwrapAlongBins(wrappedDeg);
    std::vector<PhaseColumn> out(static_cast<std::size_t>(columnCount));
    for (std::size_t i = 0; i < unwrapped.size(); ++i) {
        const int column = columnForBin[i];
        if (column < 0 || column >= columnCount) continue;  // caller's mapping, not our crash

        auto& c = out[static_cast<std::size_t>(column)];
        const float v = unwrapped[i];
        if (!c.hasData) {
            c.minDeg = v;
            c.maxDeg = v;
            c.hasData = true;
        } else {
            if (v < c.minDeg) c.minDeg = v;
            if (v > c.maxDeg) c.maxDeg = v;
        }
    }
    return out;
}

/// Map unwrapped column extents back into the +-180 pane, and decide where the
/// pen lifts.
///
/// The pen lifts wherever a connecting line would be an artefact rather than
/// part of the curve: at the first drawn column, after a hole, on either side
/// of a band or a straddle, and wherever the drawn midpoints jump by more than
/// 180 degrees.
[[nodiscard]] inline std::vector<DrawnPhaseColumn> wrapForDrawing(
    std::span<const PhaseColumn> columns) {
    std::vector<DrawnPhaseColumn> out(columns.size());

    bool havePrevious = false;
    float previousMid = 0.0f;

    for (std::size_t i = 0; i < columns.size(); ++i) {
        const PhaseColumn& in = columns[i];
        DrawnPhaseColumn& d = out[i];
        if (!in.hasData) {
            havePrevious = false;  // nothing to connect the NEXT column back to
            continue;
        }
        d.hasData = true;

        if ((in.maxDeg - in.minDeg) >= 360.0f) {
            d.fullBand = true;
            d.minDeg = -180.0f;
            d.maxDeg = 180.0f;
            d.penLift = true;
            havePrevious = false;
            continue;
        }

        d.minDeg = wrapTo180(in.minDeg);
        d.maxDeg = wrapTo180(in.maxDeg);
        d.straddlesWrap = d.minDeg > d.maxDeg;

        const float mid = wrapTo180(0.5f * (in.minDeg + in.maxDeg));
        d.penLift = !havePrevious || d.straddlesWrap || std::abs(mid - previousMid) > 180.0f;

        previousMid = mid;
        havePrevious = !d.straddlesWrap;
    }
    return out;
}

}  // namespace rta::view
```

- [ ] **Step 4: Register and run**

Add `test_phase_decimator.cpp` to `rtatool_analysis_tests` and
`app/src/view/PhaseDecimator.h` to the guard's `GLOBS`.

```bash
build-l5c/app/tests/Release/rtatool_analysis_tests.exe "[phase-decimator]"
```

```bash
ctest --test-dir build-l5c -C Release -R "framework_deps" -V
```

Expected: 8 cases pass; the guard's scanned count moves again. Paste the count.

- [ ] **Step 5: Prove the headline test can fail**

Replace the body of `decimatePhaseToColumns` with min/max taken directly on
`wrappedDeg` (skip the unwrap). Re-run. The +179/−179 case must go red with an
extent near 358. Restore, re-run, paste both outputs. **Do not skip this**: the
handoff records seven green signals that asserted nothing and states plainly
that in every case reasoning said they were fine and only running the test
against broken code found otherwise.

- [ ] **Step 6: Commit**

```bash
git add app/src/view/PhaseDecimator.h app/tests/test_phase_decimator.cpp app/tests/CMakeLists.txt
```

Message: `feat(app): phase columns that span what was measured, not what wrapped`.

---

## Task 4 — Trust to alpha, with a floor

Record decision 3, the continuous half. Small, but it carries the rule that
keeps L5b's thresholds out of L5c.

**Files:**
- Create: `app/src/view/CoherenceAlpha.h`
- Create: `app/tests/test_coherence_alpha.cpp`
- Modify: `app/tests/CMakeLists.txt` (test file + guard path)

**Interfaces:**
- Consumes: `rta::view::decimateToColumns` (`view/TraceDecimator.h`).
- Produces: `kUntrustedAlphaFloor`, `alphaForCoherence()`, `columnAlpha()`.
  Used by tasks 7 and 8.

- [ ] **Step 1: Write the failing test**

`app/tests/test_coherence_alpha.cpp`:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/tests. Decision 3 of
// docs/dsp/2026-08-29-display-layer-l5c.md, continuous half only. Every
// THRESHOLD -- blanking, gating, a 0.95-style default -- is L5b's, and no test
// in this file may assert one.
#include "view/CoherenceAlpha.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <vector>

TEST_CASE("an untrusted region dims but never vanishes", "[coherence-alpha]") {
    // The floor is what separates a FADE from a DELETION. Deleting data
    // quietly is the gate's job, and the gate is required to do it loudly --
    // which is L5b's decision to make, not this file's.
    CHECK(rta::view::alphaForCoherence(0.0f)
          == Catch::Approx(rta::view::kUntrustedAlphaFloor));
    CHECK(rta::view::kUntrustedAlphaFloor > 0.0f);
    CHECK(rta::view::alphaForCoherence(1.0f) == Catch::Approx(1.0f));
}

TEST_CASE("alpha is monotone in coherence", "[coherence-alpha]") {
    // The one property the record actually fixes: "a monotone function of
    // gamma^2 with a floor of 0.25". The particular curve is a judgement; that
    // it never decreases as trust rises is not.
    float previous = -1.0f;
    for (int i = 0; i <= 100; ++i) {
        const float gamma = static_cast<float>(i) / 100.0f;
        const float alpha = rta::view::alphaForCoherence(gamma);
        CHECK(alpha >= previous);
        CHECK(alpha >= rta::view::kUntrustedAlphaFloor);
        CHECK(alpha <= 1.0f);
        previous = alpha;
    }
}

TEST_CASE("nonsense trust is treated as no trust", "[coherence-alpha]") {
    // core clamps coherence to [0,1] and withholds it entirely below the gate,
    // so these cannot arrive from the live engine -- but a stored trace comes
    // off disk, and a NaN that compares false against every bound would sail
    // through an unguarded comparison and paint at FULL confidence.
    CHECK(rta::view::alphaForCoherence(std::numeric_limits<float>::quiet_NaN())
          == Catch::Approx(rta::view::kUntrustedAlphaFloor));
    CHECK(rta::view::alphaForCoherence(-0.5f)
          == Catch::Approx(rta::view::kUntrustedAlphaFloor));
    CHECK(rta::view::alphaForCoherence(4.0f) == Catch::Approx(1.0f));
}

TEST_CASE("a column takes the MINIMUM trust of its bins", "[coherence-alpha]") {
    // Decision 3: trust shown never exceeds trust measured. A mean would let
    // one confident bin carry three noisy ones into looking solid, at exactly
    // the frequencies -- a null, the LF end -- where the operator is deciding
    // whether to move a delay.
    const std::vector<float> coherence{ 1.0f, 0.2f, 1.0f, 0.9f, 0.8f, 0.95f };
    const std::vector<int> columnForBin{ 0, 0, 0, 1, 1, 1 };

    const auto alpha = rta::view::columnAlpha(coherence, columnForBin, 2);
    REQUIRE(alpha.size() == 2u);
    CHECK(alpha[0] == Catch::Approx(rta::view::alphaForCoherence(0.2f)));
    CHECK(alpha[1] == Catch::Approx(rta::view::alphaForCoherence(0.8f)));

    // And it is genuinely the minimum, not the first or the last: reversing
    // each column's contents must not change the answer.
    const std::vector<float> reversed{ 1.0f, 1.0f, 0.2f, 0.95f, 0.8f, 0.9f };
    const auto again = rta::view::columnAlpha(reversed, columnForBin, 2);
    CHECK(again[0] == Catch::Approx(alpha[0]));
    CHECK(again[1] == Catch::Approx(alpha[1]));
}

TEST_CASE("a column with no bins reports no trust", "[coherence-alpha]") {
    // The extent's own hasData decides whether the column draws at all; this
    // only has to avoid handing back a confident alpha for a column nothing
    // measured.
    const std::vector<float> coherence{ 0.9f, 0.9f };
    const std::vector<int> columnForBin{ 0, 2 };
    const auto alpha = rta::view::columnAlpha(coherence, columnForBin, 3);
    REQUIRE(alpha.size() == 3u);
    CHECK(alpha[1] == Catch::Approx(rta::view::kUntrustedAlphaFloor));
}

TEST_CASE("no coherence at all means full confidence is never assumed",
          "[coherence-alpha]") {
    // A trace that carries no coherence field is not a trace measured at
    // gamma^2 = 1. Callers pass an empty span and get an empty result, and
    // task 7's stroking treats an EMPTY alpha vector as "this quantity has no
    // trust information" -- which is a different thing from "trust is zero"
    // and is drawn opaque, because dimming a single-channel RTA capture would
    // be asserting a measurement that was never taken.
    CHECK(rta::view::columnAlpha({}, {}, 4).empty());
}
```

- [ ] **Step 2: Run it and watch it fail**

Expected: no such file `view/CoherenceAlpha.h`.

- [ ] **Step 3: Write the header**

`app/src/view/CoherenceAlpha.h`:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. No JUCE: enforced by the
// measure_has_no_framework_deps ctest. Decision 3 of
// docs/dsp/2026-08-29-display-layer-l5c.md -- the CONTINUOUS mechanism only.
// Every threshold (blanking, gating, a match verdict) is L5b's, and none may
// appear in this file.
#pragma once

#include "view/TraceDecimator.h"

#include <algorithm>
#include <cstddef>
#include <span>
#include <vector>

namespace rta::view {

/// Alpha for a completely untrusted column. Zero would make the display
/// silently DELETE data, which is the gate's job to do loudly; much above this
/// and the fade stops reading as a fade at all. A judgement, and the record
/// says so.
inline constexpr float kUntrustedAlphaFloor = 0.25f;

/// Monotone in gamma^2, floored. Linear is the simplest curve satisfying what
/// the record actually fixes -- monotone, floored at 0.25, 1.0 at full
/// coherence. Anything shaped (a power, a knee) would be a threshold in
/// disguise, and thresholds belong to L5b.
///
/// A NaN fails `> 0.0f` and lands on the floor. That is deliberate and not
/// defensive clutter: core clamps and gates its coherence, but a stored trace
/// comes off disk, and a NaN compares false against every bound -- an
/// unguarded comparison would paint an unmeasurable bin at full confidence.
[[nodiscard]] inline float alphaForCoherence(float gammaSquared) noexcept {
    if (!(gammaSquared > 0.0f)) return kUntrustedAlphaFloor;
    const float t = std::min(gammaSquared, 1.0f);
    return kUntrustedAlphaFloor + (1.0f - kUntrustedAlphaFloor) * t;
}

/// Per-column alpha, the column taking the MINIMUM gamma^2 of its bins:
/// trust shown never exceeds trust measured.
///
/// Reuses `decimateToColumns` rather than growing a second reduction -- its
/// `minValue` is exactly the quantity wanted here. (Phase could not reuse it;
/// see PhaseDecimator.h for why that is a real difference and not an
/// inconsistency.) Columns no bin landed in get the floor; whether such a
/// column draws at all is decided by its extent's `hasData`, not here.
[[nodiscard]] inline std::vector<float> columnAlpha(std::span<const float> coherence,
                                                    std::span<const int> columnForBin,
                                                    int columnCount) {
    const auto columns = decimateToColumns(coherence, columnForBin, columnCount);
    if (columns.empty()) return {};

    std::vector<float> out(columns.size(), kUntrustedAlphaFloor);
    for (std::size_t c = 0; c < columns.size(); ++c) {
        if (columns[c].hasData) out[c] = alphaForCoherence(columns[c].minValue);
    }
    return out;
}

}  // namespace rta::view
```

- [ ] **Step 4: Register, run, and prove it can fail**

Add the test file and the guard path. Run:

```bash
build-l5c/app/tests/Release/rtatool_analysis_tests.exe "[coherence-alpha]"
```

```bash
ctest --test-dir build-l5c -C Release -R "framework_deps" -V
```

Then break it: change `columnAlpha` to use `maxValue`. The minimum test must go
red. Restore. Paste both.

- [ ] **Step 5: Commit**

```bash
git add app/src/view/CoherenceAlpha.h app/tests/test_coherence_alpha.cpp app/tests/CMakeLists.txt
```

Message: `feat(app): trust fades, and never fades to nothing`.

---

## Task 5 — The transfer function reaches `app/`

**This task is the scope addition the owner approved on 2026-08-29**, and it is
not in the decision record. The L2 engine is complete in `core/` and no line of
`app/` calls it; without this, the composite in task 8 draws closed-form curves
and is the mockup with better geometry.

**Files:**
- Modify: `app/src/measure/Snapshot.h` (add `TransferBlock`)
- Modify: `app/src/measure/Analyser.h`, `app/src/measure/Analyser.cpp`
- Modify: `app/tests/test_analyser.cpp`
- Modify: `app/tests/CMakeLists.txt` (nothing new to register — `Analyser.cpp`
  and `Snapshot.h` are already in the guard list; **verify the count does not
  drop**)

**Interfaces:**
- Consumes: `rta::dsp::DualFftEngine`, `rta::dsp::TransferEstimator`
  (`makeSnapshot`, `Estimator`), `rta::dsp::TransferAveraging` — all existing,
  unmodified core.
- Produces: `rta::measure::TransferBlock`, `Snapshot::transfer`,
  `Analyser::pushPair()`. Used by tasks 6, 8 and 10.

- [ ] **Step 1: Write the failing tests**

Append to `app/tests/test_analyser.cpp`:

```cpp
// ---------------------------------------------------------------------------
// Transfer function -- the app-side bridge to rta::dsp::DualFftEngine.
// ---------------------------------------------------------------------------

namespace {

/// Deterministic broadband source. Pink rather than white only because it is
/// what the rest of this file already uses; the transfer function of a pure
/// delay is the same either way.
///
/// `SyntheticPink(blockSize, levelDbFs, seed)` -- a looping block, not an
/// endless stream. That the block repeats does not disturb any assertion here:
/// both channels are drawn from the SAME sequence, one shifted, so
/// y[n] = x[n-D] holds across the loop seam exactly as it does everywhere
/// else, and H = exp(-j*omega*D) with it.
std::vector<float> pinkBlock(std::size_t n, std::uint32_t seed) {
    rta::gen::SyntheticPink pink(8192, -20.0, seed);
    std::vector<float> out(n);
    pink.render(out);
    return out;
}

/// `source` delayed by `delaySamples`, zero-filled at the front. Same length,
/// so the two spans a dual-FFT is handed are the same time instant -- which is
/// the one precondition DualFftEngine refuses to guess about.
std::vector<float> delayed(std::span<const float> source, int delaySamples) {
    std::vector<float> out(source.size(), 0.0f);
    for (std::size_t i = static_cast<std::size_t>(delaySamples); i < source.size(); ++i) {
        out[i] = source[i - static_cast<std::size_t>(delaySamples)];
    }
    return out;
}

rta::measure::Analyser::Config transferConfig() {
    rta::measure::Analyser::Config cfg;
    cfg.fftSize = 4096;
    cfg.hopSize = 2048;
    cfg.sampleRate = 48000.0;
    return cfg;
}

}  // namespace

TEST_CASE("a measurement with no reference carries no transfer function",
          "[analyser][transfer]") {
    // Absence of a reference is absence of a transfer function -- not a flat
    // 0 dB one. A single-channel RTA capture must not produce a Bode plot of
    // itself against nothing.
    rta::measure::Analyser analyser(transferConfig());
    const auto block = pinkBlock(48000, 0x5EEDu);
    analyser.pushMeasurement(block);
    const auto snapshot = analyser.publish(0);

    REQUIRE(snapshot != nullptr);
    CHECK_FALSE(snapshot->transfer.has_value());
}

TEST_CASE("a compensated delay leaves the phase flat", "[analyser][transfer]") {
    // The engine applies referenceDelaySamples BEFORE the transform, as an
    // integer stream offset (dual-FFT record section 4). Compensating a known
    // 4-sample lag must therefore return phase to zero across the band -- and
    // magnitude to 0 dB, since nothing but a delay was applied.
    auto cfg = transferConfig();
    cfg.referenceDelaySamples = 4;

    rta::measure::Analyser analyser(cfg);
    const auto reference = pinkBlock(48000 * 2, 0x5EEDu);
    const auto measurement = delayed(reference, 4);
    analyser.pushPair(reference, measurement);
    const auto snapshot = analyser.publish(0);

    REQUIRE(snapshot != nullptr);
    REQUIRE(snapshot->transfer.has_value());
    const auto& tf = *snapshot->transfer;
    REQUIRE(tf.phaseDeg.size() == snapshot->fftSize / 2 + 1);

    const double binHz = cfg.sampleRate / static_cast<double>(cfg.fftSize);
    for (const double hz : { 1000.0, 2000.0, 4000.0 }) {
        const auto bin = static_cast<std::size_t>(std::llround(hz / binHz));
        CHECK(std::abs(tf.phaseDeg[bin]) < 2.0f);
        CHECK(std::abs(tf.magnitudeDb[bin]) < 1.0f);
    }
}

TEST_CASE("an uncompensated delay gives the closed-form phase slope",
          "[analyser][transfer]") {
    // phi(f) = -2*pi*f*D/fs, i.e. -360*f*D/fs degrees. With D = 4 at 48 kHz
    // that is -30 deg at 1 kHz, -60 at 2 kHz and -120 at 4 kHz -- none of them
    // near the +-180 wrap, so the assertion tests the SLOPE and not the
    // wrapping. This is the identity, not a number the implementation printed.
    auto cfg = transferConfig();
    cfg.referenceDelaySamples = 0;

    rta::measure::Analyser analyser(cfg);
    const auto reference = pinkBlock(48000 * 2, 0x5EEDu);
    const auto measurement = delayed(reference, 4);
    analyser.pushPair(reference, measurement);
    const auto snapshot = analyser.publish(0);

    REQUIRE(snapshot->transfer.has_value());
    const auto& tf = *snapshot->transfer;
    const double binHz = cfg.sampleRate / static_cast<double>(cfg.fftSize);

    for (const double hz : { 1000.0, 2000.0, 4000.0 }) {
        const auto bin = static_cast<std::size_t>(std::llround(hz / binHz));
        const double expected = -360.0 * (static_cast<double>(bin) * binHz) * 4.0 / cfg.sampleRate;
        CHECK(static_cast<double>(tf.phaseDeg[bin]) == Catch::Approx(expected).margin(2.0));
    }
}

TEST_CASE("phase crosses the seam in degrees, not radians", "[analyser][transfer]") {
    // Cheap, and it genuinely falsifies. core hands out radians, wrapped to
    // (-pi, pi], so no radian value can exceed 3.15 -- while the case above
    // expects -120 degrees at 4 kHz. A forgotten conversion turns the whole
    // phase pane into a flat line at the middle of the axis, which looks like
    // a perfectly aligned system rather than like a bug.
    auto cfg = transferConfig();
    rta::measure::Analyser analyser(cfg);
    const auto reference = pinkBlock(48000 * 2, 0x5EEDu);
    analyser.pushPair(reference, delayed(reference, 4));
    const auto snapshot = analyser.publish(0);

    REQUIRE(snapshot->transfer.has_value());
    const auto& phase = snapshot->transfer->phaseDeg;
    const bool anyBeyondRadians =
        std::any_of(phase.begin(), phase.end(), [](float p) { return std::abs(p) > 3.2f; });
    CHECK(anyBeyondRadians);
}

TEST_CASE("coherence is withheld until enough averages exist",
          "[analyser][transfer]") {
    // dual-FFT record section 3: for a single frame |X*Y|^2 == |X|^2|Y|^2
    // identically, so coherence is exactly 1.0 at every frequency and a
    // completely broken engine looks perfect. The gate lives in core's
    // makeSnapshot and is guarded by coherence_gate_is_not_bypassed; this
    // asserts the app-side bridge PROPAGATES the absence rather than
    // substituting an empty vector or a 1.0.
    auto cfg = transferConfig();

    rta::measure::Analyser thin(cfg);
    const auto shortBlock = pinkBlock(cfg.fftSize + cfg.hopSize, 0x5EEDu);
    thin.pushPair(shortBlock, delayed(shortBlock, 4));
    const auto thinSnapshot = thin.publish(0);
    REQUIRE(thinSnapshot->transfer.has_value());
    CHECK_FALSE(thinSnapshot->transfer->coherence.has_value());

    rta::measure::Analyser thick(cfg);
    const auto longBlock = pinkBlock(48000 * 2, 0x5EEDu);
    thick.pushPair(longBlock, delayed(longBlock, 4));
    const auto thickSnapshot = thick.publish(0);
    REQUIRE(thickSnapshot->transfer.has_value());
    REQUIRE(thickSnapshot->transfer->coherence.has_value());

    // Two identical channels differing only by a delay ARE fully coherent, so
    // this is the honest expectation here -- and it is exactly why the
    // low-coherence display test in task 8 uses added noise instead.
    const auto& coherence = *thickSnapshot->transfer->coherence;
    const double binHz = cfg.sampleRate / static_cast<double>(cfg.fftSize);
    const auto bin = static_cast<std::size_t>(std::llround(1000.0 / binHz));
    CHECK(coherence[bin] == Catch::Approx(1.0).margin(0.05));
    CHECK(thickSnapshot->transfer->effectiveAverages > 8.0);
}

TEST_CASE("reset drops the transfer function with everything else",
          "[analyser][transfer]") {
    // A mid-run device change must not splice frames from two different device
    // sessions into one averaged H -- the same reason
    // AnalysisThread::rebuildAnalyserIfEpochChanged exists.
    rta::measure::Analyser analyser(transferConfig());
    const auto block = pinkBlock(48000 * 2, 0x5EEDu);
    analyser.pushPair(block, delayed(block, 4));
    REQUIRE(analyser.publish(0)->transfer.has_value());

    analyser.reset();
    CHECK_FALSE(analyser.publish(0)->transfer.has_value());
}
```

Add `#include "rta/gen/Synthetic.h"`, `<algorithm>`, `<cmath>` and `<span>` to
the test file's include list if not already present.

- [ ] **Step 2: Run and watch it fail**

Expected: `Snapshot` has no member `transfer`; `Analyser` has no `pushPair`.

- [ ] **Step 3: Add `TransferBlock` to the snapshot**

In `app/src/measure/Snapshot.h`, add `#include <optional>` and, after
`spectrumDb`:

```cpp
/// The transfer function of measurement against reference, per bin, when a
/// reference channel was actually being fed. Built from
/// `rta::dsp::TransferSnapshot` in `Analyser::publish`.
///
/// Degrees, not radians: core wraps to (-pi, pi] because that is the natural
/// output of a complex division, and every consumer in `view/` works in
/// degrees because `PlotGeometry`'s phase pane runs +180 to -180. The
/// conversion happens exactly once, at that one seam, so no file downstream
/// has to know which unit it is holding.
struct TransferBlock {
    std::vector<float> magnitudeDb;
    std::vector<float> phaseDeg;

    /// Absent -- not empty, not a vector of 1.0 -- below the engine's
    /// effective-average gate. dual-FFT record section 3: a single frame gives
    /// coherence identically 1.0 at every frequency, so a broken engine looks
    /// perfect, and a sentinel would be plotted.
    std::optional<std::vector<float>> coherence;

    /// Effective, never a raw frame count: overlapped frames are not
    /// independent, and a gate that trusts a raw count opens too early.
    double effectiveAverages = 0.0;

    /// What was compensated BEFORE the transform, carried so a readout can
    /// state it. Not applied to the phase after the fact -- that is the
    /// mistake dual-FFT record section 4 exists to refuse.
    int appliedDelaySamples = 0;
};

/// Absent when no reference was fed. A single-channel capture has no transfer
/// function; it does not have a flat one.
std::optional<TransferBlock> transfer;
```

- [ ] **Step 4: Give `Analyser` the engine**

In `app/src/measure/Analyser.h`: include `rta/dsp/DualFftEngine.h` and
`rta/dsp/TransferEstimator.h`; extend `Config`:

```cpp
        /// Dual-FFT settings. Unused until `pushPair` is called at least once
        /// -- an RTA-only session pays for the engine's buffers and nothing
        /// else, which is cheaper than a second Analyser subclass and far
        /// cheaper than a runtime branch nobody can test.
        rta::dsp::TransferAveraging transferAveraging = rta::dsp::TransferAveraging::Fifo;
        std::size_t transferFifoDepth = 16;
        rta::dsp::Estimator estimator = rta::dsp::Estimator::H1;

        /// Positive = the measurement lags the reference by this many samples.
        /// Compensated as a stream offset before the transform; see
        /// docs/dsp/2026-08-28-dual-fft.md section 4 for why never after.
        int referenceDelaySamples = 0;
```

Add the method and members:

```cpp
    /// Feed one hop of BOTH channels, same time instant.
    ///
    /// Separate from pushMeasurement/pushReference, not a replacement for
    /// them: those two exist for the single-channel RTA path and impose no
    /// pairing, while a dual-FFT's one refusable precondition is that its two
    /// spans ARE the same instant. Passing the channels in separately and
    /// hoping they stay in step is the defect AnalysisThread's paired drain
    /// (task 6) exists to close.
    ///
    /// Throws `std::invalid_argument` (out of DualFftEngine::process) if the
    /// two spans differ in length.
    void pushPair(std::span<const float> reference, std::span<const float> measurement);
```

```cpp
    rta::dsp::DualFftEngine dual_;
    /// False until the first pushPair. Distinct from `dual_.frameCount() > 0`:
    /// a caller can push a pair shorter than one frame, and "a reference was
    /// offered" is a different fact from "a frame was analysed". Both are
    /// required before a transfer function is published.
    bool dualEngaged_ = false;
```

In `Analyser.cpp`, build the engine config in the constructor's init list from
`Config`, implement `pushPair` (feed both spectrum engines *and* `dual_`), clear
`dual_` and `dualEngaged_` in `reset()`, and in `publish()`:

```cpp
    if (dualEngaged_ && dual_.frameCount() > 0) {
        const auto tf = rta::dsp::makeSnapshot(dual_, config_.estimator);
        TransferBlock block;
        block.magnitudeDb = tf.magnitudeDb;
        block.phaseDeg.resize(tf.phaseRadians.size());
        // The one radians -> degrees crossing in the whole application.
        for (std::size_t i = 0; i < tf.phaseRadians.size(); ++i) {
            block.phaseDeg[i] =
                tf.phaseRadians[i] * static_cast<float>(180.0 / std::numbers::pi);
        }
        block.coherence = tf.coherence;   // std::optional copied AS an optional
        block.effectiveAverages = tf.effectiveAverages;
        block.appliedDelaySamples = config_.referenceDelaySamples;
        snapshot->transfer = std::move(block);
    }
```

`Analyser.cpp` needs `#include <numbers>` for `std::numbers::pi`.

**`block.coherence = tf.coherence;` must copy the optional itself, not its
value.** Writing `if (tf.coherence) block.coherence = *tf.coherence;` is the
same thing today and stops being the same thing the moment somebody adds an
`else`. The gate's whole point is that absence survives every hop to the
screen.

- [ ] **Step 5: Run the tests**

```bash
build-l5c/app/tests/Release/rtatool_analysis_tests.exe "[analyser][transfer]"
```

All six new cases pass. Paste the output.

- [ ] **Step 6: Confirm the coherence guard still bites**

```bash
ctest --test-dir build-l5c -C Release -R "coherence_gate|framework_deps" -V
```

`coherence_gate_is_not_bypassed` must still pass — this task writes
`TransferBlock::coherence` in `app/`, and if that guard's scan reaches into
`app/` the new assignment could trip it. If it does trip, **do not widen the
guard**: report it and stop. Paste the output either way, including the
`measure_has_no_framework_deps` scanned count, which must not have dropped.

- [ ] **Step 7: Commit**

```bash
git add app/src/measure/Snapshot.h app/src/measure/Analyser.h app/src/measure/Analyser.cpp app/tests/test_analyser.cpp
```

Message: `feat(app): the dual-FFT reaches the snapshot, in degrees, gate intact`.

---

## Task 6 — Two rings, drained in lock-step; a synthetic source worth measuring

Two halves of one deliverable: the live path must feed the engine *aligned*,
and the hardware-free path must produce a transfer function that is not the
identity.

**The defect this closes.** `AnalysisThread::drainRole` is called twice, once
per role, and each call drains **its own ring to exhaustion**:

```cpp
    drainRole(rta::platform::ChannelRole::Measurement, false);
    drainRole(rta::platform::ChannelRole::Reference, true);
```

For a single-channel RTA that is correct and cheap. For a dual-FFT it is
poison: an audio callback landing between those two calls leaves the reference
ring one hop richer, that hop is consumed on this pass, and **every frame
afterwards pairs reference *t* with measurement *t*−hop**. At hopSize 2048 that
is 42 ms of permanent, invisible misalignment. Coherence collapses at HF first —
which `DualFftEngine`'s own comment attributes to *your own misalignment*
rather than to the system under test. The operator would read it as a broken
loudspeaker.

**Files:**
- Create: `app/src/measure/PairedDrain.h`, `app/src/measure/SyntheticImpairment.h`
- Create: `app/tests/test_paired_drain.cpp`, `app/tests/test_synthetic_impairment.cpp`
- Modify: `app/src/measure/AnalysisThread.h`, `.cpp`
- Modify: `app/src/measure/SyntheticInput.h`, `.cpp`
- Modify: `app/tests/CMakeLists.txt` (two test files, two guard paths)

**Interfaces:**
- Consumes: `Analyser::pushPair` (task 5),
  `rta::platform::CaptureBus::ring/config`, `rta::dsp::RingBuffer::availableToRead`.
- Produces: `pairedHopCount()`, `DelayLine`, `addNoise()`,
  `SyntheticInput::Config::{measurementDelaySamples, measurementNoiseDb}`.

- [ ] **Step 1: Write the failing tests**

`app/tests/test_paired_drain.cpp`:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/tests.
//
// SCOPE OF THIS FILE, stated so nobody claims more from it than it proves:
// it tests the ARITHMETIC of how far two rings may advance together. It does
// NOT test AnalysisThread's ring plumbing, which needs a live CaptureBus and a
// running thread and has no test target in this tree. The plumbing is reviewed
// by reading, and that is what the report must say.
#include "measure/PairedDrain.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("two rings advance only as far as the shorter one allows",
          "[paired-drain]") {
    // The whole point: a callback landing between two independent drains
    // leaves one ring richer, and consuming that surplus offsets the two
    // channels permanently. Taking the minimum is what refuses the surplus.
    CHECK(rta::measure::pairedHopCount(4096, 2048, 1024) == 2u);
    CHECK(rta::measure::pairedHopCount(2048, 4096, 1024) == 2u);
    CHECK(rta::measure::pairedHopCount(4096, 4096, 1024) == 4u);
}

TEST_CASE("a partial hop is left in both rings", "[paired-drain]") {
    // Whole hops only, so pushPair always sees a frame-aligned chunk -- the
    // same rule drainRole already follows for the single-channel path. A short
    // remainder stays for next time.
    CHECK(rta::measure::pairedHopCount(1500, 1500, 1024) == 1u);
    CHECK(rta::measure::pairedHopCount(1023, 5000, 1024) == 0u);
}

TEST_CASE("a degenerate hop size consumes nothing", "[paired-drain]") {
    // Divide-by-zero on the analysis thread is a hang or a crash at a show.
    CHECK(rta::measure::pairedHopCount(4096, 4096, 0) == 0u);
    CHECK(rta::measure::pairedHopCount(0, 0, 1024) == 0u);
}
```

`app/tests/test_synthetic_impairment.cpp`:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/tests. The impairments that make the hardware-free
// path produce a transfer function worth looking at.
#include "measure/SyntheticImpairment.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

TEST_CASE("the delay line shifts by exactly the requested samples",
          "[synthetic-impairment]") {
    rta::measure::DelayLine line(3);
    const std::vector<float> in{ 1, 2, 3, 4, 5, 6, 7, 8 };
    std::vector<float> out(in.size());
    line.process(in, out);

    const std::vector<float> expected{ 0, 0, 0, 1, 2, 3, 4, 5 };
    for (std::size_t i = 0; i < expected.size(); ++i) {
        CHECK(out[i] == Catch::Approx(expected[i]));
    }
}

TEST_CASE("the delay line carries state across block boundaries",
          "[synthetic-impairment]") {
    // The failure this catches is a delay implemented per-block: it looks
    // perfect in a one-block test and re-inserts three zeros every buffer in
    // the real app, which is a click train, not a delay.
    rta::measure::DelayLine line(3);
    const std::vector<float> first{ 1, 2, 3, 4 };
    const std::vector<float> second{ 5, 6, 7, 8 };
    std::vector<float> out(4);

    line.process(first, out);
    CHECK(out[3] == Catch::Approx(1.0f));

    line.process(second, out);
    const std::vector<float> expected{ 2, 3, 4, 5 };
    for (std::size_t i = 0; i < expected.size(); ++i) {
        CHECK(out[i] == Catch::Approx(expected[i]));
    }
}

TEST_CASE("a zero delay is a copy", "[synthetic-impairment]") {
    rta::measure::DelayLine line(0);
    const std::vector<float> in{ 1, 2, 3 };
    std::vector<float> out(3);
    line.process(in, out);
    for (std::size_t i = 0; i < in.size(); ++i) CHECK(out[i] == Catch::Approx(in[i]));
}

TEST_CASE("added noise lands within a few percent of the requested RMS",
          "[synthetic-impairment]") {
    // Closed form: the generator is uniform on [-1, 1), whose RMS is
    // 1/sqrt(3), so the scale factor is rms*sqrt(3). Over 200k samples the
    // sample RMS of a uniform generator is within well under 1% of the
    // population value, so a 3% margin is loose enough never to flake and
    // tight enough to fail a missing sqrt(3).
    std::vector<float> block(200000, 0.0f);
    std::uint32_t state = 0x1234u;
    rta::measure::addNoise(block, 0.05f, state);

    double sumSq = 0.0;
    for (const float v : block) sumSq += static_cast<double>(v) * v;
    const double rms = std::sqrt(sumSq / static_cast<double>(block.size()));
    CHECK(rms == Catch::Approx(0.05).epsilon(0.03));
}

TEST_CASE("noise is added to, never substituted for, the signal",
          "[synthetic-impairment]") {
    std::vector<float> block(1000, 1.0f);
    std::uint32_t state = 0x1234u;
    rta::measure::addNoise(block, 0.01f, state);

    double mean = 0.0;
    for (const float v : block) mean += v;
    mean /= static_cast<double>(block.size());
    CHECK(mean == Catch::Approx(1.0).margin(0.01));
}

TEST_CASE("the same seed gives the same noise", "[synthetic-impairment]") {
    // Determinism is what makes transfer.png reviewable as a byte-for-byte
    // diff, the same property makeSyntheticSnapshot already buys for
    // rta-view.png.
    std::vector<float> a(64, 0.0f), b(64, 0.0f);
    std::uint32_t sa = 0x99u, sb = 0x99u;
    rta::measure::addNoise(a, 0.1f, sa);
    rta::measure::addNoise(b, 0.1f, sb);
    for (std::size_t i = 0; i < a.size(); ++i) CHECK(a[i] == Catch::Approx(b[i]));
}
```

- [ ] **Step 2: Run and watch both fail**

- [ ] **Step 3: Write `PairedDrain.h`**

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE: enforced by the
// measure_has_no_framework_deps ctest.
#pragma once

#include <algorithm>
#include <cstddef>

namespace rta::measure {

/// How many whole hops two rings can advance TOGETHER.
///
/// Extracted from AnalysisThread purely so this arithmetic is testable without
/// a CaptureBus and a running thread -- and because getting it wrong is
/// invisible. Draining each role's ring to exhaustion independently (which is
/// correct for the single-channel RTA path, and is what drainRole still does)
/// lets an audio callback landing between the two calls leave one ring a hop
/// richer. Consume that surplus and every frame afterwards pairs reference t
/// with measurement t-hop: at hopSize 2048, 42 ms of permanent misalignment
/// that decorrelates the channels at HIGH frequency first, because HF has the
/// shortest period. The operator reads that as a broken loudspeaker.
///
/// Sampling both availabilities once and consuming the minimum is safe against
/// the writer: an audio callback only ever ADDS, so availability can grow
/// between this call and the drain but never shrink.
[[nodiscard]] inline std::size_t pairedHopCount(std::size_t referenceAvailable,
                                                std::size_t measurementAvailable,
                                                std::size_t hop) noexcept {
    if (hop == 0) return 0;
    return std::min(referenceAvailable, measurementAvailable) / hop;
}

}  // namespace rta::measure
```

- [ ] **Step 4: Write `SyntheticImpairment.h`**

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE: enforced by the
// measure_has_no_framework_deps ctest.
//
// Why this exists: SyntheticInput writes the SAME block into both channels, so
// the transfer function of the hardware-free path is identically H = 1 -- flat
// 0 dB, zero phase, coherence 1.0 at every bin. That is the single most
// misleading picture this application could put on screen, because it is
// exactly what a perfectly working measurement of nothing looks like, and it
// is also what several broken engines look like. A known delay and a known
// noise floor give the display something with a checkable answer.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace rta::measure {

/// Integer-sample delay, carrying state across blocks.
///
/// Block-local delays are the classic mistake here: they pass a one-block test
/// and re-insert the leading zeros on every buffer in the running app, which
/// is a click train at the block rate, not a delay.
class DelayLine {
public:
    explicit DelayLine(int delaySamples)
        : history_(static_cast<std::size_t>(std::max(0, delaySamples)), 0.0f) {}

    /// `out` and `in` must be the same length; `out` may not alias `in`.
    void process(std::span<const float> in, std::span<float> out) noexcept {
        const std::size_t d = history_.size();
        for (std::size_t i = 0; i < in.size() && i < out.size(); ++i) {
            if (d == 0) {
                out[i] = in[i];
                continue;
            }
            out[i] = history_[cursor_];
            history_[cursor_] = in[i];
            cursor_ = (cursor_ + 1) % d;
        }
    }

    void reset() noexcept {
        std::fill(history_.begin(), history_.end(), 0.0f);
        cursor_ = 0;
    }

private:
    std::vector<float> history_;
    std::size_t cursor_ = 0;
};

/// Add independent noise of the given RMS, in place.
///
/// xorshift32 rather than <random>: it is three lines, it is bit-identical on
/// every platform and standard-library version, and `transfer.png` has to be
/// reviewable as a byte-for-byte diff the way `rta-view.png` already is.
/// std::uniform_real_distribution makes no such guarantee across
/// implementations.
///
/// A uniform generator on [-1, 1) has RMS 1/sqrt(3), so the scale is
/// rms*sqrt(3) -- the factor a missing conversion would silently drop, leaving
/// the noise floor 4.8 dB lower than asked for.
inline void addNoise(std::span<float> block, float rms, std::uint32_t& state) noexcept {
    const float scale = rms * 1.7320508f;  // sqrt(3)
    for (float& v : block) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        const float uniform = static_cast<float>(state) / 2147483648.0f - 1.0f;
        v += uniform * scale;
    }
}

}  // namespace rta::measure
```

- [ ] **Step 5: Make `AnalysisThread` drain in lock-step**

In `AnalysisThread.h`, add `#include "measure/PairedDrain.h"`, a second scratch
buffer `std::vector<float> referenceScratch_;` (sized with `hopScratch_` in the
constructor), and declare `void drain();` and
`void drainPaired(int referenceChannel, int measurementChannel);`.

In `AnalysisThread.cpp`, replace the two `drainRole` calls in `runBody` with a
single `drain()`:

```cpp
void AnalysisThread::drain() {
    const int reference = bus_.config().firstChannelWithRole(rta::platform::ChannelRole::Reference);
    const int measurement =
        bus_.config().firstChannelWithRole(rta::platform::ChannelRole::Measurement);

    // Both roles present means a transfer function is being measured, and the
    // two streams must advance together -- see PairedDrain.h for what
    // independent drains cost. With only one role assigned there is no pairing
    // to preserve and the original path is both correct and cheaper.
    if (reference >= 0 && measurement >= 0) {
        drainPaired(reference, measurement);
        return;
    }
    drainRole(rta::platform::ChannelRole::Measurement, false);
    drainRole(rta::platform::ChannelRole::Reference, true);
}

void AnalysisThread::drainPaired(int referenceChannel, int measurementChannel) {
    auto* referenceRing = bus_.ring(referenceChannel);
    auto* measurementRing = bus_.ring(measurementChannel);
    if (referenceRing == nullptr || measurementRing == nullptr) return;

    const std::size_t hop = hopScratch_.size();
    const std::size_t hops = pairedHopCount(referenceRing->availableToRead(),
                                            measurementRing->availableToRead(), hop);

    for (std::size_t i = 0; i < hops; ++i) {
        // peek both BEFORE discarding either: a short read on the second ring
        // after the first has already been discarded would drop a hop from one
        // channel only -- creating the very misalignment this function exists
        // to prevent, and doing it under the code that prevents it.
        if (referenceRing->peek(referenceScratch_) != hop) break;
        if (measurementRing->peek(hopScratch_) != hop) break;
        referenceRing->discard(hop);
        measurementRing->discard(hop);
        analyser_->pushPair(referenceScratch_, hopScratch_);
    }
}
```

`drainRole` stays exactly as it is, still used by the single-role path.

- [ ] **Step 6: Give `SyntheticInput` its impairments**

In `SyntheticInput.h`, add to `Config`:

```cpp
        /// Applied to the MEASUREMENT channel only, so the two channels stop
        /// being identical and the transfer function stops being H = 1. The
        /// display then has an answer that can be checked by eye against the
        /// readout: phi(f) = -360*f*D/fs.
        int measurementDelaySamples = 0;

        /// Independent noise on the measurement channel, dB relative to
        /// `amplitude`. Drives coherence below 1 in a way an operator can
        /// reason about -- gamma^2 = S/(S+N) per bin -- which is what makes the
        /// coherence ribbon and the trace fade visible at all without a room,
        /// a microphone, and somebody talking.
        double measurementNoiseDb = -120.0;
```

In `SyntheticInput.cpp`: add a second block buffer and a `DelayLine`, and write
the clean block to whichever channel `bus_.config()` gives the **Reference**
role and the impaired block to the **Measurement** role. Roles, not channel
indices: a fixed "channel 0 is clean" convention would produce a *negative*
measured delay the moment somebody swaps the roles in the channel table, and
nothing on screen would explain why.

- [ ] **Step 7: Run everything and prove the drain test can fail**

```bash
build-l5c/app/tests/Release/rtatool_analysis_tests.exe "[paired-drain],[synthetic-impairment]"
```

```bash
ctest --test-dir build-l5c -C Release -R "framework_deps" -V
```

Then break `pairedHopCount` to use `std::max`, re-run, watch the first case go
red, restore. Paste both, plus the guard's scanned count.

- [ ] **Step 8: Commit**

```bash
git add app/src/measure/PairedDrain.h app/src/measure/SyntheticImpairment.h app/src/measure/AnalysisThread.h app/src/measure/AnalysisThread.cpp app/src/measure/SyntheticInput.h app/src/measure/SyntheticInput.cpp app/tests/test_paired_drain.cpp app/tests/test_synthetic_impairment.cpp app/tests/CMakeLists.txt
```

Message: `feat(app): two rings that advance together, and a synthetic H worth
drawing`.

## Task 7 — Stroking a field, with per-column trust

Record decision 3's drawing half. `StoredTraceLayer` currently rasterises
magnitude only, at full opacity. It has to draw phase too, and every trace —
live and stored, magnitude and phase — has to fade with its own coherence.

**Files:**
- Create: `app/src/view/TraceStroke.h`, `app/src/view/TraceStroke.cpp`
- Create: `app/tests_juce/test_trace_stroke.cpp`
- Modify: `app/src/view/StoredTraceLayer.h`, `.cpp`
- Modify: `app/tests_juce/CMakeLists.txt`

**Interfaces:**
- Consumes: `ColumnExtent` (`view/TraceDecimator.h`), `DrawnPhaseColumn`
  (task 3), `columnAlpha` (task 4), `PlotGeometry`.
- Produces: `strokeMagnitudeExtents()`, `strokePhaseColumns()`, and
  `StoredTraceLayer(rta::trace::Field)`. Used by task 8.

**Why a separate file.** `StoredTraceLayer.cpp` is 196 lines and adding a
second field's stroking plus alpha would take it past the 300-line aim. The
seam that made it long is "cache management" versus "put ink on a column", so
that is where it splits — and the stroking half becomes directly testable
against a `juce::Image` without a library, a revision counter, or a cache key.

- [ ] **Step 1: Write the failing pixel tests**

`app/tests_juce/test_trace_stroke.cpp` — render into a `juce::Image`, read back
with `juce::Image::BitmapData`, exactly as `test_stored_trace_layer.cpp`
already does. Cases:

1. **`a low-trust column is dimmer than a high-trust one, and still visible`** —
   two columns, alpha `alphaForCoherence(1.0f)` and `alphaForCoherence(0.0f)`,
   same colour, same extent. Assert the low-trust column's drawn pixel differs
   from the background AND is closer to the background than the high-trust one.
   This is the record's §8 "assert the drawn alpha differs between high- and
   low-γ² columns" test, and the "still visible" half is what pins the floor.
2. **`an empty alpha span draws opaque`** — a single-channel RTA capture has no
   coherence, and dimming it would assert a measurement nobody took. Assert the
   pixel matches the high-trust case exactly.
3. **`a straddling phase column paints both edges and not the middle`** — a
   `DrawnPhaseColumn` with `straddlesWrap`, `minDeg = 179`, `maxDeg = -179`.
   Assert painted pixels within two rows of the pane top and bottom, and an
   unpainted row at the vertical centre. **This is the test that catches the
   whole 358°-band defect at the drawing layer**, one level below where task 3
   catches it.
4. **`a full-band column paints the whole pane height`** — `fullBand` true;
   assert top row, centre row and bottom row are all painted.
5. **`a column with no data paints nothing`** — assert the whole column matches
   the background.

- [ ] **Step 2: Run and watch them fail**

- [ ] **Step 3: Write `TraceStroke.h` / `.cpp`**

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. Uses JUCE (juce::Graphics), so it is NOT in
// the measure_has_no_framework_deps file list. Decision 3 of
// docs/dsp/2026-08-29-display-layer-l5c.md, drawing half.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "view/PhaseDecimator.h"
#include "view/PlotGeometry.h"
#include "view/TraceDecimator.h"

#include <span>

namespace rta::view {

/// One filled 1 px column per extent.
///
/// `columnAlpha` may be EMPTY, which means "this trace carries no coherence"
/// and draws fully opaque -- a different thing from "coherence is zero". A
/// single-channel capture has no trust information to show, and dimming it
/// would assert a measurement that was never taken. When non-empty it must be
/// the same length as `extents`; a shorter span draws the remainder opaque
/// rather than reading past its end.
void strokeMagnitudeExtents(juce::Graphics& g, std::span<const ColumnExtent> extents,
                            const PlotGeometry& geometry, int originY,
                            std::span<const float> columnAlpha, juce::Colour base);

/// Same contract, for wrapped phase.
///
/// Three shapes, one per DrawnPhaseColumn state: a plain extent, a straddle
/// (two pieces, at the top and bottom edges of the pane, with NOTHING between
/// them -- a line across the middle is the wrap artefact, not the curve), and
/// a full-height band where phase rotates faster than the column can resolve.
void strokePhaseColumns(juce::Graphics& g, std::span<const DrawnPhaseColumn> columns,
                        const PlotGeometry& geometry, int originY,
                        std::span<const float> columnAlpha, juce::Colour base);

}  // namespace rta::view
```

`TraceStroke.cpp` moves the existing `strokeExtents` body out of
`StoredTraceLayer.cpp` unchanged — **including its two comments about
`yForDb`'s clamping and the one-pixel minimum height, which are hard-won and
must not be lost in the move** — and adds the alpha lookup and the phase
variant. Alpha is applied as `base.withMultipliedAlpha(a)`, per column, before
each `fillRect`.

- [ ] **Step 4: Teach `StoredTraceLayer` about fields**

`StoredTraceLayer.h`: include `trace/Trace.h`, add

```cpp
    /// Which field this instance rasterises. Fixed at construction, NOT part of
    /// the cache key: one instance draws one field for its whole life, so a key
    /// term for it could never differ between two calls. The Bode composite
    /// owns two instances, one per pane.
    explicit StoredTraceLayer(rta::trace::Field field = rta::trace::Field::Magnitude);
```

and a `const rta::trace::Field field_;` member. `RtaView` keeps the default and
is not otherwise touched by this task.

In `rebuild`, dispatch on `field_`: magnitude keeps `decimateToColumns` +
`bridgeGaps` + `strokeMagnitudeExtents`; phase uses `decimatePhaseToColumns` +
`wrapForDrawing` + `strokePhaseColumns`. **`bridgeGaps` is not applied to
phase** — it interpolates between two extents, and interpolating across a wrap
invents a sweep through the whole pane that nothing measured. Write that
reasoning into the code as a comment; it is the kind of thing a later reader
"fixes".

Both paths compute `columnAlpha` from the trace's own coherence field when it
has one, and pass an empty span when it does not.

- [ ] **Step 5: Run, then prove test 3 can fail**

```bash
ctest --test-dir build-l5c -C Release -R "^view/" --output-on-failure
```

Then make `strokePhaseColumns` ignore `straddlesWrap` and draw min→max as one
rect. The straddle case must go red (it would paint the middle). Restore, paste
both.

- [ ] **Step 6: Check the file lengths**

```bash
wc -l app/src/view/StoredTraceLayer.cpp app/src/view/TraceStroke.cpp
```

Both under 300. If `StoredTraceLayer.cpp` is still over, the next seam is the
per-entry loop; report it rather than leaving a 350-line file.

- [ ] **Step 7: Commit** — `feat(app): ink that dims with trust, and lifts at the wrap`.

---

## Task 8 — The Bode composite

Record decisions 1–5, assembled. Everything it needs now exists.

**Files:**
- Create: `app/src/view/TransferView.h`, `.cpp`
- Create: `app/tests_juce/test_transfer_view.cpp`
- Modify: `app/src/measure/SyntheticSnapshot.h`, `.cpp` (`makeSyntheticTransfer`)
- Modify: `app/tests_juce/CMakeLists.txt`, `app/CMakeLists.txt`
- Modify: `tools/snapshot.cpp` (`transfer.png`)

**Interfaces:**
- Consumes: everything from tasks 2, 3, 4, 5, 7; `rta::view::drawGrid`,
  `drawFrequencyLabels`, `drawLevelLabels` (`view/PlotAxes.h`);
  `rta::measure::unwrapPhase` (`measure/PhaseUnwrap.h`).
- Produces: `TransferView`, `makeSyntheticTransfer()`. Used by task 10.

- [ ] **Step 1: Write `makeSyntheticTransfer` and its test first**

In `measure/SyntheticSnapshot.h`, alongside the existing synthetic snapshot
builder:

```cpp
/// A deterministic transfer function: a pure delay, a notch, and a coherence
/// dip. Built from the same closed forms the preview mockup uses so the
/// picture stays recognisable, but as PER-BIN ARRAYS rather than a function
/// sampled once per pixel -- which is the fourth mockup accident the L5c
/// record names, and the one that composes with nothing.
///
/// Deterministic for the same reason makeSyntheticSnapshot is: `transfer.png`
/// has to be reviewable as a byte-for-byte diff.
[[nodiscard]] TransferBlock makeSyntheticTransfer(std::size_t fftSize, double sampleRate,
                                                  int delaySamples);
```

Tests in `app/tests/test_synthetic_snapshot.cpp`: array lengths equal
`fftSize/2 + 1`; the phase at a few bins matches `-360·f·D/fs` wrapped (closed
form, same identity as task 5); the coherence dip is present and inside
`[0, 1]`; two calls with the same arguments compare equal element-for-element.

- [ ] **Step 2: Write the failing view test**

`app/tests_juce/test_transfer_view.cpp`:

1. **`the phase pane keeps 3/8 of the shared area at every window size`** —
   construct the view, `setSize(1100, 760)`, read `panes()` back; repeat at
   `(1100, 1000)` and `(900, 420)`. Assert the 5:3 cross-multiplication and
   that the ribbon stays 34 px. **Geometry read back from the component, not
   from pixels** — the record says so, and a pixel test here would pass for a
   view that drew the right shape in the wrong place.
2. **`renderTo draws without resized() ever having run`** — render into a
   `juce::Image` on a view that was constructed and never given a size or a
   desktop peer, and assert the image is not blank. Project CLAUDE.md records
   this as one of the two reasons an offscreen snapshot comes out empty:
   `setSize()` does not call `resized()` on a component with no peer. The
   snapshot tool depends on `renderTo` computing its own layout, and a view
   that only lays out in `resized()` produces a blank `transfer.png` while
   every other test passes.
3. **`a snapshot with no transfer draws an empty state, not an empty Bode`** —
   render a `StaticSnapshotSource` holding an RTA-only snapshot; assert the
   image is not blank (the empty-state text is drawn) and that no ink appears
   inside the magnitude pane's plot rectangle. A Bode plot of nothing looks
   like a measurement.
4. **`low coherence draws dimmer than high coherence`** — the record's §8
   image-level check, one level above task 7's: build a synthetic transfer
   whose coherence is high at 1 kHz and low at 100 Hz, render, and compare the
   painted pixels in the two columns.
5. **`unwrapping changes the axis, not the stored data`** — call
   `setPhaseUnwrapped(true)`; assert the phase pane's dB range grows to whole
   multiples of 360 and that a second `renderTo` after `setPhaseUnwrapped(false)`
   reproduces the first image exactly.

- [ ] **Step 3: Write the header**

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. Decisions 1-5 of
// docs/dsp/2026-08-29-display-layer-l5c.md.
#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "measure/SnapshotSource.h"
#include "view/BodeLayout.h"
#include "view/RepaintGate.h"
#include "view/StoredTraceLayer.h"

namespace rta::trace { class TraceLibrary; }

namespace rta::view {

/// The stacked Bode composite: coherence ribbon, magnitude pane, phase pane,
/// all three on the ONE shared log-frequency mapping (decision 1).
///
/// Stacking rather than REW's dual-axis single graph, because L5a already
/// committed to dozens of overlaid traces: dual-axis Bode is readable with one
/// or two, and with dozens every wrapped phase trace crosses every magnitude
/// trace several times per decade with nothing but memory to say which axis
/// each belongs to.
class TransferView final : public juce::Component, private juce::Timer {
public:
    explicit TransferView(const rta::measure::SnapshotSource& source);
    ~TransferView() override;

    void setSource(const rta::measure::SnapshotSource& source);

    /// Nullable, null by default -- same contract RtaView::setLibrary states.
    void setLibrary(const rta::trace::TraceLibrary* library);

    /// Wrapped +-180 is the default (decision 4): unwrap is ambiguous wherever
    /// coherence is low, and one bad bin steps every bin above it by 360 -- the
    /// wrapped view degrades locally, the unwrapped view degrades globally.
    /// When unwrapped, the axis extends in whole multiples of 360 to fit.
    ///
    /// Deliberately NOT REW's cursor re-referencing, however readable that is:
    /// it makes the drawn trace a function of the mouse position, which breaks
    /// the cached-layer architecture (stored traces rasterise once per
    /// revision, not per mouse move) and makes two screenshots of one
    /// measurement disagree.
    void setPhaseUnwrapped(bool unwrapped);
    [[nodiscard]] bool isPhaseUnwrapped() const noexcept { return unwrapped_; }

    void paint(juce::Graphics&) override;
    void resized() override;

    /// Renders into `area` with no message loop pumped and no timer fired --
    /// the same contract RtaView::renderTo provides, and for the same reason:
    /// tools/snapshot.cpp drives it offline.
    void renderTo(juce::Graphics& g, juce::Rectangle<int> area) const;

    /// Production API existing so the layout contract is TESTABLE rather than
    /// asserted -- the same trade StoredTraceLayer::rebuildCount documents.
    /// The 5:3 rule is entirely a claim about these rectangles, and reading it
    /// back from pixels would pass for a view that drew the right shape in the
    /// wrong place. `app/tests_juce/test_transfer_view.cpp` is its caller;
    /// deleting it deletes the test.
    [[nodiscard]] const BodePanes& panes() const noexcept { return panes_; }

private:
    void timerCallback() override;

    const rta::measure::SnapshotSource* source_;
    const rta::trace::TraceLibrary* library_ = nullptr;
    GateState gate_;
    bool unwrapped_ = false;
    BodePanes panes_;

    /// One cached layer per pane. The O(1)-in-trace-count property L5a bought
    /// has to survive multiplication by panes, which is what
    /// test_transfer_view asserts against `rebuildCount()`.
    mutable StoredTraceLayer storedMagnitude_{ rta::trace::Field::Magnitude };
    mutable StoredTraceLayer storedPhase_{ rta::trace::Field::Phase };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransferView)
};

}  // namespace rta::view
```

- [ ] **Step 4: Write the implementation**

`renderTo` is the whole body; `paint` is a one-line call to it, as `RtaView`
does. Structure:

1. `panes_ = bodePanes(PaneRect{...from area...}, az::ui::gap)` in `resized()`;
   `renderTo` recomputes locally so it works with no `resized()` having run.
2. `const auto axis = frequencyAxis(content);` — **once**, then three
   `paneGeometry` calls. Never a second axis computation anywhere in this file.
3. Ribbon: one 1 px filled column per pixel, alpha from
   `alphaForCoherence(columnAlpha(...))` of the **live** capture only, in
   `rta::view::trace` (the amber accent) so the strip visually belongs to the
   trace whose trust it reports. With dozens of traces, a ribbon showing "the"
   coherence would be showing an undefined quantity. The existing
   `TransferFunctionPreview::paintRibbon` is the drawing reference — read it;
   the loop and the border are right, only its data source is a mockup.
4. Magnitude pane: `drawGrid`, `drawLevelLabels`, `storedMagnitude_.draw(...)`,
   then the live trace stroked over it via `strokeMagnitudeExtents`.
5. Phase pane: `drawGrid`, `drawFrequencyLabels`, whole-degree labels every 90°
   (copy `drawPhaseLabels` from the mockup — it already implements the
   degrees-are-not-dB rule correctly), `storedPhase_.draw(...)`, then the live
   phase via `wrapForDrawing` + `strokePhaseColumns`.
6. When `unwrapped_`, run `rta::measure::unwrapPhase` over the live phase with
   the snapshot's coherence, take the min/max of the result, and set the pane's
   `dbTop`/`dbBottom` to the enclosing whole multiples of 360.
7. When `snapshot->transfer` is absent: draw the grid and one line of
   `emptyStateText` reading `NO REFERENCE CHANNEL`, and return. Do not draw a
   flat trace.

Keep the file under 300 lines. If it crosses, split the ribbon into
`view/TransferRibbon.{h,cpp}` — that is the seam.

- [ ] **Step 5: Add `transfer.png` to the snapshot tool**

In `tools/snapshot.cpp`, mirroring the existing `rta-view.png` block: build a
`StaticSnapshotSource` over a snapshot carrying `makeSyntheticTransfer`, a
`TransferView` over it, and `renderComponent(component, outDir,
"transfer.png", width, height)`.

- [ ] **Step 6: Run everything, then look at the picture**

```bash
ctest --test-dir build-l5c -C Release -R "^view/|synthetic" --output-on-failure
```

Then render and **look**:

```bash
cmake --build build-l5c --config Release --target rtatool_snapshot --parallel
```

```bash
build-l5c/app/rtatool_snapshot_artefacts/Release/rtatool_snapshot.exe shots 1100 760
```

(from Git Bash, invoke through `cmd //c` — direct invocation returns 127.)
Read `shots/transfer.png`. The eyes-only questions the record names, which **no
test output may be pasted as proof of**: is a 0.25-alpha faded trace
*distinguishable but clearly subordinate* against graphite; does 5:3 read right
at six feet; does a full-height phase band read as "unresolvable" rather than
as "broken". Report what you see as an observation, not as a pass.

- [ ] **Step 7: Commit** — `feat(app): a Bode plot that fades where it does not know`.

---

## Task 9 — A workspace that survives a reload

Record decision 6, the model and the format. No components yet.

**Files:**
- Create: `app/src/trace/Workspace.h`, `app/src/view/PaneRegistry.h`
- Create: `app/tests/test_workspace.cpp`
- Modify: `app/src/trace/SessionCodec.h`, `.cpp` (the `[pane]` section, and
  `kSchemaVersion` → 2)
- Modify: `app/src/trace/SessionStore.cpp` (stamp the schema version on write)
- Modify: `app/tests/test_session_codec.cpp`, `app/tests/test_session_store.cpp`,
  `app/tests/CMakeLists.txt`

**Interfaces:**
- Produces: `PaneSpec`, `kMaxPanes`, `normalisePanes()`,
  `SessionDocument::panes`, `PaneView`, `resolvePaneView()`. Used by task 10.

- [ ] **Step 1: Write the failing tests**

`app/tests/test_workspace.cpp`:

1. **`weights are normalised on read`** — `{2, 2}` and `{0.5, 0.5}` both
   normalise to two equal shares summing to 1.
2. **`more than three panes are clamped`** — record decision 6 caps at 3,
   matching OSM's, because below roughly a third of a 760 px window a dB pane
   stops resolving what the readout rules promise. Five specs in, three out.
3. **`an all-zero weight set becomes equal shares`** — nobody expressed a
   preference; privileging pane 0 for no stated reason would be inventing one.
4. **`an empty workspace is one rta pane`** — a session saved before workspaces
   existed, and a brand-new session, are the same case and must both open.

In `app/tests/test_session_codec.cpp`:

5. **`a workspace round-trips through the index`** — encode two panes, decode,
   compare count, view strings and weights.
6. **`an unknown view name does not refuse the session`** — decode an index
   whose pane says `view=spectrograph`. Assert `DecodeStatus::Ok` and that the
   string arrives verbatim. Then assert `resolvePaneView("spectrograph")`
   returns `PaneView::Rta` with `fellBack == true` and the requested name
   preserved for the report.
   **This is the deliberate asymmetry with the refuse-newer-schema rule**, and
   the test comment must say why: a guessed *measurement* is a plausible lie, a
   guessed *layout* is a wrong arrangement of true data, and refusing a whole
   session of real captures over a layout word would destroy value to protect
   nothing.
7. **`a malformed weight still refuses the session`** — `weight=abc` is
   `Malformed`. The tolerance in case 6 is for an unknown *view name*, not a
   licence to guess at numbers; without this test somebody will "simplify" the
   codec into accepting anything in a `[pane]` block.
8. **`a version-1 session still opens`** — decode an index whose first line is
   `schema=1` and which carries captures and entries but no panes. Assert `Ok`,
   and that `normalisePanes` on the empty pane list yields the single default
   `rta` pane. This is the assertion that makes the bump safe, and it must
   exist before the bump lands, not after.

In `app/tests/test_session_store.cpp`:

9. **`a loaded v1 session is written back as v2`** — read a document whose
   `schemaVersion` is 1, add a pane, `writeIndex`, and read the raw first line.
   Assert it says `schema=2`. Without the stamp this writes a v1 file carrying
   v2 content — the one file this change must never produce, because an older
   build would then report it as `Malformed` ("your session is corrupt")
   instead of `NewerSchema` ("this needs a newer version"), which is the whole
   reason the version was bumped.

- [ ] **Step 2: Run and watch them fail**

- [ ] **Step 3: Write `Workspace.h`**

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/trace. No JUCE: enforced by the
// measure_has_no_framework_deps ctest. Decision 6 of
// docs/dsp/2026-08-29-display-layer-l5c.md.
#pragma once

#include <string>
#include <vector>

namespace rta::trace {

/// Vertical-only, 1..3 panes, one workspace per session.
///
/// The cap is a judgement, matching Open Sound Meter's: below roughly a third
/// of a 760 px window a dB pane stops resolving the 0.1 dB the readout rules
/// promise. Friture's uncapped docking grid needs a dock framework az_ui does
/// not have and buys freedom this use case -- one screen, read from six feet,
/// seconds at a time -- never asks for.
inline constexpr int kMaxPanes = 3;

inline constexpr const char* kDefaultPaneView = "rta";

/// `view` is a VERBATIM string, never an enum, at this layer. SessionCodec must
/// not validate it: mapping a name to a pane type -- and falling back when the
/// name is unknown -- belongs to view/PaneRegistry.h, so the codec keeps its
/// "refuse what you do not understand" rule for measurements while the view
/// keeps its "never refuse a session over a layout word" rule for layouts.
struct PaneSpec {
    std::string view = kDefaultPaneView;
    float weight = 1.0f;
};

/// Clamp to `kMaxPanes`, drop non-positive weights to equal shares, normalise
/// the rest to sum to 1. An empty input yields exactly one default pane: a
/// session saved before workspaces existed and a brand-new one are the same
/// case, and both must open.
[[nodiscard]] std::vector<PaneSpec> normalisePanes(std::vector<PaneSpec> panes);

}  // namespace rta::trace
```

`normalisePanes` is defined inline in the header (it is a dozen lines and the
file stays well under the cap), or in `SessionCodec.cpp` if the implementer
prefers — in which case add nothing new to the CMake source lists, since that
file is already compiled into both targets.

- [ ] **Step 4: Write `PaneRegistry.h`**

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. No JUCE: enforced by the
// measure_has_no_framework_deps ctest. Decision 6.
#pragma once

#include "trace/Workspace.h"

#include <string>

namespace rta::view {

/// The pane type vocabulary. Lives in app/, never in az_ui -- `rta` and
/// `transfer` are measurement vocabulary, and the module rule keeps that out
/// of the design system.
enum class PaneView { Rta, Transfer };

struct PaneResolution {
    PaneView view = PaneView::Rta;
    /// True when `requested` was not recognised. The caller REPORTS this; it
    /// does not refuse the session over it.
    bool fellBack = false;
    std::string requested;
};

[[nodiscard]] inline PaneResolution resolvePaneView(const std::string& name) {
    PaneResolution out;
    out.requested = name;
    if (name == "rta") return out;
    if (name == "transfer") { out.view = PaneView::Transfer; return out; }
    out.fellBack = true;   // falls back to Rta, and says so
    return out;
}

}  // namespace rta::view
```

- [ ] **Step 5: Add `[pane]` to the index**

`SessionDocument` gains `std::vector<PaneSpec> panes;`. `encodeIndex` writes a
`[pane]` section per pane after the entries, with `view` and `weight`.
`decodeIndex` gains a `Section::Pane` case: `view` is stored verbatim with **no
validation**, `weight` goes through `tryParse` like every other number and a
parse failure is `Malformed`. Write the asymmetry into the code as a comment —
it is the single most likely thing for a later reader to "tidy" into
consistency.

**Bump `kSchemaVersion` to 2**, and stamp it on write.

An earlier draft of this plan said not to bump, on the grounds that bumping
"would make every existing session file refuse to open". **That was wrong.**
`decodeIndex` refuses only a schema NEWER than its own constant
(`if (schemaVersion > kSchemaVersion) return NewerSchema`), so raising the
constant to 2 keeps every existing `schema=1` file readable. Nothing is lost by
bumping, and something real is gained: an older build meeting a `[pane]`
section would otherwise fall through to `Malformed` — telling the user their
session is *corrupt*, which is a lie — where a version bump makes it say
`NewerSchema`, which is true and actionable. That is the format's own stated
philosophy applied to itself.

Also: `SessionStore::writeIndex` must stamp `doc.schemaVersion = kSchemaVersion`
before encoding. Without it, a document decoded from a v1 file keeps
`schemaVersion = 1`, and saving it after adding panes writes a v1 file
containing v2 content — the one file this change must never produce. Stamp it
in `writeIndex`, not in `encodeIndex`: `test_session_codec.cpp` encodes a
deliberately-future document to test the refusal path, and stamping inside the
codec would destroy that test's premise.

- [ ] **Step 6: Register, run, prove failure**

Add `test_workspace.cpp`, and `Workspace.h` + `PaneRegistry.h` to the guard's
`GLOBS`. Run:

```bash
build-l5c/app/tests/Release/rtatool_analysis_tests.exe "[workspace]"
```

```bash
ctest --test-dir build-l5c -C Release -R "framework_deps" -V
```

Then break `resolvePaneView` to return `Malformed`-equivalent behaviour by
making `decodeIndex` reject unknown view names, and watch case 6 go red.
Restore. Paste both, plus the guard count — this is the last task that adds to
it, so read the final number here and report it.

- [ ] **Step 7: Commit** — `feat(app): a layout that survives a word it does not know`.

---

## Task 10 — Wire it in: the stored-trace path finally reaches the screen

The lane's point. `docs/HANDOFF.md` records that "the stored-trace path is built
but unreached — no place attaches `TraceLibrary` to `RtaView`". This task
attaches it, and puts both pane types into a real workspace.

**Files:**
- Create: `app/src/view/WorkspaceView.h`, `.cpp`
- Create: `app/tests_juce/test_workspace_view.cpp`
- Modify: `app/src/MainComponent.h`, `.cpp`
- Modify: `tools/snapshot.cpp` (`workspace.png`)
- Modify: `app/CMakeLists.txt`, `app/tests_juce/CMakeLists.txt`

- [ ] **Step 1: Write the failing tests**

`app/tests_juce/test_workspace_view.cpp`:

1. **`a two-pane workspace gives both children the full width and the weighted
   height`** — build from two `PaneSpec`s, `setSize(1100, 760)`, assert each
   child's bounds match `az::ui::splitVertically`'s answer for the same inputs.
2. **`an unknown view name yields a pane, not a hole`** — one spec says
   `spectrograph`; assert two children exist and the fallback one is an
   `RtaView`. A missing child would shift every child after it.
3. **`the library reaches every pane`** — set a library, assert both an
   `RtaView` and a `TransferView` child report a non-null library. This is the
   assertion that would have caught the L5a gap that survived a whole session.
4. **`advancing the live sequence does not rebuild any cached layer`** — the
   record's §8 repaint-cost check: publish new snapshots, assert every pane's
   `rebuildCount()` is unchanged; then bump the library's revision and assert
   each moves by exactly one.

- [ ] **Step 2: Write `WorkspaceView`**

Owns up to `kMaxPanes` children, built through a factory the composition root
supplies (`std::function<std::unique_ptr<juce::Component>(PaneView)>`) so the
view knows the *vocabulary* but not the *classes* — which is what keeps
`WorkspaceView` from including both pane headers and becoming the second
composition root. `resized()` calls `az::ui::splitVertically` with the specs'
weights and `az::ui::gap`. `setLibrary` forwards to every child that accepts
one.

- [ ] **Step 3: Wire `MainComponent`**

- Replace the `RtaView rtaView_;` member with `WorkspaceView workspace_;`.
- Add `rta::trace::TraceLibrary library_;` and pass it to the workspace.
- Build the factory: `PaneView::Rta` → `RtaView(analysisThread_)`,
  `PaneView::Transfer` → `TransferView(analysisThread_)`.
- Default workspace when none is loaded: one `rta` pane, so the app's opening
  screen is byte-for-byte what it is today.
- **Member declaration order stays load-bearing** (trap T-1 in that file's own
  class comment): `audioIo_` before `analysisThread_` before anything holding
  references into them. `library_` must be declared **before** `workspace_`,
  because the workspace holds a pointer to it and members die in reverse
  declaration order.

- [ ] **Step 4: Add `workspace.png` and re-render every shot**

Add a two-pane (`rta` + `transfer`) `workspace.png` to `tools/snapshot.cpp`.
Then render all of them:

```bash
build-l5c/app/rtatool_snapshot_artefacts/Release/rtatool_snapshot.exe shots 1100 760
```

**`main-live.png` will change** — `MainComponent` now hosts a workspace. That
is expected, not a regression; say so in the report and describe what moved.
**`rta-view.png` must NOT change**: `RtaView` is untouched by this lane except
for its default `StoredTraceLayer` field. Compare its hash against the handoff's
recorded `d3e698964c8ffd7bcf2148b154f96e3d`. If it moved, something changed
`RtaView`'s rendering and you need to know what before going further.

- [ ] **Step 5: Full verification pass**

```bash
cmake --build build-l5c --config Release --parallel --clean-first
```

`--clean-first` is not optional before reporting a number: trap 11 in the
handoff records incremental builds silently reusing stale `.obj` files after a
restored source file kept an older mtime.

```bash
ctest --test-dir build-l5c -C Release --output-on-failure
```

Then the framework-free configuration, which CI runs:

```bash
cmake -S . -B build-l5c-off -G "Visual Studio 18 2026" -A x64 -DRTA_BUILD_APP=OFF
```

Both counts get pasted into the report, each labelled with its configuration.
**Do not add them together** and do not put either number anywhere but the
report's one baseline block — the handoff records five files carrying a stale
count within a single day, in the hand of the person who had just written the
warning about it.

- [ ] **Step 6: Commit** — `feat(app): the library reaches the plot, in a workspace that remembers`.

---

## Order, parallelism, and what contends

Tasks 1–4 are independent of each other and of 5–6; 7 needs 3 and 4; 8 needs
2, 3, 4, 5, 7; 9 is independent of everything except its own files; 10 needs 1,
8 and 9.

A sensible two-worker split: one worker takes 1 → 9 (az_ui and the format,
which touch nothing else), the other takes 2 → 3 → 4, then they meet at 5 → 6
→ 7 → 8 → 10 in sequence. `app/tests/CMakeLists.txt` is this lane's own
contention point — every JUCE-free task edits the guard's `GLOBS` line — so
serialise those edits the way the master plan serialises `core/CMakeLists.txt`,
and `git pull` immediately before each.

**Against other lanes:** L5c touches `app/`, `ui/`, `tools/` and root
`CMakeLists.txt`. It does not touch `core/` at all, so it is genuinely parallel
with L4 (sweep/IR), whose work is in `core/`. Root `CMakeLists.txt` is a
one-line addition in task 1 — land it early and tell the other lane.

---

## What the human can try, and what they should see

Rule 12: work nobody can run is work nobody can check, and an agent reporting
its own success is not evidence.

**Look at the display with no hardware at all.** Build and render:

```bash
cmake --build build-l5c --config Release --target rtatool_snapshot --parallel
```

```bash
build-l5c/app/rtatool_snapshot_artefacts/Release/rtatool_snapshot.exe shots 1100 760
```

`shots/transfer.png` — a coherence ribbon, a magnitude pane, a phase pane, all
three lined up on the same frequency columns. `shots/workspace.png` — the band
plot above the Bode composite. `shots/rta-view.png` — unchanged, and that is
the check that this lane did not disturb what already worked.

**Run the app and watch a measured transfer function.** Launch `rtatool.exe`,
switch to SYNTHETIC, and set the channel table so one channel is Reference and
one is Measurement. With task 6's impairments the measurement channel carries a
known delay and a known noise floor, so:

- the magnitude pane sits flat near 0 dB,
- the phase pane shows a straight-ish downward slope, wrapping — at a 4-sample
  delay and 48 kHz, −30° at 1 kHz and −120° at 4 kHz,
- the coherence ribbon is bright in the midband and dims where the noise floor
  dominates,
- the traces themselves visibly dim in the same places.

If the phase pane is a flat line at 0°, the radians→degrees conversion in
task 5 is missing. If coherence collapses at HF and nowhere else, the paired
drain in task 6 is not aligned.

**The three judgements no test can make** (record §8, "genuinely needs a
person"): whether a 0.25-alpha trace reads as subordinate rather than as
broken; whether 5:3 reads right at six feet; whether a full-height phase band
reads as "unresolvable" rather than "the app crashed". The snapshot makes the
looking cheap and repeatable. The looking is still yours.

---

## Traps carried forward

1. **The guard can stop watching while still reporting OK.** It globs a
   caller-supplied path list and only FATALs when the *whole* list is empty.
   Every task that adds a JUCE-free file must read the scanned-file count back.
2. **`ctest -R` does not search Catch2 tags. A tag matches zero tests and
   reports success** — this plan shipped with that very mistake in six of its
   own verification steps, caught by the Task 2 implementer, and the commands
   below are the corrected form.

   `catch_discover_tests` registers one ctest case per `TEST_CASE`, named by
   its **sentence**, not its tag. `ctest -R "[bode-layout]"` therefore matches
   nothing and exits 0. Two things do work:

   - **A tag: run the Catch2 binary directly.** Tag filtering is a Catch2
     feature, not a ctest one.
     `build-l5c/app/tests/Release/rtatool_analysis_tests.exe "[phase-decimator]"`
     (comma-separate for OR: `"[paired-drain],[synthetic-impairment]"`).
   - **A ctest `-R` selector** for the guards, which are plain `add_test`
     entries with explicit names (`framework_deps`, `coherence_gate`), and for
     the two targets that set `TEST_PREFIX` — `^ui/` and `^view/`.

   Whichever you use, **read the case count in the output**. "0 tests passed"
   is the shape of this failure, and it looks exactly like success.
3. **Build incrementally, report from `--clean-first`.**
4. **`cmd //c` to run an exe from Git Bash**; a running app holds a lock on its
   own `.exe`, so `taskkill` before rebuilding.
5. **Use the Write tool for files, not bash heredocs** — long or
   special-character content breaks silently.
6. **A literal in this plan is a suggestion; the formula is the oracle.** The
   `[pane]` key names, the alpha curve, the 34 px ribbon — assert the
   behaviour, not this document's spelling of it.
7. **The venv is in the main checkout, not this worktree.** `.venv` is
   gitignored, so `git worktree add` did not bring it. Call
   `D:\DEV CAVE EP3\PRJ010-RTA-TOOL\.venv`'s interpreter by absolute path;
   do not build a second one. (No task here needs it — no golden vectors are
   generated by this lane.)
8. **`bridgeGaps` must never be applied to phase.** It interpolates between two
   extents; across a wrap that invents a sweep through the entire pane. Task 7
   states this in code, and it is the kind of asymmetry a later reader
   "corrects".

---

## Still waiting on the owner

Neither blocks any task above; both are collected for the batch.

1. **Pane cap and named workspaces** (taste). This plan builds the record's
   decision: cap 3, vertical only, one workspace per session. Named switchable
   layouts would be an additive index change
   (`[pane]` sections keyed by a workspace name) that does not invalidate
   anything here — but the UI would change.
2. **Spectrograph colour ramp** (taste). Not built by this lane at all; noted
   so it is not lost. A perceptual multi-hue ramp (cubehelix-family, which REW
   and Friture independently chose on perceptual grounds) or a single-hue
   SODIUM-amber intensity ramp that keeps the rack aesthetic at the cost of
   less discriminable levels.

**No purchase is required for L5c** — recorded again here so this lane is never
held waiting on the ISO 2969 / SMPTE ST 202 / IEC 60268-16 purchases that block
parts of L5b and P4b.
