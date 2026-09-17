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
2. **ASIO absent from default builds.** Open Sound Meter carries an optional
   ASIO backend behind a `USE_ASIO` build flag, gated for years by Steinberg's
   proprietary SDK licence. In October 2025 Steinberg dual-licensed the SDK
   under GPLv3, so this project ships multichannel ASIO on by default, with
   per-channel measurement/reference routing.
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

### Vendored dependencies

Third-party sources are vendored verbatim under `external/`, each with its
upstream licence file and a `PROVENANCE.md` carrying the release tag and a
SHA-256 measured against that tag. They carry **no added SPDX line**: editing a
vendored file to satisfy a local convention is how it stops being verifiable
against upstream.

| path | version | licence | used by |
|---|---|---|---|
| `external/nlohmann/` | v3.12.0 | MIT | **tests only** — `app/tests/test_api_schema.cpp` is the sole translation unit that includes it, and `no_json_parser_in_shipped_code` enforces that shipped code never does |
| `external/cpp-httplib/` | v0.56.0 | MIT | the remote API's HTTP server — `app/src/api/ApiServer.cpp` is the sole translation unit that includes it, and `no_server_library_outside_api` enforces that. TLS and every optional compression backend are left undefined |

**Mongoose was the near miss worth recording.** Its licence offers GPL-2.0 with
no "or any later version", which is incompatible with AGPLv3, and its
commercial arm conflicts with the AGPL source release this project is committed
to — neither arm works. cpp-httplib's MIT terms are why the choice was
available at all, so this dependency is not one to "simplify" later without
re-reading `external/cpp-httplib/PROVENANCE.md`.

MIT is a lax permissive non-copyleft licence the FSF calls compatible with the
GNU GPL; it flows into this AGPL-3.0-or-later work imposing only notice
retention, which is why each `LICENSE` file is vendored beside its header and
is never deleted.

[osm]: https://github.com/psmokotnin/osm
[oo]: https://github.com/John-Benton/OpenOptimize
[friture]: https://github.com/tlecomte/friture
