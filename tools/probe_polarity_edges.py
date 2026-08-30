#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""The band-edge estimator the polarity gate will read, and the measurement of
how much its answer moves for a system sitting on the threshold.

    .venv/Scripts/python.exe tools/probe_polarity_edges.py

Writes nothing.

## Why this file replaces the estimator the two earlier probes used

`probe_polarity_bandwidth.py` and `probe_polarity_margin.py` both measure the
arrival's spectrum with a FIXED 50 ms window and read the -10 dB crossings at
integer bin indices. Three defects were measured in that estimator, and all
three had to be fixed before any threshold could be read off it -- calibrating a
constant on an estimator that is about to change is how the 2.5-octave gate
died.

1. **A fixed window cannot resolve a low band.** 50 ms is 2.5 cycles at 50 Hz.
   Measured: a cheby1 order 12 band-pass designed 50-71 Hz reports edges of
   46.9 Hz to 18270 Hz, while the driven signal itself is -101 dB at 10-20 kHz.
   The estimator was wrong by about 90 dB, and the reading landed a subwoofer
   inside a gate meant to refuse it. Fix: size the window from the low edge it
   just measured, and measure again.

2. **Bins that were never excited get counted.** The sweep starts at 20 Hz, so
   everything below that in the transform is leakage and inverse-filter fade,
   not measured response. A butter order 2 band designed 7.5-120 Hz reported
   4.09 octaves by counting a bin at 11.7 Hz. Fix: clamp the search to the band
   the sweep actually excited.

3. **Integer bin indices quantise the answer.** At 4096 points and 48 kHz the
   bins are 11.7 Hz apart, so near 100 Hz one bin step is 0.16 octave of
   quantisation -- charged straight to the scatter of a threshold expressed in
   hertz. Fix: interpolate the crossing between adjacent bins in log magnitude.

## What the numbers here are for

The gate is a pair of band edges, and a real box sits near one of them. If the
measured edge moves more than the distance to the threshold, the same box reads
Answer one time and Unknown the next, and an operator learns to distrust the
feature. So this file reports the SCATTER of the estimate, before and after the
fix, and that scatter -- not a tidy round number -- is what a threshold and its
tolerance have to be chosen against.

