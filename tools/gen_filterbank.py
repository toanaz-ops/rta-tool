#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Golden vectors for rta_core's IEC 61260 filter bank, from scipy.

    .venv/Scripts/python.exe tools/gen_filterbank.py

Writes core/tests/golden/filterbank.txt, which is committed.

This script must never call scipy's Butterworth designer with the ba
(transfer-function polynomial) output form, and no debugging line added to it
later may either. For the low bands that output is not a less convenient
representation of the same filter, it is a different and unstable one: at
15.85 Hz / 48 kHz its denominator has a root at |root| = 1.0864 and its
impulse response reaches 7.4e+130, while the identical design taken as SOS
peaks at 2.0e-06. A number produced the ba way will masquerade as a bug in
rta_core. See docs/dsp/2026-08-27-filterbank.md.

The Nyquist-edge clamp (docs/plans/2026-08-27-filterbank-impl-plan.md section
2) is applied here, before the design call, exactly as ButterworthDesign::
bandPass applies it -- otherwise this script and the C++ side describe two
different filters for the one band that needs it (19952.6 Hz at 44.1 kHz).
"""

from __future__ import annotations

import math
import pathlib
import sys

import numpy as np
import scipy
from scipy import signal

OUT_DIR = pathlib.Path(__file__).resolve().parent.parent / "core" / "tests" / "golden"
NEWLINE = "\n"

REFERENCE_HZ = 1000.0
RATIO = 10.0 ** (3.0 / 10.0)  # IEC 61260-1:2014 base-ten G
NYQUIST_EDGE_FRACTION = 0.995  # kept in lockstep with ButterworthDesign::kNyquistEdgeFraction


def fmt(values) -> str:
    return " ".join(repr(float(v)) for v in values)


def iec_band(index: int, fraction: int = 3) -> tuple[float, float, float]:
    """centre, lower, upper -- the same formula as OctaveBands.cpp."""
    half = RATIO ** (1.0 / (2.0 * fraction))
    centre = REFERENCE_HZ * RATIO ** (index / fraction)
    return centre, centre / half, centre * half


def octave_band_indices(fraction: int, lowest: float, highest: float) -> list[int]:
    """Reproduces OctaveBands::OctaveBands's index selection exactly, including
    its epsilon, so this script and the C++ bank agree on which indices exist.
    """
    ln_g = math.log(RATIO)
    low_index = fraction * math.log(lowest / REFERENCE_HZ) / ln_g
    high_index = fraction * math.log(highest / REFERENCE_HZ) / ln_g
    eps = 1.0e-9
    first = math.ceil(low_index - eps)
    last = math.floor(high_index + eps)
    return list(range(first, last + 1))


def clamp_upper(upper: float, fs: float) -> tuple[float, bool]:
    limit = NYQUIST_EDGE_FRACTION * (fs / 2.0)
    if upper > limit:
        return limit, True
    return upper, False


def design(lower: float, upper: float, fs: float, sections: int):
    """zpk (never ba) straight from scipy's Butterworth designer, plus the SOS
    pairing that is the whole point of these fixtures.
    """
    z, p, k = signal.butter(sections, [lower, upper], btype="bandpass", fs=fs, output="zpk")
    sos = signal.zpk2sos(z, p, k, pairing="nearest")
    return z, p, k, sos


def render_design(name: str, index: int, fs: float, sections: int = 6) -> str:
    centre, lower, upper = iec_band(index)
    clamped_upper, clamped = clamp_upper(upper, fs)
    z, p, k, sos = design(lower, clamped_upper, fs, sections)

    return NEWLINE.join([
        f"case {name}",
        f"fs {fmt([fs])}",
        f"sections {sections}",
        f"centre {fmt([centre])}",
        f"lower {fmt([lower])}",
        f"upper {fmt([clamped_upper])}",
        f"clamped {1 if clamped else 0}",
        f"zpk_gain {fmt([k])}",
        f"pole_re {fmt(p.real)}",
        f"pole_im {fmt(p.imag)}",
        f"zero_re {fmt(z.real)}",
        f"zero_im {fmt(z.imag)}",
        f"sos {fmt(sos.flatten())}",
        "end",
    ])


def render_impulse(name: str, index: int, fs: float, sections: int = 6,
                    length: int = 2048) -> str:
    centre, lower, upper = iec_band(index)
    clamped_upper, _clamped = clamp_upper(upper, fs)
    _z, _p, _k, sos = design(lower, clamped_upper, fs, sections)

    impulse = np.zeros(length, dtype=np.float64)
    impulse[0] = 1.0
    response = signal.sosfilt(sos, impulse)

    return NEWLINE.join([
        f"case {name}",
        f"fs {fmt([fs])}",
        f"impulse {fmt(response)}",
        "end",
    ])


def render_bank_response(name: str, fs: float, samples: np.ndarray, fraction: int = 3,
                          lowest: float = 20.0, highest: float = 20000.0,
                          sections: int = 6) -> str:
    """One case per band of the whole bank, mirroring FilterBank's own
    construction: a band whose CENTRE reaches the Nyquist-edge fraction is not
    built at all; a band whose upper edge alone reaches it is clamped.
    """
    x = samples.astype(np.float64)
    edge_limit = NYQUIST_EDGE_FRACTION * (fs / 2.0)

    indices, centres, band_powers = [], [], []
    for index in octave_band_indices(fraction, lowest, highest):
        centre, lower, upper = iec_band(index, fraction)
        if centre >= edge_limit:
            continue
        clamped_upper, _clamped = clamp_upper(upper, fs)
        _z, _p, _k, sos = design(lower, clamped_upper, fs, sections)
        y = signal.sosfilt(sos, x)

        indices.append(index)
        centres.append(centre)
        band_powers.append(float(np.mean(y * y)))

    return NEWLINE.join([
        f"case {name}",
        f"fs {fmt([fs])}",
        f"input {fmt(samples)}",
        f"index {' '.join(str(i) for i in indices)}",
        f"centre {fmt(centres)}",
        f"band_power {fmt(band_powers)}",
        "end",
    ])


def main() -> int:
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    blocks = [
        render_design("design_fs48000_idx-17", -17, 48000.0),
        render_design("design_fs48000_idx0", 0, 48000.0),
        render_design("design_fs48000_idx13", 13, 48000.0),
        render_design("design_fs44100_idx13", 13, 44100.0),
        render_impulse("impulse_fs48000_idx-17", -17, 48000.0),
        render_impulse("impulse_fs48000_idx0", 0, 48000.0),
    ]

    n = 8192
    rng = np.random.default_rng(20260827)
    noise = rng.standard_normal(n).astype(np.float32)
    blocks.append(render_bank_response("noise_fs48000_frac3", 48000.0, noise))

    t = np.arange(n)
    tone = np.sin(2.0 * np.pi * 1000.0 * t / 48000.0).astype(np.float32)
    blocks.append(render_bank_response("tone_fs48000_idx0", 48000.0, tone))

    header = [
        "# rta_core golden vectors -- IEC 61260 filter bank",
        f"# generated by tools/gen_filterbank.py, scipy {scipy.__version__}",
        "# DO NOT EDIT BY HAND. Regenerate and review the diff.",
        "",
    ]
    path = OUT_DIR / "filterbank.txt"
    path.write_text(NEWLINE.join(header) + NEWLINE.join(blocks) + NEWLINE, encoding="utf-8")

    print(f"wrote {path} -- {len(blocks)} cases")
    return 0


if __name__ == "__main__":
    sys.exit(main())
