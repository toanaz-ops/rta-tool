# RTA Tool — memory index

One line per memory. Content lives in the linked file, never here.

- [ASIO SDK is GPLv3 since Oct 2025](asio-sdk-is-gplv3-since-oct-2025.md) — the 20-year barrier to open-source ASIO fell right before this project started
- [JUCE is AGPLv3, not GPLv3](juce-is-agplv3-not-gplv3.md) — why this project is AGPL-3.0 and what that costs later
- [core/ must not include frameworks](core-must-not-include-frameworks.md) — the architectural rule, and the test that enforces it
- [Build toolchain on this machine](build-toolchain-on-this-machine.md) — MSVC 2026 lives in an unusual path; the venv (main checkout only, not worktrees) has numpy and scipy
- [RealFft is single precision](float32-fft-precision.md) — why a 1e-9 tolerance on any FFT-derived value fails against correct code, and what shape a correct tolerance has
- [Dual-FFT conventions that must not be tidied](dual-fft-conventions.md) — the four sign/definition choices a later reader will be tempted to flip, and what flipping each one breaks
- [Re-verify what the change could have changed](reverify-what-the-change-could-have-changed.md) — after a merge, a name-only diff can prove a rebuild is unnecessary; and when it cannot, it tells you which configuration to run
- [A fixture can be too well-behaved to fail](a-fixture-can-be-too-well-behaved-to-fail.md) — the capture-length test read 0.0026 dB and could not have failed; three fixes later it read 1.76 dB
- [A threshold read off a grid is that grid's floor](a-threshold-read-off-a-grid-is-that-grids-floor.md) — three thresholds, three sessions, each the floor of its own grid; what to ask before adopting a fourth, and when a scalar gate does survive
- [A fixed defect returns through the silent fallback](a-fixed-defect-returns-through-the-silent-fallback.md) — the branch where the fix does not run quietly restores the bug, and no fixture is short enough to notice
