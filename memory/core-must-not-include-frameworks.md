# `core/` must not include JUCE, Qt, or any audio-device API

`rta_core` takes `std::span<const float>` and returns numbers. That is the whole
contract.

**Why:** it makes every DSP algorithm provable offline — closed-form identities
and golden vectors, run on CI, with no sound card and no microphone. Open Sound
Meter and OpenOptimize both entangle DSP with their GUI and audio backend, which
is precisely why their measurement accuracy cannot be independently checked.
Losing this property would remove the project's main reason to exist.

**How it is enforced:** the `core_has_no_framework_deps` ctest greps `core/` for
such includes and fails with a non-zero exit. Verified 2026-08-26 by injecting
`#include <juce_core/juce_core.h>` into `core/src/dsp/Window.cpp` — the guard
exited 1 and named the file. It is not prose; do not weaken it to make something
compile.

**Known limitation of the GLOBS variant — found 2026-08-27, still open.** The
same script is reused by `measure_has_no_framework_deps` (`app/tests/
CMakeLists.txt`), which names files explicitly through `-DGLOBS=` instead of
scanning a directory. `check_no_framework_deps.cmake:25` expands that list with
`file(GLOB_RECURSE ...)`, so **a misspelled or deleted entry contributes zero
files and the test still passes**; only an entirely empty result trips the
FATAL at line 46. Proved by running the script with one real path plus one
bogus path: `OK (1 files scanned)`, exit 0.

So the explicit list guards what it correctly names, and silently stops guarding
anything it mis-names. Until that is fixed, the only way to know a file is
really covered is the scanned-file COUNT the test prints — when
`app/src/view/Readouts.h` was added the count went 8 → 9, which is what proved
it. If you add a file to that list, check the count moved.

Related: [[juce-is-agplv3-not-gplv3]]
