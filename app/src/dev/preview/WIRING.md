# Wiring these previews into the build — APPLIED, 2026-08-28

**Nothing to do here. This file is kept only so the note it replaces cannot
mislead a third reader.**

The four preview sources are in the build:

```
app/CMakeLists.txt:170-173        (was :117-120 when this note was written;
    src/dev/preview/PreviewFurniture.cpp         the file has grown since, and
    src/dev/preview/TransferFunctionPreview.cpp  a line number in a doc is a
    src/dev/preview/TargetMatchPreview.cpp       claim with an expiry date --
    src/dev/preview/PhaseAlignPreview.cpp        exactly this file's own point)
```

**Updated 2026-09-16, L7-ALIGN task I.** `PhaseAlignPreview.cpp` is no longer a
canned-data mockup: it builds a real `rta::view::CrossoverSurface`. That target
names every source one by one — there is no glob — so three more entries had to
go in beside it or `preview-phase.png` fails to LINK:

```
    src/view/CrossoverSurface.cpp
    src/measure/CrossoverTopology.cpp
    src/trace/VirtualTrace.cpp
```

Anyone repointing another specimen at a real model owes the same edit.

and `rtatool_snapshot` renders them. Verified 2026-08-28: a run of

```
build/app/rtatool_snapshot_artefacts/Release/rtatool_snapshot.exe shots 1100 760
```

exits 0 and writes six PNGs, including `preview-tf.png`, `preview-target.png`
and `preview-phase.png`.

## Why this file still exists

Its previous contents were a hand-off note from an earlier wave saying the CMake
lines were *not* applied, because `app/CMakeLists.txt` was owned by another agent
at the time. The lines were applied shortly afterwards. The note was not.

It cost real time: the L5 station-1 research pass read this file, believed it,
and reported to the orchestrator that the previews had never rendered from this
tree — while three rendered PNGs sat in `shots/`. The claim was caught only
because the orchestrator had looked at those files itself an hour earlier.

The lesson is the same one the handoff records about stale numbers: **a note
about the state of the build is a claim with an expiry date, and the build file
itself is the only thing that can be believed.** If a future wave needs to defer
a CMake edit again, the deferral belongs in the handoff — which the next session
is obliged to reconcile — not in a file beside the source, where nothing will
ever prompt anyone to delete it.
