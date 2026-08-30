#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Falsification runs on the ANALYSIS PULSE itself, for the claims in
docs/dsp/2026-08-30-sweep-ir-l4a.md.

    .venv/Scripts/python.exe tools/verify_l4a_pulse.py

Writes nothing. These sections need no system under test: they measure what
`sweep (x) inverseFilter` is, before anything has been measured with it.
Sections A, B -> decision 5.  C -> decision 4.  F, I -> decision 8.
J -> decision 5's mechanism.

Companion: tools/verify_l4a_measurement.py, which drives real systems through
the deconvolution. Split because together they exceeded this project's 400-line
file cap, along the seam that made them long.
"""

import numpy as np
import scipy.fft
import scipy.signal

from l4a_sweep import (FS, sweep_and_inverse, linear_convolve,
                       pre_arrival, split_pre_and_main, band_ratio_db)


# ---------------------------------------------------------------- A
def section_a():
    """Where does the pre-arrival energy actually live?

    Record decision 5 recommends a 2-octave fade-in, costing low-frequency SNR.
    That is only worth paying if the sidelobe energy is IN the band of interest.
    If it sits below 20 Hz, band-limiting removes it for free and the record is
    over-engineering. This is the check that would falsify decision 5.
    """
    edges = [5, 10, 20, 40, 80, 160, 320, 1280, 5120, 20480]
    print("A. Which octave band holds the pre-arrival energy?  (falsifies dec. 5)")
    print("   pre-arrival energy relative to the same band of the main pulse")
    print("        band          fade 0.5 oct   fade 2.0 oct")
    rows = {}
    for fade_oct in (0.5, 2.0):
        sweep, inverse, _ = sweep_and_inverse(2.0, 20.0, 20000.0, fade_oct, 0.5)
        pulse = linear_convolve(sweep, inverse)
        pre, main = split_pre_and_main(pulse, len(inverse) - 1)
        rows[fade_oct] = band_ratio_db(pre, main, edges)
    for (lo, hi, a), (_, _, b) in zip(rows[0.5], rows[2.0]):
        print(f"   {lo:6d}-{hi:6d} Hz     {a:8.1f} dB    {b:8.1f} dB")


# ---------------------------------------------------------------- B
def section_b():
    """Does the AES-2id practice -- short fade, sweep started below the band of
    interest -- reach the same floor as a wide fade, judged over 20 Hz-20 kHz?"""
    print("\nB. Short fade + extended start frequency vs a wide fade (dec. 5)")
    print("   pre-arrival energy over 20 Hz - 20 kHz, relative to the main pulse")
    # Duration is held so the SWEEP RATE is constant at 5 octaves/second. Lowering
    # f1 at fixed duration would speed the sweep up and change two things at once,
    # which is how a fair-looking comparison decides the wrong question.
    rate = np.log2(20000.0 / 20.0) / 2.0
    cases = [(20.0, 0.5, "f1=20 Hz, fade 0.5 oct   today's default   "),
             (20.0, 2.0, "f1=20 Hz, fade 2.0 oct   record's proposal "),
             (10.0, 0.5, "f1=10 Hz, fade 0.5 oct   AES-2id practice  "),
             (5.0, 0.5, "f1= 5 Hz, fade 0.5 oct   two octaves margin"),
             (5.0, 2.0, "f1= 5 Hz, fade 2.0 oct   both              ")]
    for f1, fade_oct, label in cases:
        duration = np.log2(20000.0 / f1) / rate
        sweep, inverse, _ = sweep_and_inverse(duration, f1, 20000.0, fade_oct, 0.5)
        pulse = linear_convolve(sweep, inverse)
        pre, main = split_pre_and_main(pulse, len(inverse) - 1)
        (_, _, ratio), = band_ratio_db(pre, main, [20.0, 20000.0])
        print(f"   {label}   {ratio:7.1f} dB")


# ---------------------------------------------------------------- C
def section_c():
    """Is the analysis pulse actually flat across the band decision 4 derives?"""
    print("\nC. Flatness of the analysis pulse over [f1*e^(fin/L), f2*e^(-fout/L)] (dec. 4)")
    # The third row is Sweep::Config's ACTUAL defaults -- durationSec 10,
    # fadeInSec 0.02 clamped up to 2/startHz = 0.1 s, fadeOutSec 0.02 -- converted
    # into octaves. A record quoting a band computed at some other duration is
    # quoting a band the shipped code never produces.
    default_l = 10.0 / np.log(1000.0)
    default_in = 0.1 / (np.log(2.0) * default_l)
    default_out = 0.02 / (np.log(2.0) * default_l)
    # The fourth row is the configuration that will SHIP once the fade-in clamp
    # moves to octaves: fadeInOctaves 2.0, fadeOutSec left at 0.02 s. A record
    # whose flatness table lacks the row for the shipped default cannot be used
    # to write the test that asserts it.
    shipping_out = 0.02 / (np.log(2.0) * default_l)
    # The fifth row is the fixture the C++ tests use (100 Hz - 10 kHz, 2 s), so
    # the tolerance those tests assert has a measured row behind it rather than
    # being borrowed from a configuration with a different fade-out.
    fixture_l = 2.0 / np.log(100.0)
    fixture_out = 0.02 / (np.log(2.0) * fixture_l)
    # (duration, f1, f2, fade-in octaves, fade-out octaves). f1/f2 are per-case:
    # an earlier version held them at 20 Hz - 20 kHz for every row, so the row
    # labelled "the C++ fixture" described a sweep the fixture never renders.
    cases = ((2.0, 20.0, 20000.0, 0.5, 0.5),
             (2.0, 20.0, 20000.0, 2.0, 0.5),
             (10.0, 20.0, 20000.0, default_in, default_out),
             (10.0, 20.0, 20000.0, 2.0, shipping_out),
             (2.0, 100.0, 10000.0, 2.0, fixture_out))
    for duration, f1, f2, fade_in_oct, fade_out_oct in cases:
        sweep, inverse, length_l = sweep_and_inverse(duration, f1, f2, fade_in_oct, fade_out_oct)
        pulse = linear_convolve(sweep, inverse)
        size = 1 << int(np.ceil(np.log2(len(pulse))))
        mag = np.abs(scipy.fft.rfft(pulse, size))
        freqs = np.arange(len(mag)) * FS / size
        f_lo = f1 * np.exp(fade_in_oct * np.log(2.0))
        f_hi = f2 * np.exp(-fade_out_oct * np.log(2.0))
        sel = (freqs >= f_lo) & (freqs <= f_hi)
        db = 20 * np.log10(mag[sel] / mag[sel].mean())
        print(f"   T={duration:4.1f}s {f1:6.0f}-{f2:5.0f} Hz"
              f"  fade {fade_in_oct:5.2f}/{fade_out_oct:4.2f} oct"
              f"   band {f_lo:7.1f}-{f_hi:8.1f} Hz"
              f"   deviation {db.min():+.2f} .. {db.max():+.2f} dB")


# ---------------------------------------------------------------- F
def section_f():
    """float32 computed natively end to end, not a float64 result cast down.
    The first version of this measurement made exactly that mistake."""
    print("\nF. Native single precision vs double (dec. 8)")
    for fade_oct in (0.5, 2.0, 3.0):
        sweep, inverse, _ = sweep_and_inverse(2.0, 20.0, 20000.0, fade_oct, 0.5)
        origin = len(inverse) - 1
        out = []
        for dtype, name in ((np.float64, "float64"), (np.float32, "float32")):
            pulse = linear_convolve(sweep, inverse, dtype=dtype)
            before, peak = pre_arrival(pulse, origin)
            out.append(20 * np.log10(before.max() / peak))
        print(f"   {fade_oct:4.1f} oct   float64 {out[0]:8.1f}   float32 {out[1]:8.1f}"
              f"   difference {out[1]-out[0]:+.1f} dB")


# ---------------------------------------------------------------- I
def section_i():
    """Where does single precision actually become the limit?

    Decision 8 says the crossover is beyond the last point measured and refuses
    to guess. This finds it, along both axes that could move it: fade width
    (which lowers the floor) and transform size (which raises the arithmetic
    noise). The second matters more -- the shipping path uses 2^21, not 2^18.
    """
    print("\nI. Where single precision becomes the limit (dec. 8, was unmeasured)")
    print("   fade width, T = 2 s, transform 2^18:")
    for fade_oct in (2.0, 3.0, 4.0, 5.0, 6.0):
        got = []
        for dtype in (np.float64, np.float32):
            sweep, inverse, _ = sweep_and_inverse(2.0, 20.0, 20000.0, fade_oct, 0.5)
            pulse = linear_convolve(sweep, inverse, dtype=dtype)
            before, peak = pre_arrival(pulse, len(inverse) - 1)
            got.append(20 * np.log10(before.max() / peak))
        mark = "  <-- diverged" if abs(got[1] - got[0]) > 0.5 else ""
        print(f"   {fade_oct:4.1f} oct   float64 {got[0]:8.1f}   float32 {got[1]:8.1f}"
              f"   difference {got[1]-got[0]:+6.1f} dB{mark}")

    print("   transform size, fade held at 4 octaves:")
    for duration in (2.0, 8.0, 32.0):
        got = []
        for dtype in (np.float64, np.float32):
            sweep, inverse, _ = sweep_and_inverse(duration, 20.0, 20000.0, 4.0, 0.5)
            pulse = linear_convolve(sweep, inverse, dtype=dtype)
            before, peak = pre_arrival(pulse, len(inverse) - 1)
            got.append(20 * np.log10(before.max() / peak))
        size = 1 << int(np.ceil(np.log2(2 * int(duration * FS) - 1)))
        mark = "  <-- diverged" if abs(got[1] - got[0]) > 0.5 else ""
        print(f"   T={duration:5.1f}s  2^{int(np.log2(size)):2d}"
              f"   float64 {got[0]:8.1f}   float32 {got[1]:8.1f}"
              f"   difference {got[1]-got[0]:+6.1f} dB{mark}")


# ---------------------------------------------------------------- J
def section_j():
    """WHY do the two fades pull in opposite directions?

    The record carries a hypothesis: the inverse filter's +6 dB/oct envelope is
    at its maximum exactly where the reversed fade-OUT lands and at its minimum
    where the fade-IN lands, so the two discontinuities are weighted differently.
    That is a quantitative claim -- the weighting ratio is f2/f1, which for
    20 Hz-20 kHz is 60 dB -- and it predicts that removing the envelope makes the
    two fades behave alike. Both halves are tested here.
    """
    print("\nJ. Why the fades are asymmetric (record called this a hypothesis)")
    f1, f2 = 20.0, 20000.0
    print(f"   envelope weight where each fade lands: fade-out x1.0,"
          f" fade-in x{f1/f2:.4f}  =  {20*np.log10(f2/f1):.0f} dB apart")
    print("   floor when only ONE fade is widened, envelope on vs off:")
    print("      widened fade    octaves   +6 dB/oct on   plain reversal")
    for which in ("in", "out"):
        for octaves in (0.5, 2.0):
            row = []
            for whitening in (True, False):
                fi = octaves if which == "in" else 0.5
                fo = 0.5 if which == "in" else octaves
                sweep, inverse, _ = sweep_and_inverse(2.0, f1, f2, fi, fo,
                                                      whitening=whitening)
                pulse = linear_convolve(sweep, inverse)
                before, peak = pre_arrival(pulse, len(inverse) - 1)
                row.append(20 * np.log10(before.max() / peak))
            print(f"      fade-{which:<3s}         {octaves:4.2f}   {row[0]:12.1f}"
                  f"   {row[1]:14.1f}")
    print("   Result: the envelope explains why a wide fade-OUT is harmful -- without")
    print("   it, widening the fade-out is merely useless. It does NOT explain the")
    print("   fade-in, which buys the same ~33 dB either way. Two mechanisms, one found.")



if __name__ == "__main__":
    section_a()
    section_b()
    section_c()
    section_f()
    section_i()
    section_j()
