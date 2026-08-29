# RTA Tool — memory index

One line per memory. Content lives in the linked file, never here.

- [ASIO SDK is GPLv3 since Oct 2025](asio-sdk-is-gplv3-since-oct-2025.md) — the 20-year barrier to open-source ASIO fell right before this project started
- [JUCE is AGPLv3, not GPLv3](juce-is-agplv3-not-gplv3.md) — why this project is AGPL-3.0 and what that costs later
- [core/ must not include frameworks](core-must-not-include-frameworks.md) — the architectural rule, and the test that enforces it
- [Build toolchain on this machine](build-toolchain-on-this-machine.md) — MSVC 2026 lives in an unusual path; the venv (main checkout only, not worktrees) has numpy and scipy
- [RealFft is single precision](float32-fft-precision.md) — why a 1e-9 tolerance on any FFT-derived value fails against correct code, and what shape a correct tolerance has
- [Dual-FFT conventions that must not be tidied](dual-fft-conventions.md) — the four sign/definition choices a later reader will be tempted to flip, and what flipping each one breaks