Second-order sections throughout; transfer-function form is prohibited repo-wide
by core/tests/check_no_polynomial_form.cmake. The guard greps `tools/*.py` and
its patterns match a call site, so describe the prohibition rather than spelling
the forbidden argument.
"""

import numpy as np
import scipy.fft
import scipy.signal

from l4a_sweep import FS, sweep_and_inverse, linear_convolve

MINUS_10_DB = 10.0 ** (-10.0 / 20.0)


def _interpolated_crossing(mag_db, k, k_next, freqs, threshold_db):
    """Where between two bins the magnitude crosses the threshold, in Hz.

    Linear in dB against linear in frequency. The alternative -- taking the bin
    index itself -- charges up to a full bin of quantisation to the answer, and
    near 100 Hz with 11.7 Hz bins that is 0.16 octave of pure estimator noise on
    a threshold expressed in hertz.
    """
    y0, y1 = mag_db[k], mag_db[k_next]
    if y1 == y0:
        return float(freqs[k])
    t = (threshold_db - y0) / (y1 - y0)
    t = min(max(t, 0.0), 1.0)
    return float(freqs[k] + t * (freqs[k_next] - freqs[k]))


def band_edges(tail, sweep_lo, sweep_hi, cycles=10.0, floor_seconds=0.05,
               sample_rate=FS, _pass=0):
    """The -10 dB edges of the arrival's spectrum, in Hz.

    `tail` starts at the deconvolution's origin. `sweep_lo`/`sweep_hi` are the
    band the excitation actually covered; bins outside it are not evidence.

    Recurses ONCE: the first pass uses `floor_seconds` to get a rough low edge,
    the second sizes the window at `cycles` periods of it. One re-measurement is
    enough because the second window only ever grows, and a longer window cannot
    push the low edge higher; recursing further would chase its own tail.
    """
    span = min(int(round(floor_seconds * sample_rate)), len(tail))
    if span <= 1:
        return None
    size = 1 << int(np.ceil(np.log2(span)))
    padded = np.zeros(size)
    padded[:span] = tail[:span]
    mag = np.abs(scipy.fft.rfft(padded))
    freqs = np.arange(len(mag)) * sample_rate / size

    inband = (freqs >= sweep_lo) & (freqs <= sweep_hi)      # defect 2
    if not inband.any() or mag[inband].max() <= 0.0:
        return None
    peak = mag[inband].max()
    with np.errstate(divide="ignore"):
        mag_db = 20.0 * np.log10(np.maximum(mag, 1e-30) / peak)
    hits = np.nonzero(inband & (mag_db >= -10.0))[0]
    if hits.size == 0:
        return None
    low_bin, high_bin = int(hits[0]), int(hits[-1])

    if _pass == 0:                                          # defect 1
        rough_low = max(freqs[low_bin], 1.0)
        # ceil, not round: the rule is "at least `cycles` cycles", and rounding
        # down can hand back 9.99 of them. It also keeps this in step with the
        # C++, where a one-sample disagreement can cross a power-of-two boundary
        # and halve the bin width, moving every interpolated edge with it.
        needed = int(np.ceil(cycles * sample_rate / rough_low))
        if needed > span:
            if needed <= len(tail):
                return band_edges(tail, sweep_lo, sweep_hi, cycles,
                                  needed / sample_rate, sample_rate, _pass=1)
            # The capture ends before the window can be sized. Refuse rather than
            # keep the first-pass reading: measured, a 50-71 Hz subwoofer cut to a
            # 0.15 s capture returns 40.7 Hz - 18272 Hz from that fallback, which
            # is the fiction this function exists to remove. A silent fallback is
            # how a fixed defect comes back. C++ reports Refusal::CaptureTooShort.
            return None

    # defect 3: interpolate outward, but never INTO a bin outside the clamp --
    # this file's own docstring says those bins are not evidence, and using one
    # as an interpolation endpoint uses it as evidence.
    in_band_bins = np.nonzero(inband)[0]
    first_in, last_in = int(in_band_bins[0]), int(in_band_bins[-1])
    lo_hz = (_interpolated_crossing(mag_db, low_bin, low_bin - 1, freqs, -10.0)
             if low_bin > first_in else float(freqs[low_bin]))
    hi_hz = (_interpolated_crossing(mag_db, high_bin, high_bin + 1, freqs, -10.0)
             if high_bin < last_in else float(freqs[high_bin]))
    return lo_hz, hi_hz


def _sos(family, order, lo, hi):
    if family == "butter":
        return scipy.signal.butter(order, [lo, hi], "bandpass", fs=FS, output="sos")
    if family == "bessel":
        return scipy.signal.bessel(order, [lo, hi], "bandpass", fs=FS,
                                   output="sos", norm="phase")
    return scipy.signal.ellip(order, 1.0, 60.0, [lo, hi], "bandpass", fs=FS,
                              output="sos")


def _legacy_edges(tail):
    """The estimator the earlier probes used, kept only so the fix can be shown
    to reduce scatter rather than asserted to."""
    span = min(int(0.05 * FS), len(tail))
    size = 1 << int(np.ceil(np.log2(span)))
    p = np.zeros(size)
    p[:span] = tail[:span]
    mag = np.abs(scipy.fft.rfft(p))
    freqs = np.arange(len(mag)) * FS / size
    if mag[1:].max() <= 0.0:
        return None
    hits = np.nonzero(mag[1:] >= mag.max() * MINUS_10_DB)[0] + 1
    return (float(freqs[hits[0]]), float(freqs[hits[-1]])) if hits.size else None


def main():
    print("Band-edge estimator: the three measured defects, and what fixing them")
    print("does to the scatter a threshold has to survive.\n")

    rng = np.random.default_rng(20260830)
    sweep, inverse, _ = sweep_and_inverse(2.0, 20.0, 20000.0, 2.0, 0.5)
    origin = len(inverse) - 1

    print("=" * 74)
    print("  DEFECT 1 -- the cell that put a subwoofer inside the gate")
    print("=" * 74)
    sos = scipy.signal.cheby1(12, 1.0, [50.0, 71.0], "bandpass", fs=FS, output="sos")
    tail = linear_convolve(scipy.signal.sosfilt(sos, sweep), inverse)[origin:]
    old, new = _legacy_edges(tail), band_edges(tail, 20.0, 20000.0)
    print(f"  design 50-71 Hz (0.5 octave, a subwoofer band)")
    print(f"    fixed 50 ms window : {old[0]:8.1f} - {old[1]:8.0f} Hz   <- fiction")
    print(f"    adaptive + clamped : {new[0]:8.1f} - {new[1]:8.0f} Hz")
    print("  The driven signal is -101 dB at 10-20 kHz; the first reading was not")
    print("  a wrong answer about a loudspeaker, it was not a measurement at all.")

    print("\n" + "=" * 74)
    print("  SCATTER for a system designed exactly on the threshold")
    print("=" * 74)
    print("  butter/bessel/ellip, orders 2-8, clean and at 30 and 20 dB SNR.")
    for design_lo, design_hi in ((100.0, 8000.0),):
        old_lo, new_lo, old_hi, new_hi = [], [], [], []
        for family in ("butter", "bessel", "ellip"):
            for order in (2, 4, 6, 8):
                for snr in (None, 30.0, 20.0):
                    driven = scipy.signal.sosfilt(
                        _sos(family, order, design_lo, design_hi), sweep)
                    if snr is not None:
                        driven = driven + rng.normal(size=len(driven)) * np.sqrt(
                            np.mean(driven ** 2)) * 10.0 ** (-snr / 20.0)
                    t = linear_convolve(driven, inverse)[origin:]
                    o, n = _legacy_edges(t), band_edges(t, 20.0, 20000.0)
                    if o is None or n is None:
                        continue
                    old_lo.append(o[0]); new_lo.append(n[0])
                    old_hi.append(o[1]); new_hi.append(n[1])
        for name, old_v, new_v, design in (("low", old_lo, new_lo, design_lo),
                                           ("high", old_hi, new_hi, design_hi)):
            o, n = np.array(old_v), np.array(new_v)
            print(f"\n  {name} edge, design {design:.0f} Hz  ({len(n)} measurements)")
            print(f"    before: {o.min():8.1f} - {o.max():8.1f} Hz"
                  f"   spread {np.log2(o.max()/o.min()):.3f} oct"
                  f"   median {np.median(o):8.1f}")
            print(f"    after : {n.min():8.1f} - {n.max():8.1f} Hz"
                  f"   spread {np.log2(n.max()/n.min()):.3f} oct"
                  f"   median {np.median(n):8.1f}")
            change = np.log2(n.max()/n.min()) - np.log2(o.max()/o.min())
            print(f"    -> scatter {'REDUCED' if change < 0 else 'NOT reduced'}"
                  f" by {abs(change):.3f} octave")

    print("\n  A threshold and its tolerance must be chosen against the AFTER row.")
    print("  Choosing against the BEFORE row calibrates on an estimator that no")
    print("  longer exists, which is exactly how the 2.5-octave gate was lost.")


if __name__ == "__main__":
    main()
