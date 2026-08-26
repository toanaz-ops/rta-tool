# Build toolchain present on this machine (verified 2026-08-26)

- **MSVC 14.51** (`cl.exe` 19.51.36248) from **Visual Studio Build Tools 2026**,
  installed at `C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools`.
  Note the `(x86)` path and the `18` version folder — *not* the usual
  `C:\Program Files\Microsoft Visual Studio\2022\...` location.
- Windows SDK **10.0.26100.0**
- CMake **4.3.2**, Ninja **1.13.2**, Git **2.53**
- MinGW-w64 GCC **16.1.0** (UCRT) also on PATH — fallback only; JUCE + ASIO on
  MinGW is not a supported path.
- Python **3.14.6**, **without numpy or scipy**. `tools/gen_golden.py` needs them;
  install into a venv before Phase 1 golden-vector work.

The CMake generator string that works here is `"Visual Studio 18 2026"` with
`-A x64`. It locates MSVC without needing `vcvars64.bat`, which is why the build
commands in README.md run unchanged from Git Bash.
