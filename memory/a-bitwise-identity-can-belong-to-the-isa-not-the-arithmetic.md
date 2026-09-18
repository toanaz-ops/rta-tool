# A bitwise identity can belong to the ISA, not to the arithmetic

*2026-09-18. Run 35306075307 on `main` at d071269: ubuntu and windows 774/774,
macos-latest 4 red. GitHub Actions had just come back from a billing block, so
this was the first three-OS run in a while — none of the four failures was
caused by a code change.*

## The four failures were one cause

`app/src/measure/Levels.h` computes

```cpp
return clampLevelDb(10.0 * std::log10(power) + kFullScaleSineOffsetDb);
```

and three tests asserted `levelDbFs(0.5) == 0.0` **bitwise**. That equality is
not an identity. It is a coincidence of two roundings:

```
log10(0.5)                     = -0x1.34413509f79ffp-2   (correctly rounded)
fl(10 * log10(0.5))            = -0x1.8151824c7587fp+1
kFullScaleSineOffsetDb         = +0x1.8151824c7587fp+1   (the literal 3.0102999566398120)
sum                            =  0.0                    exactly
```

Round the product first and the two cancel to the bit. Keep the product at full
width — which is what a **single `fma`** does — and the sum is `2^-53` =
1.1102230246251565e-16 dB. Apple clang on arm64 contracts that multiply-add
because FMA is in the baseline ISA; MSVC (`/fp:precise`) and gcc on x86-64 do
not, because x86-64's baseline has no FMA instruction to contract into.

The fourth failure was the same mechanism one layer down: the 198045-byte
`app/tests/golden/api-v1-snapshot.json` regression lock. 35 of the 2049
`spectrum.spectrumDb` values differed, **every one by exactly ±1 float32 ULP**,
because `std::norm` on a `complex<float>`, the FFT butterflies and
`acc += alpha * (psd - acc)` in `SpectrumEngine::analyseFrame` are all
multiply-adds too.

## The diagnostic that isolated it, without a Mac

**ubuntu-latest and windows-latest agreed on all 198045 bytes.** Different
libm (glibc vs UCRT), different compiler, same bits. That rules out libm as the
source and leaves the one thing the two share and macOS does not: an ISA with
no baseline FMA. One flag — `-ffp-contract=off`, non-MSVC, in the root
`CMakeLists.txt` — therefore covers the whole observed divergence.

Read the pattern before reaching for a tolerance. Two-of-three agreeing is
evidence about *what they share*, and it is available from the log.

## Two fixes, for two different defects

1. **`-ffp-contract=off`.** Not a new numeric regime: it is the regime every
   golden vector and every bitwise identity in this repo was already written
   and validated under, since MSVC is the development toolchain. The flag says
   so instead of relying on an instruction not existing. Cost is throughput in
   inner loops, not latency — the audio callback only copies into a ring
   buffer — and a hot loop that ever needs FMA can take it back at that site
   with `#pragma clang fp contract(fast)`.
2. **The assertions.** They stay non-bitwise even though the flag would make
   the bitwise form pass, because *an assertion whose truth is a compiler flag
   records the flag, not the arithmetic*. The bitwise claim moved to
   `levelDbFs(1.0) == kFullScaleSineOffsetDb`, which is exact **by
   arithmetic**: `log10(1.0)` is exactly `+0.0`, so `10*0.0 + k` and
   `fma(10.0, 0.0, k)` are both exactly `k` — no rounding happens at all, on
   any platform, under any contraction setting.

The bound at `p = 0.5` is derived, not read off the run: 1 ulp of libm error in
`log10(0.5)` (ulp = 2^-54, the value is in [0.25,0.5)) amplified by 10 gives
5.6e-16, plus at most 2^-52 = 2.2e-16 for rounding — or not rounding — the
product (in [2,4), ulp 2^-51); the final add is exact by Sterbenz. Total
7.8e-16 dB, against the 1e-15 the sibling assertions in the same case already
used, and the observed 1.11e-16.

## The rule

**Before asserting a floating-point equality bitwise, name the rounding that
makes it hold.** If the answer involves a product being rounded before a sum,
the claim belongs to the evaluation order the compiler chose, not to the
values — and a platform with FMA is free to choose differently. Pick a fixture
where every intermediate is exactly representable, and the equality becomes a
property of the numbers.

## Related

- `memory/float32-fft-precision.md` — what the numbers above are worth in the
  first place, and the right *shape* for a tolerance on an FFT-derived value.
- `memory/a-shortest-decimal-is-not-a-fixed-point-of-narrowing.md` — same
  family: an exactness claim transplanted across a precision boundary it was
  never measured at.
- PR #5 retired `CHECK(std::isinf(nyquistAtten))` for exactly this reason:
  *"the exactness given up there was never real: it held only under x86's
  rounding."* Same finding, three lanes earlier, not yet written down here.
