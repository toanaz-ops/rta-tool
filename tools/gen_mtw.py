#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Golden vectors for rta_core's MtwLayout / MtwEngine, from
scipy.signal.csd/welch/coherence -- one band at a time.

    .venv/Scripts/python.exe tools/gen_mtw.py --out core/tests/golden/mtw.txt

Writes two cases (default: core/tests/golden/mtw.txt, committed): the DEFAULT
band table (N0=1024, K=6, layout only -- its bottom band is 65536 points and
would need ~330k committed samples to reach its own 16 frames), and a SCALED,
fully-stitched table (N0=512, K=3, 449 points) run through scipy.

memory/a-gen-script-runs-the-moment-you-invoke-it.md: a gen_*.py script is a
write to the repository the instant Python executes it, not a CLI to poke at
-- so this one uses argparse for real, and `--help` exits before any write.

Plan task 4, "the generator matches the FIFO's window, not the whole file":
fifoDepth is capped at 32 and octave-doubling spreads frame counts 16..149
across the four scaled bands, so no legal depth makes scipy's plain mean
equal the FIFO's for every band at once. Each band is analysed on its own
TAIL slice instead -- the last N_k + 15*hop_k samples, starting at a
multiple of hop_k -- reproducing exactly the 16 frames the FIFO holds.
"""

from __future__ import annotations

import argparse
import pathlib
import sys
import tempfile

import numpy as np
from scipy import signal

FS = 48000.0
NEWLINE = "\n"


def fmt(values) -> str:
    return " ".join(repr(float(v)) for v in values)


def mtw_bands(n0: int, k: int, fs: float, fifo_depth: int = 16):
    """Mirrors core/src/dsp/MtwLayout.cpp's mtwBands() (ascending-frequency
    order, index 0 = bottom / largest FFT) -- a second, independent author
    for the same closed forms."""
    band_count = k + 1
    bands = []
    running = 0
    for i in range(band_count):
        fft_size = n0 << (k - i)
        hop = fft_size // 4
        is_bottom, is_top = i == 0, i == band_count - 1
        first_bin = 0 if is_bottom else n0 // 8
        last_bin = (n0 // 2) if is_top else (n0 // 4 - 1)
        first_index = running
        running += last_bin - first_bin + 1
        bands.append(dict(fft_size=fft_size, hop=hop, first_bin=first_bin,
                           last_bin=last_bin, first_index=first_index,
                           integration_seconds=fifo_depth * hop / fs))
    return bands


def mtw_frequencies(bands, fs: float):
    return [bin_ * fs / b["fft_size"]
            for b in bands for bin_ in range(b["first_bin"], b["last_bin"] + 1)]


def render_layout(name: str, n0: int, k: int, fs: float) -> str:
    bands = mtw_bands(n0, k, fs)
    freq = mtw_frequencies(bands, fs)
    return NEWLINE.join([
        f"case {name}",
        f"n0 {n0}",
        f"octaves {k}",
        f"point_count {len(freq)}",
        "first_index " + " ".join(str(b["first_index"]) for b in bands),
        "fft_size " + " ".join(str(b["fft_size"]) for b in bands),
        "first_bin " + " ".join(str(b["first_bin"]) for b in bands),
        "last_bin " + " ".join(str(b["last_bin"]) for b in bands),
        "integration_seconds " + fmt(b["integration_seconds"] for b in bands),
        f"freq {fmt(freq)}",
        "end",
    ])


def render_stitched(name: str, n0: int, k: int, fs: float, x: np.ndarray, y: np.ndarray) -> str:
    x64, y64 = x.astype(np.float64), y.astype(np.float64)
    fifo_depth = 16
    bands = mtw_bands(n0, k, fs, fifo_depth)
    n = x.size

    freq_all, pxx_all, pyy_all = [], [], []
    pxy_real_all, pxy_imag_all, coherence_all = [], [], []

    for b in bands:
        nk, hop = b["fft_size"], b["hop"]
        # frames(n) = 1 + (n-N)/hop for n >= N; the tail with the FIFO's last
        # `fifo_depth` frames starts that many hops before the last one.
        frames = 1 + (n - nk) // hop
        xs = x64[(frames - fifo_depth) * hop:]
        ys = y64[(frames - fifo_depth) * hop:]

        kwargs = dict(fs=fs, window="hann", nperseg=nk, noverlap=nk - hop,
                      detrend=False, return_onesided=True, average="mean")
        freqs, pxy = signal.csd(xs, ys, **kwargs)
        _, pxx = signal.welch(xs, **kwargs)
        _, pyy = signal.welch(ys, **kwargs)

        lo, hi = b["first_bin"], b["last_bin"] + 1
        freq_all.extend(freqs[lo:hi])
        pxx_all.extend(pxx[lo:hi])
        pyy_all.extend(pyy[lo:hi])
        pxy_real_all.extend(pxy[lo:hi].real)
        pxy_imag_all.extend(pxy[lo:hi].imag)
        # From the SAME three sliced rows, never a fourth scipy call, so the
        # golden cannot disagree with itself.
        for i in range(lo, hi):
            coherence_all.append(float(abs(pxy[i]) ** 2 / (pxx[i] * pyy[i])))

    return NEWLINE.join([
        f"case {name}",
        f"n0 {n0}",
        f"octaves {k}",
        f"fifo_depth {fifo_depth}",
        f"size {n}",
        f"input_x {fmt(x)}",
        f"input_y {fmt(y)}",
        f"freq {fmt(freq_all)}",
        f"pxx {fmt(pxx_all)}",
        f"pyy {fmt(pyy_all)}",
        f"pxy_real {fmt(pxy_real_all)}",
        f"pxy_imag {fmt(pxy_imag_all)}",
        f"coherence {fmt(coherence_all)}",
        "end",
    ])


def build(out_path: pathlib.Path) -> str:
    blocks = [render_layout("mtw_layout_default", 1024, 6, FS)]

    # N0=512, K=3, n=19456=4096+15*1024 (task 4): smallest length giving the
    # BOTTOM band its 16 frames, n-N_k an exact multiple of hop_k in all four
    # bands. Biquad via sosfilt, never a ba-form filter (check_no_polynomial_
    # form.cmake) -- same system tools/gen_transfer.py uses.
    n = 19456
    rng = np.random.default_rng(seed=613)
    x = rng.standard_normal(n).astype(np.float32)
    sos = np.array([[0.3, -0.2, 0.1, 1.0, -0.5, 0.2]])
    y = signal.sosfilt(sos, x.astype(np.float64)).astype(np.float32)
    blocks.append(render_stitched("mtw_stitched_512_3", 512, 3, FS, x, y))

    header = [
        "# rta_core golden vectors -- multi-time-window transfer function",
        "# generated by tools/gen_mtw.py from scipy.signal.csd/welch, one band at a time",
        "# DO NOT EDIT BY HAND. Regenerate and review the diff.",
        "",
    ]
    text = NEWLINE.join(header) + NEWLINE.join(blocks) + NEWLINE
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(text, encoding="utf-8")
    return text


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    default_out = pathlib.Path(__file__).resolve().parent.parent / "core" / "tests" / "golden" / "mtw.txt"
    parser.add_argument("--out", type=pathlib.Path, default=default_out,
                         help="output path for the golden file")
    parser.add_argument("--check", action="store_true",
                         help="diff a fresh regeneration against --out instead of writing it")
    args = parser.parse_args()

    if args.check:
        with tempfile.TemporaryDirectory() as tmp:
            text = build(pathlib.Path(tmp) / "mtw.txt")
            existing = args.out.read_text(encoding="utf-8") if args.out.exists() else None
            if existing == text:
                print(f"{args.out} is byte-identical to a fresh regeneration")
                return 0
            print(f"{args.out} DIFFERS from a fresh regeneration", file=sys.stderr)
            return 1

    build(args.out)
    print(f"wrote {args.out} -- scipy {signal.__name__}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
