# L5a — trace library and session persistence: implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: use `superpowers:subagent-driven-development`
> (recommended) or `superpowers:executing-plans` to implement this plan
> task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Store, organise and draw dozens of captured measurements without the
live plot slowing down, and persist them so a session survives a restart.

**Architecture:** A trace is one complete capture with presence-based fields —
no RTA/TF type tag. Capture metadata is immutable; library state (name, group,
visibility, shade) is editable and owns a monotonic `revision`. A session is a
folder: a line-oriented index plus one binary blob per trace. The plot repaints
on `snapshot.sequence` **or** `library.revision`, and stored traces render into
a cached image so the live repaint stays O(1) in trace count.

**Tech stack:** C++20, MSVC 14.51, Catch2, JUCE 9.0.1 (view layer only), CMake.

**Spec:** `docs/specs/2026-08-28-trace-library-and-session.md`. Read it first.
Every decision below traces to a section there.

## Global Constraints

Every task's requirements implicitly include all of these.

- Every source file starts with `// SPDX-License-Identifier: AGPL-3.0-or-later`.
- **Hard cap 400 lines per file, aim for 300.** Headers too. If a file would
  exceed it, split along the seam that made it long.
- Comments explain **why a thing is what it is**, not what the line does.
- `core/` must never include JUCE, Qt, or an audio-device API. Tasks 1-5 add
  JUCE-free headers under `app/src/` and are bound by the same discipline.
- **Every new JUCE-free header must be appended to the `GLOBS` list of the
  `measure_has_no_framework_deps` test in `app/tests/CMakeLists.txt`, and you
  must confirm the test's scanned-file COUNT increased.** That guard expands its
  list with `file(GLOB_RECURSE)`, so a misspelled path contributes nothing and
  the test still passes — the count is the only proof of coverage. See
  `memory/core-must-not-include-frameworks.md`. The count is **9** before this
  plan starts.
- Build: `cmake --build build --config Release --parallel`
- Test: `ctest --test-dir build -C Release --output-on-failure`
- Baseline before this plan: **169/169 passing, zero warnings at /W4.**
- Adding a file to a CMake source list requires re-running configure:
  `cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DRTA_BUILD_APP=ON -DRTA_JUCE_PATH="D:/DEV CAVE EP3/PROJECT005-AZ-handsfree/external/JUCE"`
- Commits: `feat(scope):` / `fix(scope):` / `test(scope):`. **Never `git add -A`
  or `git add .`** — stage explicit paths only; other sessions may share this
  repo.
- Every commit must build and every commit's tests must pass.
- Do not weaken an existing test to make a change compile or pass.

## File structure

| File | Responsibility | JUCE? |
|---|---|---|
| `app/src/trace/Trace.h` | The capture record: metadata + presence-based fields | no |
| `app/src/view/TraceDecimator.h` | Bins → one min/max extent per pixel column | no |
| `app/src/trace/SessionCodec.h` | Pure encode/decode: text index and binary blob | no |
| `app/src/trace/SessionCodec.cpp` | Its implementation | no |
| `app/src/trace/SessionStore.h` / `.cpp` | Folder layout, atomic write, filesystem | no |
| `app/src/trace/TraceLibrary.h` / `.cpp` | Library state, groups, visibility, `revision` | no |
| `app/src/view/RepaintGate.h` | The pure two-part gate decision | no |
| `app/src/view/StoredTraceLayer.h` / `.cpp` | Cached image of stored traces | **yes** |
| `app/src/view/RtaView.{h,cpp}` | Modified: use the two-part gate | **yes** |

Tests live in `app/tests/`, added to the `rtatool_analysis_tests` target, except
Task 6's which is view-side and covered through `RepaintGate`.

---

## Task 1: The trace record

**Files:**
- Create: `app/src/trace/Trace.h`
- Create: `app/tests/test_trace.cpp`
- Modify: `app/tests/CMakeLists.txt` (add the test source; add `Trace.h` to `GLOBS`)

**Interfaces:**
- Consumes: nothing.
- Produces: `rta::trace::Field`, `LevelUnit`, `CaptureMeta`, `pointCountFor(int)`,
  `class Trace` with `Trace::make`, `setPhase`, `setCoherence`, `has`, `field`,
  `meta`, `pointCount`, `binHz`. Tasks 3-6 all depend on these exact names.

- [ ] **Step 1: Write the failing test**

Create `app/tests/test_trace.cpp`:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "trace/Trace.h"

#include <catch2/catch_test_macros.hpp>

using rta::trace::CaptureMeta;
using rta::trace::Field;
using rta::trace::LevelUnit;
using rta::trace::Trace;

namespace {
CaptureMeta meta(int fftSize = 8, double sampleRate = 48000.0) {
    CaptureMeta m;
    m.id = "trace-0001";
    m.fftSize = fftSize;
    m.sampleRate = sampleRate;
    return m;
}
}  // namespace

TEST_CASE("pointCountFor is the real-FFT bin count", "[trace]") {
    CHECK(rta::trace::pointCountFor(8) == 5u);
    CHECK(rta::trace::pointCountFor(32768) == 16385u);
    CHECK(rta::trace::pointCountFor(0) == 0u);
}

TEST_CASE("a magnitude of the wrong length is refused outright", "[trace]") {
    CHECK_FALSE(Trace::make(meta(), std::vector<float>(4, 0.0f)).has_value());
    CHECK(Trace::make(meta(), std::vector<float>(5, 0.0f)).has_value());
}

TEST_CASE("absent fields report absence, not zeros", "[trace]") {
    auto t = Trace::make(meta(), std::vector<float>(5, -30.0f));
    REQUIRE(t.has_value());
    CHECK(t->has(Field::Magnitude));
    CHECK_FALSE(t->has(Field::Phase));
    CHECK_FALSE(t->has(Field::Coherence));
    // The whole point of the presence model: a consumer asking for a field a
    // single-channel capture never had gets nothing, not a plausible zero.
    CHECK(t->field(Field::Phase).empty());
    CHECK(t->field(Field::Coherence).empty());
}

