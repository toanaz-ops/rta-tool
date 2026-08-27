# Wiring these previews into the build

Not applied here -- `app/CMakeLists.txt` is owned by another agent this wave
(CLAUDE.md, "Module boundaries" / this task's house rules). This file states
the exact lines that target needs so whoever owns the CMake file next can
drop them in without re-deriving the source list.

## `rtatool_snapshot` (`app/CMakeLists.txt`, its `target_sources` call)

Add these four lines to the existing `target_sources(rtatool_snapshot
PRIVATE ...)` block -- alongside `src/view/PlotAxes.cpp` / `src/view/
RtaView.cpp`, which the tool already lists for the same reason (the offline,
JUCE-console-app render path):

```cmake
target_sources(rtatool_snapshot PRIVATE
    ${CMAKE_SOURCE_DIR}/tools/snapshot.cpp
    src/dev/SpecimenComponent.cpp
    src/AppTypefaces.cpp
    src/view/PlotAxes.cpp
    src/view/RtaView.cpp
    src/measure/Analyser.cpp
    src/measure/SyntheticSnapshot.cpp
    # --- add below: the three lane-L5 preview mockups + their shared furniture ---
    src/dev/preview/PreviewFurniture.cpp
    src/dev/preview/TransferFunctionPreview.cpp
    src/dev/preview/TargetMatchPreview.cpp
    src/dev/preview/PhaseAlignPreview.cpp
)
```

No new include directory is needed: `target_include_directories(rtatool_snapshot
PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)` already puts `app/src` on the
include root, which is what lets these files say `#include "dev/preview/
PreviewFurniture.h"` and `#include "view/MeasureColours.h"` the same way
`RtaView.cpp` already says `#include "view/PlotAxes.h"`.

`PreviewMath.h` is header-only (pure `<algorithm>`/`<cmath>`, no JUCE) and
needs no `target_sources` entry of its own -- same reason `PlotGeometry.h`
does not appear in either target's source list today.

`tools/snapshot.cpp` itself already includes the three preview headers and
renders `preview-tf.png`, `preview-target.png`, `preview-phase.png` (this
change is already applied, unlike the CMake lines above).

## `rtatool` (the shipping app target)

Not required for this wave -- these are `dev/` namespace previews reached
only through `rtatool_snapshot` today, the same way `SpecimenComponent` is
built into `rtatool_snapshot` for `specimen.png` but is also separately
wired into `rtatool` (`src/dev/SpecimenComponent.cpp` appears in BOTH
target_sources lists already, reached from a developer menu at runtime).
If a later wave wants these three previews reachable from the running app's
own developer menu the same way, add the same four lines to `rtatool`'s
`target_sources` block too -- nothing in these four files depends on
anything `rtatool_snapshot` has that `rtatool` does not (both link `az_ui`
and `rta::core`; none of the four files touch device I/O or the analysis
thread).

## Verification done without CMake

Because `app/CMakeLists.txt` could not be touched this wave, all four new
`.cpp` files were compiled standalone with `cl.exe /W4 /std:c++20 /c`, using
the include directories and preprocessor defines read out of the already-
generated `build/app/rtatool_snapshot.vcxproj` (Debug config) -- see the
session report for the exact command and its zero-warning output. This is a
syntax/type/warning check only; it does not link or run the renderer. The
real coverage -- confirming `preview-tf.png` / `preview-target.png` /
`preview-phase.png` actually render -- needs the CMake lines above applied
and then:

```
cmake --build build --config Release --target rtatool_snapshot --parallel
build/app/rtatool_snapshot_artefacts/Release/rtatool_snapshot.exe shots 1100 760
```
