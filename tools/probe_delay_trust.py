#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Survey: does c=4 separate every trustworthy answer from every wrong one?

Station 3's plan (docs/plans/2026-09-07-L7-delay-impl-plan.md, task E) and
the decision record (docs/dsp/2026-09-06-l7-auto-delay.md sec.4) propose
`accept iff trust >= c * sqrt(ln m / M_in)` with c = 4 as a starting value,
to be confirmed or moved by "one mechanism along four axes -- SNR, D/L
overlap, band limit, excess-phase order". This script is that survey, run
against an INDEPENDENT NumPy PHAT implementation (not a call into rta_core),
per CLAUDE.md's golden-vector principle: an independent implementation is
what lets a disagreement mean something.

Filters use scipy.signal SOS forms only (sosfilt, butter(..., output='sos'),
and a hand-built RBJ-cookbook allpass SOS row) -- never the transposed-
direct-form-II filtering call taking a (b, a) coefficient pair:
core/tests/check_no_polynomial_form.cmake scans tools/*.py for exactly that
name (docs/dsp/2026-08-27-filterbank.md's numerically unstable ba form), and
this script is inside its scan -- even in a comment, so this file spells
neither that call nor the frequency-response one sharing its `(b` argument
shape.

This is a MEASUREMENT, not a golden generator (memory/a-gen-script-runs-
the-moment-you-invoke-it.md): it writes nothing, prints decision tables.

Run with the main-checkout venv:

    D:/DEV CAVE EP3/PRJ010-RTA-TOOL/.venv/Scripts/python.exe tools/probe_delay_trust.py

--help exits before any measurement.
"""

from __future__ import annotations

import argparse

import numpy as np
from scipy import signal


def phat_correlate(x: np.ndarray, y: np.ndarray, sample_rate: float, min_hz: float,
                    max_hz: float | None) -> tuple[np.ndarray, int, int]:
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


def best_lag(corr: np.ndarray, m: int) -> tuple[int, float]:
    idx = int(np.argmax(np.abs(corr)))
    lag = idx if idx < m // 2 else idx - m
    return lag, float(corr[idx])


def trust_and_floor(x: np.ndarray, y: np.ndarray, sample_rate: float, min_hz: float,
                     max_hz: float | None) -> tuple[int, float, float]:
    corr, m, m_in = phat_correlate(x, y, sample_rate, min_hz, max_hz)
    lag, height = best_lag(corr, m)
    hi = max_hz if max_hz is not None else sample_rate / 2.0
    f_band = (hi - min_hz) / (sample_rate / 2.0)
    trust = abs(height) / f_band if f_band > 0 else 0.0
    null_floor = float(np.sqrt(np.log(m) / m_in)) if m_in > 0 else 1.0
    return lag, trust, null_floor


def rbj_allpass_sos(fc_hz: float, q: float, fs: float) -> np.ndarray:
    """RBJ Audio EQ Cookbook allpass, as one direct-form SOS row -- no (b, a)
    polynomial helper or filtering call (see module docstring)."""
    w0 = 2.0 * np.pi * fc_hz / fs
    alpha = np.sin(w0) / (2.0 * q)
    cw = np.cos(w0)
    a0 = 1.0 + alpha
    return np.array([[(1.0 - alpha) / a0, -2.0 * cw / a0, (1.0 + alpha) / a0,
                       1.0, -2.0 * cw / a0, (1.0 - alpha) / a0]])


def lr4_sos(fc_hz: float, fs: float, btype: str) -> np.ndarray:
    """Two cascaded Butterworth-Q 2nd-order sections == one 4th-order
    Linkwitz-Riley section, built entirely through scipy's SOS output."""
    sos2 = signal.butter(2, fc_hz, btype=btype, fs=fs, output="sos")
    return np.vstack([sos2, sos2])


def make_pair(rng: np.random.Generator, length: int, delay: int, snr_db: float | None,
              filter_kind: str | None, fs: float) -> tuple[np.ndarray, np.ndarray]:
    x = rng.standard_normal(length)
    source = x
    if filter_kind == "box":
        sos = signal.butter(4, [60.0, 960.0], btype="bandpass", fs=fs, output="sos")
        source = signal.sosfilt(sos, x)
    elif filter_kind == "allpass":
        source = signal.sosfilt(rbj_allpass_sos(1000.0, 0.7, fs), x)
    elif filter_kind == "lr4":
        lp = signal.sosfilt(lr4_sos(1000.0, fs, "low"), x)
        hp = signal.sosfilt(lr4_sos(1000.0, fs, "high"), x)
        source = lp + hp

    y = np.zeros(length)
    if delay < length:
        y[delay:] = source[: length - delay]
    if snr_db is not None:
        noise = rng.standard_normal(length)
        y = y + (10.0 ** (-snr_db / 20.0)) * noise
    return x, y


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--trials", type=int, default=10, help="trials per grid point")
    parser.add_argument("--seed", type=int, default=11)
    parser.add_argument("--c", type=float, default=4.0, help="acceptMultiple to test")
    parser.add_argument("--tolerance", type=int, default=30,
                         help="lag tolerance, samples. Wide enough to absorb a real "
                              "filter's own group delay (axis 3's narrowband box measured "
                              "here at +21 samples, a genuine latency -- record sec.7 -- "
                              "not an estimator error) without absorbing an actually wrong "
                              "answer, which axis 1's SNR sweep shows landing far outside "
                              "this margin whenever it happens at all")
    args = parser.parse_args()

    fs = 48000.0
    rng = np.random.default_rng(args.seed)
    wrong_above_gate = 0
    total_above_gate = 0

    print(f"acceptMultiple c = {args.c}\n")

    # --- Axis 1: SNR, full band, low D/L overlap ---------------------------
    print("axis 1: SNR (L=32768, D=300, full band)")
    print(f"{'SNR dB':>8s} {'wrong/N':>10s} {'trust mean':>12s} {'trust/floor':>12s}")
    for snr_db in (0.0, -6.0, -12.0, -18.0, -24.0, -27.0, -30.0, -33.0):
        wrong = 0
        trusts = []
        ratios = []
        for _ in range(args.trials):
            x, y = make_pair(rng, 32768, 300, snr_db, None, fs)
            lag, trust, floor = trust_and_floor(x, y, fs, 0.0, None)
            trusts.append(trust)
            ratios.append(trust / floor)
            above_gate = trust >= args.c * floor
            if above_gate:
                total_above_gate += 1
                if lag != 300:
                    wrong += 1
                    wrong_above_gate += 1
        print(f"{snr_db:8.1f} {wrong:4d}/{args.trials:<5d} {np.mean(trusts):12.4f} "
              f"{np.mean(ratios):12.2f}")

    # --- Axis 2: D/L overlap, fixed SNR, full band --------------------------
    print("\naxis 2: D/L overlap (L=16384, SNR=-6 dB, full band)")
    print(f"{'D/L':>6s} {'wrong/N':>10s} {'trust mean':>12s}")
    length = 16384
    for ratio in (0.06, 0.25, 0.50, 0.75):
        delay = int(round(length * ratio))
        wrong = 0
        trusts = []
        for _ in range(args.trials):
            x, y = make_pair(rng, length, delay, -6.0, None, fs)
            lag, trust, floor = trust_and_floor(x, y, fs, 0.0, None)
            trusts.append(trust)
            if trust >= args.c * floor:
                total_above_gate += 1
                if lag != delay:
                    wrong += 1
                    wrong_above_gate += 1
        print(f"{ratio:6.2f} {wrong:4d}/{args.trials:<5d} {np.mean(trusts):12.4f}")

    # --- Axis 3: band limit rescues a narrowband box ------------------------
    print("\naxis 3: band limit (L=16384, D=300, no noise, a 60-960 Hz box)")
    for label, min_hz, max_hz in (("box's own band (60-960)", 60.0, 960.0),
                                   ("full band (no limit)", 0.0, None)):
        wrong = 0
        trusts = []
        for _ in range(args.trials):
            x, y = make_pair(rng, length, 300, None, "box", fs)
            lag, trust, floor = trust_and_floor(x, y, fs, min_hz, max_hz)
            trusts.append(trust)
            if trust >= args.c * floor:
                total_above_gate += 1
                if abs(lag - 300) > args.tolerance:
                    wrong += 1
                    wrong_above_gate += 1
        print(f"  {label:28s} wrong {wrong}/{args.trials}  trust mean {np.mean(trusts):.4f}")

    # --- Axis 4: excess phase does not move the verdict ---------------------
    print("\naxis 4: excess phase (L=16384, D=300, no noise, full band)")
    for label, kind in (("2nd-order allpass, 1 kHz Q0.7", "allpass"),
                         ("LR4 crossover sum, 1 kHz", "lr4")):
        wrong = 0
        trusts = []
        for _ in range(args.trials):
            x, y = make_pair(rng, length, 300, None, kind, fs)
            lag, trust, floor = trust_and_floor(x, y, fs, 0.0, None)
            trusts.append(trust)
            if trust >= args.c * floor:
                total_above_gate += 1
                if abs(lag - 300) > args.tolerance:
                    wrong += 1
                    wrong_above_gate += 1
        print(f"  {label:28s} wrong {wrong}/{args.trials}  trust mean {np.mean(trusts):.4f}")

    print(f"\ncheck 2 (zero wrong answers above c*floor, c={args.c}): "
          f"{wrong_above_gate} wrong out of {total_above_gate} accepted "
          f"({'PASS' if wrong_above_gate == 0 else 'FAIL'})")
    return 0 if wrong_above_gate == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