TEST_CASE("a second field must match the magnitude's length", "[trace]") {
    auto t = Trace::make(meta(), std::vector<float>(5, -30.0f));
    REQUIRE(t.has_value());
    CHECK_FALSE(t->setPhase(std::vector<float>(4, 0.0f)));
    CHECK_FALSE(t->has(Field::Phase));
    CHECK(t->setPhase(std::vector<float>(5, 1.5f)));
    CHECK(t->has(Field::Phase));
    CHECK(t->field(Field::Phase).size() == 5u);
}

TEST_CASE("binHz comes from the stored rate and size, not a stored axis", "[trace]") {
    auto t = Trace::make(meta(8, 48000.0), std::vector<float>(5, 0.0f));
    REQUIRE(t.has_value());
    CHECK(t->binHz() == 6000.0);
}

TEST_CASE("the calibration unit travels with the offset", "[trace]") {
    CaptureMeta m = meta();
    m.calibrationOffsetDb = 94.0f;
    m.calibrationUnit = LevelUnit::DbSpl;
    auto t = Trace::make(m, std::vector<float>(5, 0.0f));
    REQUIRE(t.has_value());
    CHECK(t->meta().calibrationUnit == LevelUnit::DbSpl);
}
```

- [ ] **Step 2: Wire it in and watch it fail**

In `app/tests/CMakeLists.txt`, add `test_trace.cpp` to the
`rtatool_analysis_tests` source list, and append
`${CMAKE_CURRENT_SOURCE_DIR}/../src/trace/Trace.h` to the semicolon-separated
`GLOBS` list of `measure_has_no_framework_deps`.

Run:
```
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DRTA_BUILD_APP=ON -DRTA_JUCE_PATH="D:/DEV CAVE EP3/PROJECT005-AZ-handsfree/external/JUCE"
cmake --build build --config Release --parallel
```
Expected: FAIL — `Cannot open include file: 'trace/Trace.h'`.

- [ ] **Step 3: Write `app/src/trace/Trace.h`**

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/trace. No JUCE: enforced by the
// measure_has_no_framework_deps ctest. See
// docs/specs/2026-08-28-trace-library-and-session.md §1.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rta::trace {

enum class Field { Magnitude, Phase, Coherence };

/// dBFS and dBSPL differ by the calibration offset. Storing the offset without
/// the unit lets a trace be reloaded as the other one: the numbers stay
/// plausible, they are wrong by that offset, and nothing on screen says so.
enum class LevelUnit { DbFs, DbSpl };

/// What the measurement WAS. Fixed at capture and never edited -- editing it
/// would make the file describe a measurement that never happened. Everything
/// the user can change lives in the library instead (spec §2).
struct CaptureMeta {
    std::string id;
    std::int64_t capturedAtUnixMs = 0;
    std::string deviceName;
    std::string channelRoles;
    double sampleRate = 0.0;
    int fftSize = 0;
    std::string window;
    std::string averagingType;
    int averagingDepth = 0;
    /// Effective, not raw: overlapped frames are not independent (dual-FFT
    /// record §5), and a gate that trusts a raw count opens too early.
    double effectiveAverages = 0.0;
    int appliedDelaySamples = 0;
    float calibrationOffsetDb = 0.0f;
    LevelUnit calibrationUnit = LevelUnit::DbFs;
};

/// Bins in a real FFT of `fftSize`: DC through Nyquist inclusive.
[[nodiscard]] constexpr std::size_t pointCountFor(int fftSize) noexcept {
    return fftSize > 0 ? static_cast<std::size_t>(fftSize) / 2u + 1u : 0u;
}

/// One complete capture. NOT a tagged RTA/TF union: a view asks what fields
/// this trace HAS. A single-channel capture simply has no phase, so nothing
/// ever has to invent one (spec §1).
class Trace {
public:
    /// Refuses a magnitude whose length disagrees with `meta.fftSize`. A trace
    /// whose axis and data disagree is not a trace worth constructing.
    [[nodiscard]] static std::optional<Trace> make(CaptureMeta meta,
                                                   std::vector<float> magnitudeDb) {
        if (magnitudeDb.size() != pointCountFor(meta.fftSize) || magnitudeDb.empty()) {
            return std::nullopt;
        }
        return Trace(std::move(meta), std::move(magnitudeDb));
    }

    [[nodiscard]] bool setPhase(std::vector<float> phase) {
        if (phase.size() != magnitude_.size()) return false;
        phase_ = std::move(phase);
        return true;
    }

    [[nodiscard]] bool setCoherence(std::vector<float> coherence) {
        if (coherence.size() != magnitude_.size()) return false;
        coherence_ = std::move(coherence);
        return true;
    }

    [[nodiscard]] bool has(Field f) const noexcept { return !field(f).empty(); }

    [[nodiscard]] std::span<const float> field(Field f) const noexcept {
        switch (f) {
            case Field::Magnitude: return magnitude_;
            case Field::Phase: return phase_;
            case Field::Coherence: return coherence_;
        }
        return {};
    }

    [[nodiscard]] const CaptureMeta& meta() const noexcept { return meta_; }
    [[nodiscard]] std::size_t pointCount() const noexcept { return magnitude_.size(); }

    /// Derived, never stored: one source of truth for the axis, so a stored
    /// axis can never disagree with the data it indexes (spec §1).
    [[nodiscard]] double binHz() const noexcept {
        return meta_.fftSize > 0 ? meta_.sampleRate / static_cast<double>(meta_.fftSize) : 0.0;
    }

private:
    Trace(CaptureMeta m, std::vector<float> mag)
        : meta_(std::move(m)), magnitude_(std::move(mag)) {}

    CaptureMeta meta_;
    std::vector<float> magnitude_;
    std::vector<float> phase_;
    std::vector<float> coherence_;
};

}  // namespace rta::trace
```

