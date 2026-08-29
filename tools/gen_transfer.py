#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Golden vectors for rta_core's DualFftEngine / TransferEstimator, from
scipy.signal.csd, welch and coherence.

    .venv/Scripts/python.exe tools/gen_transfer.py

Writes core/tests/golden/transfer.txt, which is committed.

Two conventions make these vectors line up with the C++ side with no
conjugation or fudge factor anywhere -- see docs/dsp/2026-08-28-dual-fft.md §2
and docs/plans/2026-08-29-L2-dual-fft-impl-plan.md's naming table:

  1. scipy's `_spectral_helper` computes `conjugate(X) * Y` for the
     cross-spectrum, the same convention DualFftEngine's Sxy uses. So
     `signal.csd(reference, measurement)` lines up with `crossPsd()` directly.
  2. scipy's default `average="mean"` is a plain mean over segments. Our FIFO
     equals that plain mean only once `fifoDepth` exceeds the frame count,
     which is how the C++ side (test_transfer_golden.cpp) sets it up.

`signal.coherence` does not accept `scaling`, `return_onesided` or `average`
kwargs the way `csd`/`welch` do -- it always computes a one-sided,
mean-averaged, magnitude-SQUARED estimate, so it gets its own smaller kwargs
dict below rather than sharing the csd/welch one.

Modelled line for line on tools/gen_welch.py: same `fmt`, same header block,
same float32-generated-widened-to-float64 rule so the C++ side reads
bit-identical samples (see memory/float32-fft-precision.md).
"""

from __future__ import annotations

import pathlib
import sys

import numpy as np
from scipy import signal

OUT_DIR = pathlib.Path(__file__).resolve().parent.parent / "core" / "tests" / "golden"
NEWLINE = "\n"
FS = 48000.0
NPERSEG = 512
NOVERLAP = 256


def fmt(values) -> str:
    return " ".join(repr(float(v)) for v in values)


def render(name: str, x: np.ndarray, y: np.ndarray) -> str:
    x64 = x.astype(np.float64)
    y64 = y.astype(np.float64)

    kwargs = dict(fs=FS, window="hann", nperseg=NPERSEG, noverlap=NOVERLAP,
                  detrend=False, return_onesided=True, average="mean")
    freqs, pxy = signal.csd(x64, y64, **kwargs)      # conj(X) * Y, same as ours
    _,     pxx = signal.welch(x64, **kwargs)
    _,     pyy = signal.welch(y64, **kwargs)

    coherence_kwargs = dict(fs=FS, window="hann", nperseg=NPERSEG, noverlap=NOVERLAP,
                             detrend=False)
    _, cxy = signal.coherence(x64, y64, **coherence_kwargs)  # magnitude-SQUARED

    return NEWLINE.join([
        f"case {name}",
        f"size {x.size}",
        f"nperseg {NPERSEG}",
        f"noverlap {NOVERLAP}",
        f"input_x {fmt(x)}",
        f"input_y {fmt(y)}",
        f"freq {fmt(freqs)}",
        f"pxx {fmt(pxx)}",
        f"pyy {fmt(pyy)}",
        f"pxy_real {fmt(pxy.real)}",
        f"pxy_imag {fmt(pxy.imag)}",
        f"coherence {fmt(cxy)}",
        "end",
    ])


def main() -> int:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    blocks = []
    n = 8192  # 31 segments at nperseg=512, noverlap=256 -- enough for a stable mean

    # A pure delay and gain: y[n] = 0.5 * x[n-16]. The leading 16 samples of y
    # have no defined source and are left at zero, same as a real delay line
    # powering on; 16 samples out of 8192 do not move a Welch-averaged PSD.
    rng = np.random.default_rng(seed=101)
    x_delayed = rng.standard_normal(n).astype(np.float32)
    y_delayed = np.zeros_like(x_delayed)
    y_delayed[16:] = 0.5 * x_delayed[:-16]
    blocks.append(render("delayed", x_delayed, y_delayed))

    # Independent noise added on the MEASUREMENT channel -- the case this
    # project's default H1 estimator is unbiased for (record §1): a clean
    # electrical reference tap and a microphone in a room full of energy that
    # has nothing to do with the source.
    x_noisy = np.random.default_rng(seed=103).standard_normal(n).astype(np.float32)
    noise = np.random.default_rng(seed=107).standard_normal(n).astype(np.float32)
    y_noisy = (x_noisy + 0.5 * noise).astype(np.float32)
    blocks.append(render("noisy", x_noisy, y_noisy))

    # A known IIR filter: the frequency-dependent case flat gain-and-delay
    # cannot exercise -- H must trace the filter's own transfer function.
    #
    # This script must never apply the coefficient-vector filtering call scipy
    # offers as a shortcut, the same restriction gen_filterbank.py documents,
    # because this project's own ba-polynomial filter design is numerically
    # unstable for the lowest third-octave band (docs/dsp/2026-08-27-
    # filterbank.md) -- see that file for why the coefficient-vector form is
    # never a convenience here, and core/tests/check_no_polynomial_form.cmake
    # for the build guard that enforces it across this whole tree. So the
    # single section below is applied through the cascaded-sections call
    # instead. The row, `[b0, b1, b2, a0, a1, a2]`, is exactly
    # `rta::dsp::Biquad::Coeffs{b0=0.3, b1=-0.2, b2=0.1, a1=-0.5, a2=0.2}` on
    # the C++ side -- one difference on paper: this row carries an explicit
    # a0 = 1.0 that Biquad::Coeffs does not store, since it is always
    # normalised to 1 by construction there.
    x_filtered = np.random.default_rng(seed=109).standard_normal(n).astype(np.float32)
    sos = np.array([[0.3, -0.2, 0.1, 1.0, -0.5, 0.2]])
    y_filtered = signal.sosfilt(sos, x_filtered.astype(np.float64)).astype(np.float32)
    blocks.append(render("filtered", x_filtered, y_filtered))

    header = [
        "# rta_core golden vectors -- dual-FFT transfer function",
        "# generated by tools/gen_transfer.py from scipy.signal.csd/welch/coherence",
        "# DO NOT EDIT BY HAND. Regenerate and review the diff.",
        "",
    ]
    path = OUT_DIR / "transfer.txt"
    path.write_text(NEWLINE.join(header) + NEWLINE.join(blocks) + NEWLINE, encoding="utf-8")
    print(f"wrote {path} -- {len(blocks)} cases, scipy {signal.__name__}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
