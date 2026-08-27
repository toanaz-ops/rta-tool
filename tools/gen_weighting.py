#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Golden vectors for rta_core's A/C weighting filters, from scipy.

    .venv/Scripts/python.exe tools/gen_weighting.py

Writes core/tests/golden/weighting.txt, which is committed.

Design chain (must match core/src/dsp/Weighting.cpp exactly -- this script IS
the reference the C++ is checked against, per
docs/plans/2026-08-27-weighting-meters-impl-plan.md section 8):

    z, p, k_unit = analog_zpk(type)                 # k_unit = 1.0
    k = 1.0 / abs(signal.freqs_zpk(z, p, k_unit, worN=[2*pi*1000])[1][0])  # normalise at 1 kHz, ANALOG
    zd, pd, kd = signal.bilinear_zpk(z, p, k, fs)
    sos = signal.zpk2sos(zd, pd, kd)                # 'nearest' pairing, ascending pole radius

This script never asks a scipy designer for the transfer-function polynomial
form, not even for a debug print. The filter-bank decision record
(docs/dsp/2026-08-27-filterbank.md) measured the direct-form polynomial of a
near-unit-circle design diverging to NaN in double precision while the
identical filter as SOS stays bounded -- a "sanity check" printed the ba way
would masquerade as a bug in rta_core.

Note for whoever compares C++ output to sosfilt() results here: sosfilt and
this project's Direct Form II Transposed cascade are the SAME filter but not
the SAME arithmetic order, so bit equality between them is not expected and
is not asserted anywhere -- only WithinAbs/WithinRel at the tolerances in the
implementation plan.
"""

from __future__ import annotations

import pathlib
import sys

import numpy as np
import scipy
from scipy import signal

OUT_DIR = pathlib.Path(__file__).resolve().parent.parent / "core" / "tests" / "golden"
NEWLINE = "\n"

# IEC 61672-1 Annex E pole frequencies, hertz, verbatim.
F1, F2, F3, F4 = 20.598997, 107.65265, 737.86223, 12194.217

THIRD_OCTAVE_N = list(range(-20, 14))  # n = -20..13, exact frequencies 1000*10**(0.1n)


def fmt(values) -> str:
    return " ".join(repr(float(v)) for v in values)


def analog_zpk(kind: str):
    """Poles/zeros on the s-plane, IEC 61672-1 Annex E, gain unity (k=1.0)."""
    w1, w2, w3, w4 = (2.0 * np.pi * f for f in (F1, F2, F3, F4))
    if kind == "A":
        zeros = np.array([0.0, 0.0, 0.0, 0.0])
        poles = np.array([-w4, -w4, -w1, -w1, -w2, -w3])
    elif kind == "C":
        zeros = np.array([0.0, 0.0])
        poles = np.array([-w4, -w4, -w1, -w1])
    else:
        raise ValueError(kind)
    return zeros, poles, 1.0


def design(kind: str, fs: float):
    """Analog zpk -> unity-at-1kHz normalisation -> bilinear -> SOS, in that
    exact order, matching the C++ digitisation chain.

    Evaluated with signal.freqs_zpk throughout, never through a coefficient
    polynomial: this project's own guard (core/tests/check_no_polynomial_form.cmake)
    bans materialising a transfer-function polynomial anywhere in the
    filter-design chain, in any file under core/ or tools/ -- freqs_zpk
    evaluates the frequency response directly from the zpk form, with no
    polynomial conversion in between.
    """
    z, p, k_unit = analog_zpk(kind)
    # Normalise so |H(j*2*pi*1000)| = 1 in the ANALOG domain -- this is what
    # leaves the small, sample-rate-dependent residual at 1 kHz in the
    # DIGITAL filter (see docs/dsp/2026-08-27-weighting-and-meters.md 11.5);
    # it is not a bug.
    k = 1.0 / abs(signal.freqs_zpk(z, p, k_unit, worN=[2.0 * np.pi * 1000.0])[1][0])
    zd, pd, kd = signal.bilinear_zpk(z, p, k, fs)
    sos = signal.zpk2sos(zd, pd, kd, pairing="nearest")
    return z, p, k, sos


def render_weighting_case(kind: str, fs: float) -> str:
    z, p, k, sos = design(kind, fs)
    freqs = np.array([1000.0 * 10.0 ** (0.1 * n) for n in THIRD_OCTAVE_N])

    analytic_db = 20.0 * np.log10(np.abs(signal.freqs_zpk(z, p, k, worN=2.0 * np.pi * freqs)[1]))

    omega = 2.0 * np.pi * freqs / fs
    digital_db = 20.0 * np.log10(np.abs(signal.sosfreqz(sos, worN=omega)[1]))

    return NEWLINE.join([
        f"case weighting_{kind}_{int(fs)}",
        f"fs {fmt([fs])}",
        f"sections {sos.shape[0]}",
        f"freq {fmt(freqs)}",
        f"analytic_db {fmt(analytic_db)}",
        f"digital_db {fmt(digital_db)}",
        f"sos_b0 {fmt(sos[:, 0])}",
        f"sos_b1 {fmt(sos[:, 1])}",
        f"sos_b2 {fmt(sos[:, 2])}",
        f"sos_a1 {fmt(sos[:, 4])}",
        f"sos_a2 {fmt(sos[:, 5])}",
        "end",
    ])


def render_z_case() -> str:
    freqs = np.array([1000.0 * 10.0 ** (0.1 * n) for n in THIRD_OCTAVE_N])
    return NEWLINE.join([
        "case weighting_Z",
        "sections 0",
        f"freq {fmt(freqs)}",
        f"analytic_db {fmt(np.zeros_like(freqs))}",
        "end",
    ])


def render_leq_case(kind: str) -> str:
    """End-to-end: filter 2 s of seeded noise at 48 kHz with the weighting
    SOS, and record the Leq/peak of both input and output so the C++ side can
    build its own Weighting + Leq and compare, per implementation plan 8.1.
    """
    fs = 48000.0
    _z, _p, _k, sos = design(kind, fs)

    rng = np.random.default_rng(seed=20260827)
    x = (0.25 * rng.standard_normal(int(fs) * 2)).astype(np.float32)
    y = signal.sosfilt(sos, x.astype(np.float64))

    leq_db = 10.0 * np.log10(np.mean(y ** 2))
    peak_db = 10.0 * np.log10(np.max(x.astype(np.float64) ** 2))
    weighted_peak_db = 10.0 * np.log10(np.max(y ** 2))

    return NEWLINE.join([
        f"case leq_weighted_noise_{kind}",
        f"size {x.size}",
        f"input {fmt(x.astype(np.float64))}",
        f"leq_db {fmt([leq_db])}",
        f"peak_db {fmt([peak_db])}",
        f"weighted_peak_db {fmt([weighted_peak_db])}",
        "end",
    ])


def main() -> int:
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    blocks = []
    for kind in ("A", "C"):
        for fs in (44100.0, 48000.0, 96000.0):
            blocks.append(render_weighting_case(kind, fs))
    blocks.append(render_z_case())
    for kind in ("A", "C"):
        blocks.append(render_leq_case(kind))

    header = [
        "# rta_core golden vectors -- A/C/Z weighting filters",
        f"# generated by tools/gen_weighting.py, scipy {scipy.__version__}",
        "# DO NOT EDIT BY HAND. Regenerate and review the diff.",
        "",
    ]
    path = OUT_DIR / "weighting.txt"
    path.write_text(NEWLINE.join(header) + NEWLINE.join(blocks) + NEWLINE, encoding="utf-8")

    print(f"wrote {path} -- {len(blocks)} cases, scipy {scipy.__version__}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