- [ ] **Step 4: Build and run**

Run: `cmake --build build --config Release --parallel && ctest --test-dir build -C Release --output-on-failure`
Expected: 6 new cases pass. Full suite: **175/175**.

- [ ] **Step 5: Confirm the guard actually covers the new header**

Run: `ctest --test-dir build -C Release -R measure_has_no_framework_deps -V`
Expected: the line reads `OK (10 files scanned)` — up from 9. **If it still says
9, the `GLOBS` path is wrong and the header is unguarded even though the test is
green.** Fix the path before continuing.

- [ ] **Step 6: Commit**

```bash
git add app/src/trace/Trace.h app/tests/test_trace.cpp app/tests/CMakeLists.txt
```
Then commit as `feat(app): the trace record — one capture, fields present or absent`.

---

## Task 2: The column decimator

**Files:**
- Create: `app/src/view/TraceDecimator.h`
- Create: `app/tests/test_trace_decimator.cpp`
- Modify: `app/tests/CMakeLists.txt` (test source; `GLOBS`)

**Interfaces:**
- Consumes: nothing (deliberately independent of `Trace`, so it is testable on
  bare arrays).
- Produces: `rta::view::ColumnExtent{minValue, maxValue, hasData}` and
  `decimateToColumns(values, columnForBin, columnCount)`. Task 6 uses both.

- [ ] **Step 1: Write the failing test**

Create `app/tests/test_trace_decimator.cpp`:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "view/TraceDecimator.h"

#include <catch2/catch_test_macros.hpp>

using rta::view::ColumnExtent;
using rta::view::decimateToColumns;

TEST_CASE("each column brackets the true min and max of its bins", "[decimator]") {
    // Four bins into two columns: {3, -1} and {7, 2}.
    const std::vector<float> values{3.0f, -1.0f, 7.0f, 2.0f};
    const std::vector<int> columnForBin{0, 0, 1, 1};

    const auto out = decimateToColumns(values, columnForBin, 2);
    REQUIRE(out.size() == 2u);

    CHECK(out[0].hasData);
    CHECK(out[0].minValue <= -1.0f);
    CHECK(out[0].maxValue >= 3.0f);
    CHECK(out[1].minValue <= 2.0f);
    CHECK(out[1].maxValue >= 7.0f);
}

TEST_CASE("a column with no bins reports no data rather than zero", "[decimator]") {
    const std::vector<float> values{5.0f};
    const std::vector<int> columnForBin{2};

    const auto out = decimateToColumns(values, columnForBin, 3);
    REQUIRE(out.size() == 3u);
    CHECK_FALSE(out[0].hasData);
    CHECK_FALSE(out[1].hasData);
    CHECK(out[2].hasData);
    // A gap must not draw as a line to 0 dB -- that is a null the engineer
    // would read as real.
    CHECK(out[2].minValue == 5.0f);
}

TEST_CASE("a narrow null survives decimation", "[decimator]") {
    // The reason this is min/max and not one sample per column: the engineer
    // is looking for exactly this bin.
    std::vector<float> values(100, 0.0f);
    values[57] = -40.0f;
    std::vector<int> columnForBin(100, 0);
    for (int i = 0; i < 100; ++i) columnForBin[static_cast<std::size_t>(i)] = i / 10;

    const auto out = decimateToColumns(values, columnForBin, 10);
    REQUIRE(out.size() == 10u);
    CHECK(out[5].minValue == -40.0f);
}

TEST_CASE("mismatched or empty inputs yield nothing, not garbage", "[decimator]") {
    const std::vector<float> values{1.0f, 2.0f};
    const std::vector<int> shortMap{0};
    CHECK(decimateToColumns(values, shortMap, 2).empty());
    CHECK(decimateToColumns(values, std::vector<int>{0, 0}, 0).empty());
}

TEST_CASE("an out-of-range column index is skipped, not written past", "[decimator]") {
    const std::vector<float> values{1.0f, 2.0f};
    const std::vector<int> columnForBin{0, 9};
    const auto out = decimateToColumns(values, columnForBin, 1);
    REQUIRE(out.size() == 1u);
    CHECK(out[0].hasData);
    CHECK(out[0].maxValue == 1.0f);
}
```

- [ ] **Step 2: Wire it in and watch it fail**

Add `test_trace_decimator.cpp` to the test sources and
`${CMAKE_CURRENT_SOURCE_DIR}/../src/view/TraceDecimator.h` to `GLOBS`.
Reconfigure and build.
Expected: FAIL — `Cannot open include file: 'view/TraceDecimator.h'`.

- [ ] **Step 3: Write `app/src/view/TraceDecimator.h`**

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. No JUCE: enforced by the
// measure_has_no_framework_deps ctest. See
// docs/specs/2026-08-28-trace-library-and-session.md §4.
#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace rta::view {

/// What one pixel column of a trace covers. `hasData` is not redundant with a
/// zero range: a column no bin lands in must draw NOTHING. Drawing it as 0 dB
/// would put a line where there is no measurement.
struct ColumnExtent {
    float minValue = 0.0f;
    float maxValue = 0.0f;
    bool hasData = false;
};

/// Reduce per-bin values to one min/max extent per pixel column.
///
/// Min/max rather than one representative sample per column, because a narrow
/// peak or null falling between representatives simply disappears -- and a null
/// is what the engineer is hunting. `columnForBin` comes from the plot's own
/// log-frequency mapping, so this stays free of geometry and testable on bare
/// arrays.
[[nodiscard]] inline std::vector<ColumnExtent> decimateToColumns(
    std::span<const float> values, std::span<const int> columnForBin, int columnCount) {
    if (columnCount <= 0 || values.empty() || values.size() != columnForBin.size()) {
        return {};
    }

    std::vector<ColumnExtent> out(static_cast<std::size_t>(columnCount));
    for (std::size_t i = 0; i < values.size(); ++i) {
        const int column = columnForBin[i];
        if (column < 0 || column >= columnCount) continue;  // caller's mapping, not our crash

        auto& extent = out[static_cast<std::size_t>(column)];
        const float v = values[i];
        if (!extent.hasData) {
            extent.minValue = v;
            extent.maxValue = v;
            extent.hasData = true;
        } else {
            if (v < extent.minValue) extent.minValue = v;
            if (v > extent.maxValue) extent.maxValue = v;
        }
    }
    return out;
}

}  // namespace rta::view
```

