#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Survey: does sqrt(ln m / M_in) predict PHAT's null-hypothesis peak?

Station 3's plan (docs/plans/2026-09-07-L7-delay-impl-plan.md, task E) and
the decision record (docs/dsp/2026-09-06-l7-auto-delay.md sec.4) derive the
null floor -- the expected peak of a GCC-PHAT correlation between two
UNCORRELATED channels -- as sqrt(ln m / M_in), where m is the padded linear-
correlation length and M_in the count of in-band FFT bins. core/tests/
test_delay_policy_trust.cpp already pins two fixed-seed slices of this in
C++ (full-band and one band-limited case, 40 trials each). This script is
the WIDER, independent survey the plan calls for: an separate NumPy PHAT
re-implementation (not a call into rta_core) checked against the SAME
closed form across more bandwidths and lengths than the committed C++
suite carries, per record sec.4/sec.12.4 and CLAUDE.md's "golden vector"
principle -- an independent implementation is what lets a disagreement mean
something.

This is a MEASUREMENT, not a golden generator (memory/a-gen-script-runs-
the-moment-you-invoke-it.md): it writes nothing, prints a decision table.

Run with the main-checkout venv:

    D:/DEV CAVE EP3/PRJ010-RTA-TOOL/.venv/Scripts/python.exe tools/probe_delay_nullfloor.py

--help exits before any measurement (real argparse, unlike the older
tools/gen_*.py scripts -- see the memory file above).
"""

from __future__ import annotations

import argparse

import numpy as np


def phat_correlate(x: np.ndarray, y: np.ndarray, sample_rate: float, min_hz: float,
                    max_hz: float | None) -> tuple[np.ndarray, int, int]:
    """Linear (zero-padded), band-masked GCC-PHAT correlation -- the same
    shape as core/src/dsp/PhatCorrelation.h's computePhatCorrelation, an
    independent re-implementation rather than a call into it."""
    n = len(x)
    m = 4
    while m < 2 * n:
        m *= 2
    x_pad = np.zeros(m)
    y_pad = np.zeros(m)
    x_pad[:n] = x
    y_pad[:n] = y

    xf = np.fft.rfft(x_pad)
    yf = np.fft.rfft(y_pad)
    freqs = np.fft.rfftfreq(m, d=1.0 / sample_rate)
    hi = max_hz if max_hz is not None else sample_rate / 2.0
    in_band = (freqs >= min_hz) & (freqs <= hi)
    m_in = int(np.count_nonzero(in_band))

    g = np.where(in_band, np.conj(xf) * yf, 0.0)
    mag_g = np.abs(g)
    max_abs_g = float(mag_g.max()) if mag_g.size else 0.0
    floor = 1e-10 * max_abs_g

    weighted = np.zeros_like(g)
    nonzero = in_band & (mag_g > 0.0)
    weighted[nonzero] = g[nonzero] / np.maximum(mag_g[nonzero], floor)

    corr = np.fft.irfft(weighted, m)
    return corr, m, m_in


def predicted_floor(m: int, m_in: int) -> float:
    return float(np.sqrt(np.log(m) / m_in))


def null_trial(rng: np.random.Generator, length: int, min_hz: float,
                max_hz: float | None, sample_rate: float) -> tuple[float, int, int]:
    x = rng.standard_normal(length)
    y = rng.standard_normal(length)  # independent -> the null hypothesis itself
    corr, m, m_in = phat_correlate(x, y, sample_rate, min_hz, max_hz)
    raw_peak = float(np.max(np.abs(corr)))
    # The prediction sqrt(ln m / M_in) is derived on the TRUST scale (record
    # sec.4's "normalised" step divides by f_band), not the raw peak: a
    # perfect band-limited match already reads raw peak ~= f_band (the
    # existing C++ band-limit fixture pins this), so the null floor
    # comparison must undo the same normalisation or a narrow band looks
    # like it violates the derivation when it does not.
    hi = max_hz if max_hz is not None else sample_rate / 2.0
    f_band = (hi - min_hz) / (sample_rate / 2.0)
    trust = raw_peak / f_band if f_band > 0 else 0.0
    return trust, m, m_in


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--trials", type=int, default=40, help="trials per slice")
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("--tolerance", type=float, default=0.30,
                         help="allowed relative deviation of the mean from the prediction")
    args = parser.parse_args()

    sample_rate = 48000.0
    # Four axes worth of slices, per record sec.4: length (equivalently m),
    # and band limit -- the tracker's own SNR/D-L-overlap/excess-phase axes
    # are covered by probe_delay_trust.py, which needs a REAL delay to be
    # meaningful; the null floor itself is measured on uncorrelated pairs.
    slices = [
        ("full-band, L=8192", 8192, 0.0, None),
        ("full-band, L=16384", 16384, 0.0, None),
        ("full-band, L=32768", 32768, 0.0, None),
        ("band 200-4000 Hz, L=8192", 8192, 200.0, 4000.0),
        ("band 60-960 Hz, L=8192", 8192, 60.0, 960.0),
        ("band 1000-16000 Hz, L=16384", 16384, 1000.0, 16000.0),
    ]

    rng = np.random.default_rng(args.seed)
    header = (f"{'slice':28s} {'m':>8s} {'M_in':>8s} {'predicted':>10s} "
              f"{'mean':>10s} {'worst':>10s} {'mean/pred':>10s} {'worst/pred':>11s}  ok?")
    print(header)
    print("-" * len(header))

    all_ok = True
    for name, length, lo, hi in slices:
        peaks = []
        m = m_in = 0
        for _ in range(args.trials):
            peak, m, m_in = null_trial(rng, length, lo, hi, sample_rate)
            peaks.append(peak)
        predicted = predicted_floor(m, m_in)
        mean = float(np.mean(peaks))
        worst = float(np.max(peaks))
        within = abs(mean / predicted - 1.0) <= args.tolerance
        all_ok = all_ok and within
        print(f"{name:28s} {m:8d} {m_in:8d} {predicted:10.4f} {mean:10.4f} {worst:10.4f} "
              f"{mean / predicted:10.3f} {worst / predicted:11.3f}  {'PASS' if within else 'FAIL'}")

    print()
    print(f"check 1 (mean within {args.tolerance:.0%} of sqrt(ln m / M_in), every slice): "
          f"{'PASS' if all_ok else 'FAIL'}")
    return 0 if all_ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
