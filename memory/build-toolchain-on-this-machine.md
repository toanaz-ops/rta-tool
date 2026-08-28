# Build toolchain present on this machine (verified 2026-08-26)

- **MSVC 14.51** (`cl.exe` 19.51.36248) from **Visual Studio Build Tools 2026**,
  installed at `C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools`.
  Note the `(x86)` path and the `18` version folder — *not* the usual
  `C:\Program Files\Microsoft Visual Studio\2022\...` location.
- Windows SDK **10.0.26100.0**
- CMake **4.3.2**, Ninja **1.13.2**, Git **2.53**
- MinGW-w64 GCC **16.1.0** (UCRT) also on PATH — fallback only; JUCE + ASIO on
  MinGW is not a supported path.
- Python **3.14.6**. The bare interpreter has **no numpy or scipy** — but a venv
  that does was created since, and the golden vectors under `core/tests/golden/`
  were generated with it (their headers record `numpy 2.5.2, scipy 1.18.1`).

**The venv lives in the MAIN checkout only, and worktrees cannot see it**
(verified 2026-08-28):

```
D:\DEV CAVE EP3\PRJ010-RTA-TOOL\.venv\Scripts\python.exe   ->  numpy 2.5.2, scipy 1.18.1
python -c "import numpy"   inside any worktree              ->  ModuleNotFoundError
```

`.venv` is gitignored, so `git worktree add` does not carry it over. A lane
session working in its own worktree will hit `ModuleNotFoundError`, conclude the
"no numpy" blocker recorded in the Phase-1 FEAT doc is still live, and rebuild a
venv that already exists one directory up. Call the main checkout's interpreter
by absolute path instead — do not create a second venv, and do not conclude from
a worktree that this machine lacks numpy.

The CMake generator string that works here is `"Visual Studio 18 2026"` with
`-A x64`. It locates MSVC without needing `vcvars64.bat`, which is why the build
commands in README.md run unchanged from Git Bash.