- [ ] **Step 4: Build and run**

Run: `cmake --build build --config Release --parallel && ctest --test-dir build -C Release --output-on-failure`
Expected: 5 cases pass. Full suite **180/180**.

- [ ] **Step 5: Confirm the guard count moved**

Run: `ctest --test-dir build -C Release -R measure_has_no_framework_deps -V`
Expected: `OK (11 files scanned)`.

- [ ] **Step 6: Commit**

```bash
git add app/src/view/TraceDecimator.h app/tests/test_trace_decimator.cpp app/tests/CMakeLists.txt
```
Commit as `feat(app): decimate traces by column min/max so nulls survive`.

---

## Task 3: The session codec (pure, no filesystem)

**Files:**
- Create: `app/src/trace/SessionCodec.h`, `app/src/trace/SessionCodec.cpp`
- Create: `app/tests/test_session_codec.cpp`
- Modify: `app/tests/CMakeLists.txt` (test source, the `.cpp` as a source, `GLOBS` for the header)

**Interfaces:**
- Consumes: `rta::trace::Trace`, `CaptureMeta`, `LevelUnit`, `pointCountFor` (Task 1).
- Produces: `kSchemaVersion`, `LibraryEntry`, `SessionDocument`, `DecodeStatus`,
  `encodeIndex`, `decodeIndex`, `encodeTraceBlob`, `decodeTraceBlob`. Tasks 4
  and 5 use all of them.

Keeping encode/decode free of the filesystem is what makes the format testable
without touching disk; Task 4 adds the I/O around it.

- [ ] **Step 1: Write the failing test**

Create `app/tests/test_session_codec.cpp`:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "trace/SessionCodec.h"

#include <catch2/catch_test_macros.hpp>

using namespace rta::trace;

namespace {
CaptureMeta sampleMeta() {
    CaptureMeta m;
    m.id = "abc-123";
    m.capturedAtUnixMs = 1756400000000;
    m.deviceName = "Scarlett 2i2";
    m.sampleRate = 48000.0;
    m.fftSize = 8;
    m.window = "hann";
    m.averagingType = "fifo";
    m.averagingDepth = 16;
    m.effectiveAverages = 9.5;
    m.appliedDelaySamples = 137;
    m.calibrationOffsetDb = 94.0f;
    m.calibrationUnit = LevelUnit::DbSpl;
    return m;
}
}  // namespace

TEST_CASE("an index round-trips every metadata item", "[codec]") {
    SessionDocument doc;
    doc.captures.push_back(sampleMeta());
    doc.entries.push_back(LibraryEntry{"abc-123", "FOH left", "positions", 2, false});

    SessionDocument back;
    REQUIRE(decodeIndex(encodeIndex(doc), back) == DecodeStatus::Ok);

    REQUIRE(back.captures.size() == 1u);
    const auto& m = back.captures.front();
    CHECK(m.id == "abc-123");
    CHECK(m.capturedAtUnixMs == 1756400000000);
    CHECK(m.deviceName == "Scarlett 2i2");
    CHECK(m.sampleRate == 48000.0);
    CHECK(m.fftSize == 8);
    CHECK(m.averagingDepth == 16);
    CHECK(m.effectiveAverages == 9.5);
    CHECK(m.appliedDelaySamples == 137);
    CHECK(m.calibrationOffsetDb == 94.0f);
    // The one that turns a reload into a silent 94 dB lie if it is dropped.
    CHECK(m.calibrationUnit == LevelUnit::DbSpl);

    REQUIRE(back.entries.size() == 1u);
    CHECK(back.entries.front().name == "FOH left");
    CHECK(back.entries.front().group == "positions");
    CHECK(back.entries.front().shadeIndex == 2);
    CHECK_FALSE(back.entries.front().visible);
}

TEST_CASE("a newer schema is refused, never partly read", "[codec]") {
    SessionDocument doc;
    doc.schemaVersion = kSchemaVersion + 1;
    SessionDocument back;
    CHECK(decodeIndex(encodeIndex(doc), back) == DecodeStatus::NewerSchema);
    CHECK(back.captures.empty());
}

TEST_CASE("a name may contain = and newlines", "[codec]") {
    SessionDocument doc;
    doc.captures.push_back(sampleMeta());
    doc.entries.push_back(LibraryEntry{"abc-123", "gain = +3\nrow two", "g", 0, true});

    SessionDocument back;
    REQUIRE(decodeIndex(encodeIndex(doc), back) == DecodeStatus::Ok);
    CHECK(back.entries.front().name == "gain = +3\nrow two");
}

TEST_CASE("garbage decodes to Malformed, not to a half-read session", "[codec]") {
    SessionDocument back;
    CHECK(decodeIndex("not an index at all", back) == DecodeStatus::Malformed);
    CHECK(decodeIndex("", back) == DecodeStatus::Malformed);
}

