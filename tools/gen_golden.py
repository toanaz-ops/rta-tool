#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Generate golden vectors for rta_core from NumPy.

Why this exists
---------------
An assertion that a DSP routine returns what it returned last time is a
regression lock, not a correctness test: it freezes whatever the code does,
including whatever it does wrong. These vectors come from an independent,
widely-checked implementation, so a disagreement means one of the two is wrong
and the C++ side has to justify itself.

Run it from the repo root with the project venv:

    .venv/Scripts/python.exe tools/gen_golden.py

It rewrites core/tests/golden/rfft.txt. That file is committed, so the C++ test
suite needs neither Python nor NumPy -- CI stays a plain CMake build.

Format
------
Deliberately not JSON. A line-based format needs no parser dependency in
core/tests (which must stay free of them), diffs readably in git, and cannot
fail in the subtle ways a hand-rolled JSON reader can.

    case <name>
    size <n>
    <key> <v0> <v1> ...
    end

Inputs are generated in float32 and written widened to float64, which is exact,
so the C++ side reads back bit-identical samples. That matters: a golden vector
compared against a *slightly different input* tests nothing.
"""

from __future__ import annotations

import pathlib
import sys

import numpy as np

OUT_DIR = pathlib.Path(__file__).resolve().parent.parent / "core" / "tests" / "golden"


def fmt(values) -> str:
    # repr-grade precision: float64 round-trips, and our float32 inputs widen
    # to float64 exactly.
    return " ".join(repr(float(v)) for v in values)


class ForwardCase:
    """Real samples in, rfft out."""

    def __init__(self, name: str, samples: np.ndarray):
        assert samples.dtype == np.float32, "inputs must be generated in float32"
        self.name = name
        self.samples = samples

    def render(self) -> str:
        spectrum = np.fft.rfft(self.samples.astype(np.float64))
        return "\n".join([
            f"case {self.name}",
            f"size {self.samples.size}",
            f"input {fmt(self.samples)}",
            f"rfft_re {fmt(spectrum.real)}",
            f"rfft_im {fmt(spectrum.imag)}",
            "end",
        ])


class InverseCase:
    """A spectrum that did NOT come from rfft, and what irfft makes of it.

    Forward-then-inverse can never expose the DC/Nyquist convention: rfft only
    ever puts a real value in those two bins, so an implementation that reads
    their imaginary part and one that discards it agree on everything rfft
    produces. Real analysers do not only invert their own output -- they smooth,
    apply target curves and subtract traces first, and any of those can leave
    something in an imaginary part that physically cannot exist. These cases put
    it there deliberately.
    """

    def __init__(self, name: str, spectrum: np.ndarray, size: int):
        assert spectrum.dtype == np.complex128
        self.name = name
        self.spectrum = spectrum
        self.size = size

    def render(self) -> str:
        recovered = np.fft.irfft(self.spectrum, n=self.size)
        return "\n".join([
            f"case {self.name}",
            f"size {self.size}",
            f"spec_re {fmt(self.spectrum.real)}",
            f"spec_im {fmt(self.spectrum.imag)}",
            f"irfft {fmt(recovered)}",
            "end",
        ])


def build_forward_cases() -> list[ForwardCase]:
    cases: list[ForwardCase] = []

    # A unit impulse at n=0 transforms to a flat spectrum of ones. Catches a
    # normalisation applied in the wrong direction.
    impulse = np.zeros(16, dtype=np.float32)
    impulse[0] = 1.0
    cases.append(ForwardCase("impulse_at_zero", impulse))

    # An impulse anywhere else has flat MAGNITUDE and a linear phase ramp.
    # Catches a bit-reversal or twiddle-sign error that a symmetric input hides.
    shifted = np.zeros(32, dtype=np.float32)
    shifted[5] = 1.0
    cases.append(ForwardCase("impulse_at_five", shifted))

    # DC: all energy in bin 0, magnitude N.
    cases.append(ForwardCase("dc", np.ones(16, dtype=np.float32)))

    n = 64
    t = np.arange(n)

    # A sine sitting exactly on bin 5: every other bin must be zero. Any leakage
    # here is the transform's own error, not the signal's.
    cases.append(ForwardCase("sine_on_bin_5",
                             np.sin(2 * np.pi * 5 * t / n).astype(np.float32)))

    # A sine BETWEEN bins leaks into every bin in a precisely defined way. This
    # is the case that actually pins the transform down -- an implementation can
    # be wrong in ways that on-bin sines and impulses never reveal.
    cases.append(ForwardCase("sine_off_bin_5_5",
                             np.sin(2 * np.pi * 5.5 * t / n).astype(np.float32)))

    # Arbitrary data at several sizes, starting at the smallest the real
    # transform supports. A radix-2 implementation can be wrong only at its base
    # case, where the butterfly loop runs once.
    for size in (4, 8, 256, 4096):
        rng = np.random.default_rng(seed=size)
        cases.append(ForwardCase(f"noise_{size}",
                                 rng.standard_normal(size).astype(np.float32)))

    return cases


def build_inverse_cases() -> list[InverseCase]:
    cases: list[InverseCase] = []

    # Junk in the imaginary part of DC and of Nyquist. A real signal cannot
    # produce either, so irfft discards both; an implementation that trusts them
    # returns a different signal.
    spectrum = np.array([0.0 + 3.0j, 2.0 - 1.0j, 5.0 + 4.0j], dtype=np.complex128)
    cases.append(InverseCase("inverse_junk_at_dc_and_nyquist", spectrum, 4))

    # The same idea at a size where the interior bins outnumber the two ends.
    rng = np.random.default_rng(seed=99)
    interior = rng.standard_normal(9) + 1j * rng.standard_normal(9)
    interior[0] = 1.5 + 2.5j     # DC with an impossible imaginary part
    interior[-1] = -0.5 - 3.5j   # Nyquist likewise
    cases.append(InverseCase("inverse_junk_16", interior, 16))

    return cases


def main() -> int:
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    forward = build_forward_cases()
    inverse = build_inverse_cases()

    header = [
        "# rta_core golden vectors -- generated by tools/gen_golden.py",
        f"# numpy {np.__version__}",
        "# DO NOT EDIT BY HAND. Regenerate and review the diff.",
        "",
    ]
    body = "\n".join(case.render() for case in [*forward, *inverse])

    path = OUT_DIR / "rfft.txt"
    path.write_text("\n".join(header) + body + "\n", encoding="utf-8")

    print(f"wrote {path} -- {len(forward)} forward + {len(inverse)} inverse cases, "
          f"numpy {np.__version__}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
