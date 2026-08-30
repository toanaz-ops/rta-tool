# A misconfigured build goes 99 % of the way, then fails like a normal test

Lane L4b, 2026-08-30. A fresh build directory was configured with `-G Ninja` and
`-DCMAKE_BUILD_TYPE=Release`. CMake silently selected **MinGW g++** from
WinLibs on the PATH instead of the MSVC toolchain this project is verified on.

196 of 196 objects compiled cleanly. Then Catch2's test-discovery step died with
`0xc0000139` (STATUS_ENTRYPOINT_NOT_FOUND — the executables could not find their
MinGW runtime DLLs), and ctest reported:

    rta_core_tests_NOT_BUILT-b12d07c (Not Run)

Read through `tail -5`, that looks like an ordinary test failure in the project's
own code. It is not: nothing had been tested at all, and the compiler was not the
one any of this project's numbers were measured with.

**Configure with the generator the project documents** — here
`-G "Visual Studio 18 2026" -A x64` — and check which compiler CMake actually
chose before trusting any count from that directory. The configure log names it:

    -- Check for working CXX compiler: .../MSVC/14.51.36231/bin/Hostx64/x64/cl.exe

The general shape: **a wrong toolchain does not fail early and loudly.** It fails
late, at the last step, with a message that resembles the failures you are
expecting to debug.