TEST_CASE("a blob round-trips presence, not just numbers", "[codec]") {
    auto t = Trace::make(sampleMeta(), std::vector<float>{1.f, 2.f, 3.f, 4.f, 5.f});
    REQUIRE(t.has_value());
    REQUIRE(t->setCoherence(std::vector<float>{0.1f, 0.2f, 0.3f, 0.4f, 0.5f}));

    std::optional<Trace> back;
    REQUIRE(decodeTraceBlob(encodeTraceBlob(*t), sampleMeta(), back) == DecodeStatus::Ok);
    REQUIRE(back.has_value());
    CHECK(back->has(Field::Magnitude));
    CHECK(back->has(Field::Coherence));
    // Absent on the way in must be absent on the way out -- not zeros.
    CHECK_FALSE(back->has(Field::Phase));
    CHECK(back->field(Field::Coherence)[4] == 0.5f);
}

TEST_CASE("a blob whose id does not match its metadata is refused", "[codec]") {
    auto t = Trace::make(sampleMeta(), std::vector<float>(5, 0.0f));
    REQUIRE(t.has_value());
    CaptureMeta other = sampleMeta();
    other.id = "different-id";

    std::optional<Trace> back;
    CHECK(decodeTraceBlob(encodeTraceBlob(*t), other, back) == DecodeStatus::Malformed);
    CHECK_FALSE(back.has_value());
}
```

- [ ] **Step 2: Wire it in and watch it fail**

Add `test_session_codec.cpp` and `${CMAKE_CURRENT_SOURCE_DIR}/../src/trace/SessionCodec.cpp`
to the `rtatool_analysis_tests` sources, and add both
`../src/trace/SessionCodec.h` and `../src/trace/SessionCodec.cpp` to `GLOBS`.
Reconfigure and build. Expected: FAIL — header not found.

- [ ] **Step 3: Write the header**

`app/src/trace/SessionCodec.h`:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/trace. No JUCE: enforced by the
// measure_has_no_framework_deps ctest. Pure encode/decode with no filesystem,
// so the format is testable without touching disk (spec §3, §6).
#pragma once

#include "trace/Trace.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rta::trace {

inline constexpr int kSchemaVersion = 1;

/// How the user has organised a capture. Every field editable -- unlike
/// CaptureMeta, which records what the measurement was (spec §2).
struct LibraryEntry {
    std::string traceId;
    std::string name;
    std::string group;
    int shadeIndex = 0;
    bool visible = true;
};

struct SessionDocument {
    int schemaVersion = kSchemaVersion;
    std::vector<CaptureMeta> captures;
    std::vector<LibraryEntry> entries;
};

enum class DecodeStatus { Ok, Malformed, NewerSchema };

/// Line-oriented `key=value`, split on the FIRST `=` so values may contain it.
/// Newline and backslash are backslash-escaped; nothing else is.
[[nodiscard]] std::string encodeIndex(const SessionDocument& doc);

/// Refuses a newer schema outright rather than interpreting what it recognises:
/// a partly-read session is a plausible wrong measurement, which is worse than
/// no session at all (spec §3).
[[nodiscard]] DecodeStatus decodeIndex(std::string_view text, SessionDocument& out);

/// float32 little-endian field arrays behind a header carrying only what is
/// needed to read the bytes back: point count, which fields are present, and
/// the trace id as a cross-check. Descriptive metadata lives once, in the
/// index (spec §1.1).
[[nodiscard]] std::vector<std::byte> encodeTraceBlob(const Trace& trace);

[[nodiscard]] DecodeStatus decodeTraceBlob(std::span<const std::byte> blob,
                                           const CaptureMeta& meta,
                                           std::optional<Trace>& out);

}  // namespace rta::trace
```

- [ ] **Step 4: Write `SessionCodec.cpp`**

Implement exactly the contract above. Required behaviours, each pinned by a test
written in Step 1:

- `encodeIndex` writes `schema=<n>` first, then one `[capture]` block per
  `CaptureMeta` and one `[entry]` block per `LibraryEntry`, each field on its
  own `key=value` line. `calibrationUnit` writes as the literal `dbfs` or
  `dbspl`.
- `escape()` replaces `\` with `\\` and newline with `\n`; `unescape()` inverts
  it. Split each line at the first `=` only.
- `decodeIndex` reads `schema` before anything else. If it exceeds
  `kSchemaVersion`, return `NewerSchema` and leave `out` untouched. If the first
  line is not a `schema=` line, return `Malformed`.
- `encodeTraceBlob` writes: magic `"RTAT"`, `std::uint32_t` point count, a
  `std::uint32_t` presence bitmask (bit 0 magnitude, bit 1 phase, bit 2
  coherence), a `std::uint32_t` id length followed by the id bytes, then each
  present field's floats in Field order.
- `decodeTraceBlob` verifies the magic, verifies the id equals `meta.id`
  (`Malformed` if not), verifies point count equals `pointCountFor(meta.fftSize)`
  (`Malformed` if not), then reconstructs via `Trace::make` and the setters. On
  any failure `out` is left empty.

Keep the file under 300 lines. If it grows past that, split the blob codec into
`TraceBlobCodec.cpp` and add it to both the sources and `GLOBS`.

- [ ] **Step 5: Build and run**

Run: `cmake --build build --config Release --parallel && ctest --test-dir build -C Release --output-on-failure`
Expected: 6 cases pass. Full suite **186/186**.

- [ ] **Step 6: Confirm the guard count moved**

Expected: `OK (13 files scanned)` — the header and the `.cpp` both added.

- [ ] **Step 7: Commit**

```bash
git add app/src/trace/SessionCodec.h app/src/trace/SessionCodec.cpp app/tests/test_session_codec.cpp app/tests/CMakeLists.txt
```
Commit as `feat(app): the session codec — a format that refuses to half-read`.

---

## Task 4: The session store (filesystem, atomic)

**Files:**
- Create: `app/src/trace/SessionStore.h`, `app/src/trace/SessionStore.cpp`
- Create: `app/tests/test_session_store.cpp`
- Modify: `app/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: everything from Task 3.
- Produces: `rta::trace::StoreStatus` and `class SessionStore` with
  `writeIndex`, `readIndex`, `writeTrace`, `readTrace`.

