# RealFft transforms in float, and that sets every tolerance above it

`core/include/rta/dsp/RealFft.h` takes `std::span<const float>` and writes
`std::span<std::complex<float>>`. The transform is single precision throughout.
Two consequences, both of which cost time in lane L2 before being written down:

**1. Roughly 1.3e-6 relative error on a 1024-point transform**, even when the
input satisfies an exact mathematical identity. A test asserting
`epsilon(1e-9)` on anything derived from FFT bins is asserting more precision
than the transform has, and will fail against correct code.

**2. The ABSOLUTE error floor is set by the LARGEST bins, not by the bin under
test.** This is the half that surprises people. A 10-stage butterfly propagates
rounding from wherever the energy is; for pink noise that is near DC. By the top
of the band a bin's own magnitude has decayed by ~500x while the error floor has
not moved at all. So a tolerance shaped as "parts per million of *this* bin"
is unattainable at the top of a decaying spectrum, and is the wrong shape.

The right shape carries both terms:

```cpp
const double peak = *std::max_element(psd.begin(), psd.end());
const double tolerance = binValue * 1e-6 + peak * 1e-7;
```

Low bins stay parts-per-million tight because the first term dominates; the
second is the floor the transform actually imposes.

## Where this bit, twice, on 2026-08-29

- `core/tests/test_dualfft.cpp`, the cross-spectrum sign test: failed on a
  CORRECT engine at k=510 of 511 (sxx 1.9e-07, residual 3.9e-13 against a
  1.9e-13 margin). Diagnosed and fixed with the two-term tolerance above.
- `core/tests/test_transfer_estimator.cpp`, the gain-and-delay identity: the
  plan asked for `epsilon(1e-9)` on `h[k]`, loosened to `epsilon(1e-5)` /
  `margin(5e-6)`.

Two different agents, two different tasks, same ceiling — found independently
both times, which is why it is recorded here rather than in one test comment.

## What is NOT the fix

`DualFftEngine` already promotes to `std::complex<double>` at the earliest
possible point, before any arithmetic on the bins. The engine is not the limit.
Promoting earlier cannot help: the precision was lost one layer down, inside the
transform. Widening a tolerance without understanding which of the two terms is
binding is how a real error gets hidden — see [[dual-fft-conventions]].

Changing `RealFft` to double would fix it and is not obviously worth it: it
doubles the memory traffic on the hot analysis path to buy precision no
measurement needs. If that trade is ever revisited, this file is the reason it
would be revisited.
