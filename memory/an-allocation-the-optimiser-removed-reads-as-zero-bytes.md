# An allocation the optimiser removed reads as zero bytes

*2026-09-18, macOS CI fix. Run 35306075307, `main` at d071269, case
`B0c AllocationProbe resets on construction so one case cannot read another's
bytes`:*

```
app/tests/test_average_group.cpp:376: FAILED:
  CHECK( first >= 1024 * sizeof(double) )
with expansion:
  0 >= 8192 (0x2000)
```

The measured code was

```cpp
const rta::test::AllocationProbe probe;
std::vector<double> a(1024);
a[0] = 1.0;
first = probe.bytes();
```

`first` was 8231 on Windows and 0 on macos-latest. **The allocation did not
happen.** libc++'s `std::allocator<T>::allocate` reaches the heap through
`__builtin_operator_new`, which clang documents as elidable: the compiler may
delete a matched new/delete pair whose pointer never escapes. At `-O3`, a
local vector written once and never read is exactly that, so clang removed it
and the probe correctly counted zero bytes of nothing.

libstdc++ and the MSVC STL call `::operator new` as a plain function, which no
optimiser is permitted to remove. That is the whole of why the same case was
green on ubuntu and windows: **not** a different set of replaced overloads,
and not a probe that fails to see libc++'s allocations.

## Why the log could not tell the two apart, and what fixed that

Every other case in that binary asserts a count of **zero** (`B4 push
allocates nothing`, `feedHop allocates nothing`) or a bound (`delta <= bound`).
All of those pass whether the probe works or is blind, so the suite as it
stood had exactly one assertion that could tell — the one that failed — and it
could not say which.

The fix therefore adds an **anchor** beside the container case: a direct
`::operator new(n)` call, which is neither a new-expression nor the builtin
and so cannot be elided on any platform. The two cases now split the
hypotheses in one CI round: anchor green + container red means elision, both
red means the replacement is not installed. Verified by mutation — making
`setAllocationCounting` a no-op turns both red with `0 == 8192` and
`0 >= 8192`, reproducing the macOS symptom exactly.

The container case itself keeps its subject and stops being elidable: its
element count comes from a `volatile std::size_t` and one element escapes
through a `volatile double` sink. Both are in
`app/tests/test_allocation_probe.cpp` with the reason at the definition.

## The rule

**A counter reading zero is not evidence that the path allocates nothing; it
is evidence that nothing was allocated.** Those differ whenever the optimiser
can see the whole lifetime of the buffer. For an allocation probe to prove the
claim its callers want, the measured buffer's result must ESCAPE — every real
caller in this repo satisfies that by asserting on a published value, and only
the probe's self-test did not.

This is `memory/a-fixture-can-be-too-well-behaved-to-fail.md` again, found in a
compiler optimisation rather than in fixture design: ask what wrong
implementation the fixture would catch, and if the answer depends on `-O0`,
the fixture is a demonstration.

## The sequel, found by a verifier on the same PR

The new file asserted, twice, that "Catch2 runs every file in this binary in one
process". **It does not, under ctest.** `catch_discover_tests` registers one
ctest test per Catch2 case and re-invokes the binary once per case with that
case's name as a filter, so every case gets a fresh process and the
program-global counter cannot carry between cases at all.

Which made the file's first assertion order-dependent and green anyway:

```
$ rtatool_analysis_tests "[allocationprobe]" --order decl
test_allocation_probe.cpp(139): FAILED:
  CHECK( rta::test::allocationBytes() == 0 )
with expansion:
  8192 (0x2000) == 0
```

`~AllocationProbe` deliberately does **not** clear the byte counter — the
header documents the reading as valid after the guard leaves scope, which is
what lets `measureGroupPublishBytes` return a measurement it took inside one.
So in a one-process run the previous case's total is still sitting in the
global, and an assertion placed *before* any probe is constructed reads it.
Pre-existing at `d071269` (residue `17735 == 0`), invisible to ctest by
construction.

Two lessons, and the second is the transferable one:

1. The claim was "counting is OFF outside a probe". Asserting that against
   **whatever the last case left behind** is a different and weaker claim. Zero
   the global first; then the assertion is the claim.
2. **A per-case process is a test isolation you did not ask for and cannot
   see.** ctest with `catch_discover_tests` gives it for free, so any shared
   mutable state between cases is untested rather than proven absent. The fix
   was not only the reset but two extra ctest entries —
   `allocation_probe_one_process_order_decl` and `..._lex` — that run the tag
   the way a developer and every mutation check run it: one process, one
   counter. Both orders, because they expose opposite residues and under `lex`
   alone the bug stays hidden. Proven by mutation: removing the reset turns
   `decl` red with `8192 == 0` and leaves `lex` green.

## Also written down while here

`AllocationProbe.h` now lists exactly the three functions it replaces and the
two things it therefore cannot see: an elided allocation, and an
**over-aligned** one. A type whose alignment exceeds 16 routes to
`operator new(size_t, align_val_t)`, which is not replaced because a portable
definition needs `_aligned_malloc` on MSVC and `std::aligned_alloc`
elsewhere. `rta::dsp::RingBuffer` is such a type (`alignas(64)` members). No
measured window reaches one today, because every caller arms the probe after
construction — a gap, not a present defect.