- [ ] **Step 1: Write the failing test**

Create `app/tests/test_session_store.cpp`:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "trace/SessionStore.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

using namespace rta::trace;

namespace {
/// A directory that removes itself, so a failing assertion cannot leave the
/// next run reading a previous run's session.
struct TempDir {
    std::filesystem::path path;
    explicit TempDir(const char* name)
        : path(std::filesystem::temp_directory_path() / "rta-test" / name) {
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TempDir() { std::error_code ec; std::filesystem::remove_all(path, ec); }
};

CaptureMeta meta(std::string id) {
    CaptureMeta m;
    m.id = std::move(id);
    m.sampleRate = 48000.0;
    m.fftSize = 8;
    return m;
}
}  // namespace

TEST_CASE("a session round-trips through the filesystem", "[store]") {
    TempDir dir("roundtrip");
    SessionStore store(dir.path);

    SessionDocument doc;
    doc.captures.push_back(meta("t1"));
    doc.entries.push_back(LibraryEntry{"t1", "FOH", "positions", 0, true});
    REQUIRE(store.writeIndex(doc) == StoreStatus::Ok);

    SessionDocument back;
    REQUIRE(store.readIndex(back) == StoreStatus::Ok);
    CHECK(back.captures.size() == 1u);
    CHECK(back.entries.front().name == "FOH");
}

TEST_CASE("a missing index is NotFound, not Malformed", "[store]") {
    TempDir dir("missing");
    SessionStore store(dir.path);
    SessionDocument back;
    CHECK(store.readIndex(back) == StoreStatus::NotFound);
}

TEST_CASE("an interrupted write leaves the previous index intact", "[store]") {
    TempDir dir("atomic");
    SessionStore store(dir.path);

    SessionDocument first;
    first.captures.push_back(meta("t1"));
    REQUIRE(store.writeIndex(first) == StoreStatus::Ok);

    // Simulate a crash mid-write: a stray temp file beside a good index.
    { std::ofstream stray(dir.path / "session.index.tmp"); stray << "half writ"; }

    SessionDocument back;
    REQUIRE(store.readIndex(back) == StoreStatus::Ok);
    CHECK(back.captures.size() == 1u);
    CHECK(back.captures.front().id == "t1");
}

TEST_CASE("a trace blob round-trips through traces/", "[store]") {
    TempDir dir("blob");
    SessionStore store(dir.path);

    auto t = Trace::make(meta("t7"), std::vector<float>{1.f, 2.f, 3.f, 4.f, 5.f});
    REQUIRE(t.has_value());
    REQUIRE(store.writeTrace(*t) == StoreStatus::Ok);

    std::optional<Trace> back;
    REQUIRE(store.readTrace(meta("t7"), back) == StoreStatus::Ok);
    REQUIRE(back.has_value());
    CHECK(back->field(Field::Magnitude)[2] == 3.0f);
    CHECK_FALSE(back->has(Field::Phase));
}

TEST_CASE("a newer schema on disk is refused", "[store]") {
    TempDir dir("newer");
    SessionStore store(dir.path);
    SessionDocument doc;
    doc.schemaVersion = kSchemaVersion + 1;
    REQUIRE(store.writeIndex(doc) == StoreStatus::Ok);

    SessionDocument back;
    CHECK(store.readIndex(back) == StoreStatus::NewerSchema);
}
```

- [ ] **Step 2: Wire it in and watch it fail**

Add the test and `../src/trace/SessionStore.cpp` to the sources; add the header
and `.cpp` to `GLOBS`. Reconfigure, build. Expected: FAIL — header not found.

- [ ] **Step 3: Write `SessionStore.h` and `.cpp`**

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/trace. No JUCE: std::filesystem only, enforced
// by the measure_has_no_framework_deps ctest. See spec §3.
#pragma once

#include "trace/SessionCodec.h"

#include <filesystem>
#include <optional>

namespace rta::trace {

enum class StoreStatus { Ok, NotFound, Malformed, NewerSchema, IoError };

/// A session is a FOLDER, not a container file: capturing during a show appends
/// one blob and rewrites a small index, so an interruption costs one trace
/// rather than the session (spec §3).
class SessionStore {
public:
    explicit SessionStore(std::filesystem::path root) : root_(std::move(root)) {}

    /// Writes to `session.index.tmp` then renames over `session.index`, so an
    /// interrupted write leaves the previous index whole rather than truncated.
    [[nodiscard]] StoreStatus writeIndex(const SessionDocument& doc) const;
    [[nodiscard]] StoreStatus readIndex(SessionDocument& out) const;

    [[nodiscard]] StoreStatus writeTrace(const Trace& trace) const;
    [[nodiscard]] StoreStatus readTrace(const CaptureMeta& meta,
                                        std::optional<Trace>& out) const;

private:
    std::filesystem::path root_;
};

}  // namespace rta::trace
```

The `.cpp` creates `traces/` on demand, writes blobs as `traces/<id>.bin` in
binary mode, and maps `DecodeStatus` onto `StoreStatus`. `readIndex` returns
`NotFound` when `session.index` does not exist — distinct from `Malformed`,
because "no session here yet" and "this session is damaged" call for different
messages to the user.

- [ ] **Step 4: Build and run**

Run: `cmake --build build --config Release --parallel && ctest --test-dir build -C Release --output-on-failure`
Expected: 5 cases pass. Full suite **191/191**.

- [ ] **Step 5: Guard count**

Expected: `OK (15 files scanned)`.

- [ ] **Step 6: Commit**

```bash
git add app/src/trace/SessionStore.h app/src/trace/SessionStore.cpp app/tests/test_session_store.cpp app/tests/CMakeLists.txt
```
Commit as `feat(app): the session store — a folder, written atomically`.

---

## Task 5: The trace library and its revision counter

**Files:**
- Create: `app/src/trace/TraceLibrary.h`, `app/src/trace/TraceLibrary.cpp`
- Create: `app/tests/test_trace_library.cpp`
- Modify: `app/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Trace`, `LibraryEntry` (Tasks 1, 3).
- Produces: `class TraceLibrary` with `add`, `rename`, `setVisible`, `setGroup`,
  `setShadeIndex`, `soloOnly`, `remove`, `revision`, `entry`, `trace`,
  `entries`. Task 6 reads `revision()` and `entries()`.

- [ ] **Step 1: Write the failing test**

Create `app/tests/test_trace_library.cpp`:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "trace/TraceLibrary.h"

#include <catch2/catch_test_macros.hpp>

using namespace rta::trace;

namespace {
Trace makeTrace(std::string id) {
    CaptureMeta m;
    m.id = std::move(id);
    m.sampleRate = 48000.0;
    m.fftSize = 8;
    auto t = Trace::make(m, std::vector<float>(5, -20.0f));
    return std::move(*t);
}
}  // namespace

TEST_CASE("every mutation advances the revision", "[library]") {
    TraceLibrary lib;
    const auto start = lib.revision();

    lib.add(makeTrace("a"), "A", "left");
    const auto afterAdd = lib.revision();
    CHECK(afterAdd > start);

    CHECK(lib.rename("a", "A renamed"));
    CHECK(lib.revision() > afterAdd);
    const auto afterRename = lib.revision();

    CHECK(lib.setGroup("a", "right"));
    CHECK(lib.revision() > afterRename);
}

TEST_CASE("a call that changes nothing does NOT advance the revision", "[library]") {
    // The gate in Task 6 rebuilds a cached image whenever this moves. Bumping it
    // for a no-op would rebuild the whole layer on every idle UI tick.
    TraceLibrary lib;
    lib.add(makeTrace("a"), "A", "left");
    const auto before = lib.revision();

    CHECK(lib.setVisible("a", true));   // already visible
    CHECK(lib.rename("a", "A"));        // same name
    CHECK(lib.revision() == before);
}

TEST_CASE("an unknown id is refused and changes nothing", "[library]") {
    TraceLibrary lib;
    lib.add(makeTrace("a"), "A", "left");
    const auto before = lib.revision();

    CHECK_FALSE(lib.rename("nope", "X"));
    CHECK_FALSE(lib.setVisible("nope", false));
    CHECK(lib.revision() == before);
}

TEST_CASE("solo hides everything else and is one revision step", "[library]") {
    TraceLibrary lib;
    lib.add(makeTrace("a"), "A", "g");
    lib.add(makeTrace("b"), "B", "g");
    lib.add(makeTrace("c"), "C", "g");
    const auto before = lib.revision();

    lib.soloOnly("b");
    CHECK(lib.revision() == before + 1);
    CHECK_FALSE(lib.entry("a")->visible);
    CHECK(lib.entry("b")->visible);
    CHECK_FALSE(lib.entry("c")->visible);
}

TEST_CASE("library state is separate from capture metadata", "[library]") {
    TraceLibrary lib;
    lib.add(makeTrace("a"), "A", "left");
    REQUIRE(lib.rename("a", "New name"));
    // Renaming organises; it must not touch what the measurement was.
    CHECK(lib.trace("a")->meta().id == "a");
    CHECK(lib.entry("a")->name == "New name");
}
```

- [ ] **Step 2: Wire in, build, watch it fail**

Add the test and `../src/trace/TraceLibrary.cpp` to sources; header and `.cpp`
to `GLOBS`. Expected: FAIL — header not found.

- [ ] **Step 3: Implement**

`TraceLibrary.h` holds a `std::vector<LibraryEntry>` and a parallel
`std::vector<Trace>`, plus `std::uint64_t revision_ = 0`. Every setter compares
the incoming value with the current one and returns `true` without touching
`revision_` when they are equal; it returns `false` for an unknown id. `add`
assigns the next free shade index within the group. `soloOnly` performs one
sweep and bumps `revision_` at most once.

- [ ] **Step 4: Build and run**

Run: `cmake --build build --config Release --parallel && ctest --test-dir build -C Release --output-on-failure`
Expected: 5 cases pass. Full suite **196/196**.

- [ ] **Step 5: Guard count** — expected `OK (17 files scanned)`.

- [ ] **Step 6: Commit**

```bash
git add app/src/trace/TraceLibrary.h app/src/trace/TraceLibrary.cpp app/tests/test_trace_library.cpp app/tests/CMakeLists.txt
```
Commit as `feat(app): the trace library — organisation, and a revision that only moves when something changed`.

---

## Task 6: The repaint gate and the cached layer

**Files:**
- Create: `app/src/view/RepaintGate.h` (pure, tested)
- Create: `app/src/view/StoredTraceLayer.h`, `.cpp` (JUCE)
- Create: `app/tests/test_repaint_gate.cpp`
- Modify: `app/src/view/RtaView.h`, `app/src/view/RtaView.cpp`
- Modify: `app/CMakeLists.txt` (add `StoredTraceLayer.cpp` to `rtatool` and to
  `rtatool_snapshot`, which already lists `RtaView.cpp` for the offline render
  path), `app/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `TraceLibrary::revision()`, `entries()`, `trace()` (Task 5);
  `decimateToColumns`, `ColumnExtent` (Task 2).
- Produces: `rta::view::GateState{lastSequence, lastRevision}` and
  `shouldRepaint(GateState&, std::uint64_t sequence, std::uint64_t revision)`.

- [ ] **Step 1: Write the failing test**

Create `app/tests/test_repaint_gate.cpp`:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "view/RepaintGate.h"

#include <catch2/catch_test_macros.hpp>

using rta::view::GateState;
using rta::view::shouldRepaint;

TEST_CASE("a new live frame repaints", "[gate]") {
    GateState g;
    CHECK(shouldRepaint(g, 1, 0));
    CHECK_FALSE(shouldRepaint(g, 1, 0));  // same frame twice: no
    CHECK(shouldRepaint(g, 2, 0));
}

TEST_CASE("a library edit repaints even with no new frame", "[gate]") {
    // The bug this fixes: renaming or hiding a stored trace produced no
    // sequence change, so the plot never redrew and the edit looked ignored.
    GateState g;
    REQUIRE(shouldRepaint(g, 5, 0));
    CHECK(shouldRepaint(g, 5, 1));
    CHECK_FALSE(shouldRepaint(g, 5, 1));
}

TEST_CASE("both moving at once is still one repaint", "[gate]") {
    GateState g;
    REQUIRE(shouldRepaint(g, 1, 1));
    CHECK_FALSE(shouldRepaint(g, 1, 1));
}

TEST_CASE("the very first call always repaints", "[gate]") {
    GateState g;
    CHECK(shouldRepaint(g, 0, 0));
    CHECK_FALSE(shouldRepaint(g, 0, 0));
}
```

- [ ] **Step 2: Wire in, build, watch it fail**

Add the test to sources and `../src/view/RepaintGate.h` to `GLOBS`.
Expected: FAIL — header not found.

- [ ] **Step 3: Write `app/src/view/RepaintGate.h`**

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. No JUCE: enforced by the
// measure_has_no_framework_deps ctest. See spec §4.
#pragma once

#include <cstdint>

namespace rta::view {

/// What the gate remembers between calls. `primed` exists so the FIRST call
/// repaints even when both counters are legitimately zero -- an empty session
/// on a freshly started app is exactly that case, and it must still draw.
struct GateState {
    std::uint64_t lastSequence = 0;
    std::uint64_t lastRevision = 0;
    bool primed = false;
};

/// Repaint when the live snapshot advanced OR the library changed.
///
/// The single-part gate this replaces watched only the snapshot sequence, which
/// broke in both directions once stored traces existed: they carry no sequence,
/// so they redrew on every live frame, while editing one produced no sequence
/// change and so never redrew at all.
[[nodiscard]] inline bool shouldRepaint(GateState& state, std::uint64_t sequence,
                                        std::uint64_t revision) noexcept {
    if (state.primed && sequence == state.lastSequence && revision == state.lastRevision) {
        return false;
    }
    state.lastSequence = sequence;
    state.lastRevision = revision;
    state.primed = true;
    return true;
}

}  // namespace rta::view
```

- [ ] **Step 4: Build and run the gate tests**

Run: `cmake --build build --config Release --parallel && ctest --test-dir build -C Release --output-on-failure`
Expected: 4 cases pass. Full suite **200/200**. Guard count: `OK (18 files scanned)`.

- [ ] **Step 5: Commit the gate before touching the view**

```bash
git add app/src/view/RepaintGate.h app/tests/test_repaint_gate.cpp app/tests/CMakeLists.txt
```
Commit as `feat(app): a repaint gate that also watches the trace library`.

- [ ] **Step 6: Write `StoredTraceLayer`**

`app/src/view/StoredTraceLayer.h` / `.cpp`: owns a `juce::Image`, rebuilt only
when the library revision or the plot bounds change. For each visible entry it
builds `columnForBin` from the existing `PlotGeometry` log mapping, calls
`decimateToColumns`, and strokes one vertical extent per column. The live trace
is **not** drawn here — `RtaView` draws it over the cached image each frame,
which is what keeps the live repaint O(1) in trace count.

Read `app/src/view/RtaView.cpp` first and follow its existing drawing idiom and
its use of `MeasureColours`. Keep the file under 300 lines.

- [ ] **Step 7: Wire the gate into `RtaView`**

Replace `RtaView`'s sequence-only comparison with a `GateState` member and a
`shouldRepaint(gate_, sequence, library.revision())` call in its timer callback.
`RtaView` gains a `const TraceLibrary*` (nullable — the snapshot tool renders
without one, and that path must keep working).

- [ ] **Step 8: Build, test, and render**

```
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```
Expected: **200/200**, zero warnings.

Then:
```
cmd //c "build\app\rtatool_snapshot_artefacts\Release\rtatool_snapshot.exe shots 1100 760"
```
Expected: exit 0, six PNGs. Open `shots/rta-view.png` and confirm the live plot
is unchanged from before this task — with no library attached, the cached layer
must contribute nothing.

- [ ] **Step 9: Commit**

```bash
git add app/src/view/StoredTraceLayer.h app/src/view/StoredTraceLayer.cpp app/src/view/RtaView.h app/src/view/RtaView.cpp app/CMakeLists.txt
```
Commit as `feat(app): stored traces draw from a cached layer, live stays O(1)`.

---

## Self-review notes

**Spec coverage.** §1 presence model → Task 1. §1.1 metadata incl. calibration
unit → Tasks 1, 3. §1.2 optional raw audio → **not implemented**: the spec
defines it as opt-in and off by default, so the absence of an audio path is the
correct default state; the folder layout in Task 4 leaves `audio/` free for it.
§2 library → Task 5. §3 session folder, atomic write, schema refusal → Tasks 3,
4. §4 two-part gate, cached layer, min/max decimation → Tasks 2, 6. §5 module
boundaries → the guard step in every task. §6 test table → every row has a test
above except "interrupted write", which is Task 4 Step 1's `atomic` case.

**Not in scope, per spec §7:** targets, corridors, coherence gating, match
score, Bode layout, workspaces, spectrograph. No task touches them.

**Guard counts** run 9 → 10 → 11 → 13 → 15 → 17 → 18. If any task's count does
not move as stated, its `GLOBS` path is wrong and the header is unguarded.
