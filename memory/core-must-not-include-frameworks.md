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

Related: [[juce-is-agplv3-not-gplv3]]
