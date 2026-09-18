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

## Also written down while here

`AllocationProbe.h` now lists exactly the functions it replaces. The C++17
**over-aligned** set is among them: `operator new(size_t, align_val_t)`,
`operator delete(void*, align_val_t)` and
`operator delete(void*, size_t, align_val_t)`, allocating through
`_aligned_malloc` on MSVC and `posix_memalign` elsewhere (not
`std::aligned_alloc`, which requires `size` to be a multiple of `alignment`
and which Apple's libc enforces). So a type whose alignment exceeds 16 —
`rta::dsp::RingBuffer` with its `alignas(64)` members — is counted rather than
invisible. The three `B0d` cases in `test_allocation_probe.cpp` prove it: an
unelidable direct aligned `::operator new`, a `std::vector` of an over-aligned
element, and a heap-allocated `RingBuffer<float>` whose measurement read zero
before this change. One thing is still invisible: an allocation the optimiser
removed.

Two things B0d turned up on the way. The RingBuffer case's first bound,
`counted >= sizeof(RingBuffer<float>)`, was **not a gate**: a RingBuffer
allocates twice and its 4096-byte storage vector goes through the ordinary
unaligned new, so 192 bytes of aligned object sat buried under it and the case
stayed green under the mutation that removes the aligned trio (4135 bytes
counted). The shipped bound is derived — object **plus** storage, 4288 — and
that turns the 192 into the margin. And B0c's `allocationBytes() == 0` before
arming was never true in-process: only a probe's constructor resets the
program-global counter, so the anchor's 8192 was still standing.
`catch_discover_tests` hid it by giving every case its own process; running the
exe directly with `[allocationprobe]` does not. It now asserts that the reading
does not MOVE, which is the property it meant.
