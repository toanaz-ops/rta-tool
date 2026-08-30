#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Golden vectors for `rta::ir`: deconvolution, band edges, flatness.

    .venv/Scripts/python.exe tools/gen_ir.py

Rewrites core/tests/golden/ir.txt.

## Every field here is amplitude-invariant, and that is enforced below

Two asymmetries exist between this Python side and the C++ it checks, both known
and both currently harmless:

1. `render_sweep_and_inverse` clamps each fade at `len(x) // 2`; the C++ has no
   equivalent clamp.
2. The sweep rendered here peaks at 1.0, while `rta::gen::Sweep` scales by
   `10^(-6/20)`.

They are harmless ONLY because every quantity these goldens compare is invariant
under a change of overall amplitude -- a ratio, a relative dB figure, a sample
index, or a frequency read at a level relative to a peak. Add one field that is
an absolute amplitude and asymmetry 2 activates instantly, silently, as a
constant factor of about two.

`docs/HANDOFF.md` records that this constraint has been stated in prose before.
Prose does not survive a working day, so it is a machine check here: every field
is declared with its KIND, and `case_block` refuses to write a kind it does not
recognise. Removing the check is a deliberate act with a message attached to it,
which is the point.

The real fix -- making the two implementations agree -- is deliberately NOT done
here. It would force every existing golden to be regenerated and re-verified
against its closed form in the same breath as closing this lane. It is recorded
as debt in `docs/HANDOFF.md` instead, with the note that the C++ convention wins
when someone does it: `10^(-6/20)` is what ships.
"""

from __future__ import annotations

import pathlib
import sys

import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from gen_generator import (case_block, render_sweep_and_inverse,  # noqa: E402
                           sweep_length_constants, synchronised_duration)

# The whitelist. A field's kind says WHY a difference of overall amplitude
# between the two implementations cannot reach it.
AMPLITUDE_INVARIANT_KINDS = {
    "index":       "a sample position; scaling the signal does not move it",
    "ratio":       "one measured amplitude over another; the factor cancels",
    "db_relative": "decibels against a mean or peak of the same measurement",
    "hz":          "a frequency read at a level relative to that measurement's peak",
    "config":      "an input the C++ is given, not a measurement it produces",
}


def checked_case(name: str, size: int, rows: dict[str, tuple[str, object]]) -> str:
    """`rows` maps key -> (kind, values). Refuses any kind not whitelisted.

    This is the machine form of "every field must be amplitude-invariant". The
    error message carries the reason so that whoever hits it does not have to
    find this docstring first.
    """
    plain = {}
    for key, (kind, values) in rows.items():
        if kind not in AMPLITUDE_INVARIANT_KINDS:
            raise SystemExit(
                f"gen_ir: field '{key}' declares kind '{kind}', which is not a "
                f"known amplitude-invariant kind.\n"
                f"  Known kinds: {', '.join(sorted(AMPLITUDE_INVARIANT_KINDS))}\n"
                f"  This generator's sweep peaks at 1.0 while rta::gen::Sweep "
                f"scales by 10^(-6/20), and its fades are clamped at len//2 "
                f"while the C++ has no such clamp. Any ABSOLUTE amplitude "
                f"written here would disagree with the C++ by about a factor of "
                f"two, quietly. Express the quantity as a ratio, or fix the two "
                f"implementations first (see docs/HANDOFF.md, and note that the "
                f"C++ convention is the one that ships).")
        plain[key] = values
    return case_block(name, size, plain)


def build_ir_deconv_case(fs: float, f1: float, f2: float, T: float) -> str:
    """A fixed three-tap room, deconvolved from a sweep built by closed form.

    The taps are asymmetric on purpose -- one inverted, all three at different
    levels -- so that a sign error, an ordering error, or a normalisation error
    each produce a different failure rather than the same one.
    """
    sweep, inverse = render_sweep_and_inverse(fs, f1, f2, T)

    taps = {0: 1.0, 500: 0.5, 1300: -0.25}
    room = np.zeros(max(taps) + 1)
    for offset, amp in taps.items():
        room[offset] = amp

    rendered = np.convolve(sweep, room)
    conv_len = len(rendered) + len(inverse) - 1
    size = 1 << int(np.ceil(np.log2(conv_len)))
    recovered = np.fft.irfft(np.fft.rfft(rendered, size) * np.fft.rfft(inverse, size),
                             size)[:conv_len]

    # t = 0 sits at inverse-1: the one lag where the reversed, shaped sweep
    # aligns with itself and every term of the sum is non-negative.
    origin = len(inverse) - 1
    main = recovered[origin]

    # Ratios, not amplitudes. This is the whole reason the two asymmetries above
    # are survivable, and the reason `kind` is "ratio" below.
    tap_ratios = [float(recovered[origin + off] / main) for off in sorted(taps)]

    # Flatness of the reference path -- the excitation deconvolved with its own
    # inverse -- across the band the record says is meaningful. Already relative
    # to its own in-band mean, so amplitude cancels.
    ref = np.fft.irfft(np.fft.rfft(sweep, size) * np.fft.rfft(inverse, size), size)
    length_l, _ = sweep_length_constants(f1, f2, T)
    fade_in_sec = max(0.02, 2.0 / f1, 2.0 * np.log(2.0) * length_l)
    band_lo = f1 * np.exp(fade_in_sec / length_l)
    band_hi = f2 * np.exp(-max(0.02, 2.0 / f2) / length_l)

    mag = np.abs(np.fft.rfft(ref))
    freqs = np.arange(len(mag)) * fs / len(ref)
    inband = (freqs >= band_lo) & (freqs <= band_hi)
    if not inband.any():
        raise SystemExit("gen_ir: the valid band contains no transform bins")
    mean = mag[inband].mean()
    rel_db = 20.0 * np.log10(np.maximum(mag[inband], 1e-30) / mean)

    # Harmonic packet offsets: -L*ln(N)*fs, a closed form, so the golden checks
    # the C++ against arithmetic rather than against a previous run.
    h_offsets = [float(-length_l * np.log(n) * fs) for n in (2, 3, 4)]

    print(f"  ir_deconv: origin={origin} taps={['%.4f' % r for r in tap_ratios]} "
          f"flatness=[{rel_db.min():.3f}, {rel_db.max():.3f}] dB")

    return checked_case("ir_deconv", len(tap_ratios), {
        "fs":              ("config", fs),
        "f1":              ("config", f1),
        "f2":              ("config", f2),
        "T":               ("config", T),
        "T_sync":          ("config", synchronised_duration(f1, f2, T)),
        "origin_index":    ("index", origin),
        "tap_offsets":     ("index", sorted(taps)),
        "tap_ratios":      ("ratio", tap_ratios),
        "flatness_min_db": ("db_relative", float(rel_db.min())),
        "flatness_max_db": ("db_relative", float(rel_db.max())),
        "band_low_hz":     ("hz", float(band_lo)),
        "band_high_hz":    ("hz", float(band_hi)),
        "harmonic_offsets": ("index", h_offsets),
    })


def main() -> int:
    fs, f1, f2, T = 48000.0, 100.0, 10000.0, 2.0
    print("gen_ir: rta::ir golden vectors")
    blocks = [build_ir_deconv_case(fs, f1, f2, T)]

    out = pathlib.Path(__file__).resolve().parent.parent / "core/tests/golden/ir.txt"
    header = ("# Generated by tools/gen_ir.py -- do not edit by hand.\n"
              "# MANIFEST: every field below is amplitude-invariant by "
              "construction.\n"
              "# This generator's sweep peaks at 1.0; rta::gen::Sweep scales by\n"
              "# 10^(-6/20). Any absolute amplitude written here would disagree "
              "with\n"
              "# the C++ by about a factor of two, silently. gen_ir.py enforces "
              "this\n"
              "# with a per-field kind whitelist; see its docstring.\n")
    out.write_text(header + "\n".join(blocks) + "\n", encoding="utf-8")
    print(f"  wrote {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
