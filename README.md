# RTA Tool

Cross-platform dual-FFT audio measurement software for tuning sound systems —
RTA, SPL metering, transfer function (magnitude / phase / coherence), and
impulse response analysis.

**Status: Phase 0 (foundations).** Not yet usable for measurement.

## Why another one

There are good open-source analysers already — [Open Sound Meter][osm],
[OpenOptimize][oo], [Friture][friture] — and this project openly borrows their
best ideas. It exists to close three gaps that all of them share:

1. **No proof the numbers are right.** Their DSP is entangled with their GUI and
   audio backend, so it cannot be tested without a sound card. Here, `core/`
   (`rta_core`) is a plain C++20 library that takes buffers of floats and returns
   numbers. Every algorithm is asserted against closed-form identities and
   golden vectors generated from NumPy/SciPy, on CI, with no hardware. That rule
   is enforced by a test, not by a comment — see `core/tests/check_no_framework_deps.cmake`.
2. **No ASIO.** In October 2025 Steinberg dual-licensed the ASIO SDK under
   GPLv3, removing the legal barrier that kept open-source Windows analysers off
   low-latency multichannel drivers for two decades. This project uses it.
3. **No multi-resolution transfer function.** A single fixed FFT gives too few
   points at 30 Hz and thousands of redundant ones at 16 kHz. Phase 3 implements
   a multi-time-window engine (decimation cascade with per-band FFT sizes).

## Building

Requirements: CMake ≥ 3.22, a C++20 compiler, Git.

```
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

On Linux/macOS substitute your generator (`-G Ninja` works everywhere).

| CMake option | Default | Meaning |
|---|---|---|
| `RTA_BUILD_TESTS` | `ON` | Build and register the `rta_core` test suite |
| `RTA_BUILD_APP` | `OFF` | Build the JUCE application shell (Phase 0b onward) |
| `RTA_ENABLE_ASIO` | `OFF` | Fetch the ASIO SDK and enable it (Windows only) |

## Licence

**AGPL-3.0-or-later.** The application shell links JUCE, which is offered under
AGPLv3 or a commercial licence; this project takes the AGPLv3 option. Code
borrowed from GPLv3 projects such as Open Sound Meter is compatible with this
under GPLv3 §13.

`rta_core` carries no framework dependency, so it can be relicensed or reused
independently of that choice.

[osm]: https://github.com/psmokotnin/osm
[oo]: https://github.com/John-Benton/OpenOptimize
[friture]: https://github.com/tlecomte/friture
