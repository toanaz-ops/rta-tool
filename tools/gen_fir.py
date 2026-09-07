#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Golden vectors for rta_core's FirDesign (lane L7, sub-lane L7-FIR, G10),
plus the MEASURED cepstral oversampling factor plan D6 requires before F2's
C++ constant is fixed.

    .venv/Scripts/python.exe tools/gen_fir.py --out core/tests/golden/fir.txt

Run with the MAIN-CHECKOUT venv (memory/build-toolchain-on-this-machine.md);
this worktree has no scipy. memory/a-gen-script-runs-the-moment-you-invoke-it.md:
a gen_*.py script is a write to the repo the instant Python executes it, so
this one uses argparse for real (copied from tools/gen_mtw.py:165-190) and
`--help` exits before any write.

Two things this script does NOT do, and why:
  - It does not call scipy.signal.minimum_phase for the oversampling sweep or
    the truncation-loss number: that function always returns the TRUNCATED
    output (`h_minimum[:n_out]`, read from scipy's own source this session),
    and both of those need the FULL, pre-truncation nFft-length reconstruction.
    _homomorphic_reconstruct below is a faithful port of scipy's algorithm
    (same log-magnitude floor placement, same fold window, same forward/
    inverse order) that stops one step earlier, self-checked against scipy's
    public output in _assert_matches_scipy.
  - It does not use scipy's own floor (`1e-7 * min(|H|>0)`) for that
    reconstruction: it uses this project's -120 dB floor (MinimumPhase.h's
    kMinPhaseFloorDb), because the SWEEP and the truncation-loss number are
    both things the actual C++ pipeline produces, and the golden's minimum-
    phase TAPS (where scipy genuinely is "the second author", record Sec.7
    item 7) are computed by calling scipy.signal.minimum_phase directly. The
    fixture below stays well clear of -120 dB (gains in [-6, +8] dB) so the
    two floor conventions never actually disagree on it -- verified by
    _assert_matches_scipy.

record docs/dsp/2026-09-06-l7-fir-export.md Sec.4, Sec.7 item 7. Plan
docs/plans/2026-09-07-L7-fir-impl-plan.md task F6, decision D6.
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

# Same -120 dB as core/include/rta/dsp/MinimumPhase.h's kMinPhaseFloorDb --
# one constant for one physical reason, not re-derived here (memory/
# a-fixed-defect-returns-through-the-silent-fallback.md's sibling caution
# about drifted constants applies to floors too).
FLOOR_DB = -120.0

# The candidates plan D6 names explicitly -- not a range chosen here.
SWEEP_FACTORS = [4, 8, 16, 32, 64, 128, 256]

# Shape A's peak coefficient (plan "Global constraints", c_M = 2e-7 for
# M <= 2^20): the sweep's own stopping rule is "below the float32 floor this
# constant sets", i.e. an aliasing residual smaller than float32 rounding
# would show anyway is not worth chasing with a bigger nFft.
FLOAT32_FLOOR = 2e-7

# The boost-and-cut fixture the golden's taps are built from (record Sec.7
# item 7). Gains stay inside [-6, +8] dB, far from FLOOR_DB, so the floor
# never engages and scipy's own floor convention cannot disagree with this
# project's -120 dB one on this data (see the module docstring).
TARGET_FREQS = np.array([20.0, 60.0, 200.0, 800.0, 3000.0, 8000.0, 20000.0])
TARGET_GAINS_DB = np.array([-2.0, 3.0, -6.0, 8.0, -4.0, 2.0, 0.0])

FIXTURE_TAPS = [1023, 4095]


def fmt(values) -> str:
    return " ".join(repr(float(v)) for v in values)


def next_pow2(n: int) -> int:
    m = 1
    while m < n:
        m <<= 1
    return m


def interpolate_target_db(f: np.ndarray) -> np.ndarray:
    """Mirrors core/src/dsp/FirDesign.cpp's interpolateTargetDb -- linear in
    log10(f), linear in dB, clamped to the edge gain outside the breakpoints
    (record Sec.6, plan T10)."""
    f_clamped = np.clip(f, TARGET_FREQS[0], TARGET_FREQS[-1])
    return np.interp(np.log10(f_clamped), np.log10(TARGET_FREQS), TARGET_GAINS_DB)


def sample_target_magnitude(m: int) -> np.ndarray:
    bins = m // 2 + 1
    f = np.arange(bins) * FS / m
    db = interpolate_target_db(f)
    return 10.0 ** (db / 20.0)


def design_linear_phase(n: int, window: str = "hann") -> tuple[np.ndarray, int]:
    """Mirrors core/src/dsp/FirDesign.cpp's designLinearPhaseCore bit for bit:
    sample the target on an M/2+1 half-grid, irfft (numpy's own 1/N, matching
    RealFft::inverse), then build ONLY the first half explicitly and mirror
    the WINDOWED value -- not the raw sample -- so taps[i] == taps[n-1-i] is
    exact by construction, the same reason the C++ side does it that way
    (a periodic window's own coefficients are symmetric about N/2, not about
    (N-1)/2 -- see FirDesign.cpp's comment on this exact point).
    """
    m = next_pow2(8 * n)
    magnitude = sample_target_magnitude(m)
    h_zero = np.fft.irfft(magnitude.astype(complex), n=m)

    # fftbins=True: PERIODIC, matching rta::dsp::Window (Window.h's own
    # docstring) -- fftbins=False ("for use in filter design") is the trap
    # record Sec.7 warns about: it would silently stop matching the C++ side.
    w = signal.get_window(window, n, fftbins=True)

    half = (n - 1) // 2
    taps = np.zeros(n)
    for i in range(half + 1):
        offset = i - half
        idx = offset % m
        value = h_zero[idx] * w[i]
        taps[i] = value
        taps[n - 1 - i] = value
    return taps, m


def _fold_window(n_fft: int) -> np.ndarray:
    """win = [1, 2, 2, ..., 2, 1 + (n_fft % 2)] -- Oppenheim & Schafer's
    minimum-phase fold, exactly as scipy's minimum_phase source has it and as
    core/src/dsp/MinimumPhase.cpp implements it (record Sec.4 step 2)."""
    win = np.zeros(n_fft)
    win[0] = 1.0
    stop = n_fft // 2
    win[1:stop] = 2.0
    win[stop] = 1.0 + (n_fft % 2)
    return win


def homomorphic_reconstruct_full(h_lin: np.ndarray, n_fft: int,
                                  floor_db: float | None = FLOOR_DB
                                  ) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """The homomorphic method (record Sec.4), stopping BEFORE scipy's own
    truncation to len(h) -- a faithful port of scipy.signal.minimum_phase's
    'homomorphic' branch (read from source this session). Returns
    (h_min_full, H_min, H_lin) so a caller can measure the magnitude identity
    |H_min| == |H_lin| on the FULL nFft grid, before any truncation cost is
    paid (record Sec.4's "verification that does not depend on the
    derivation being right").

    @param floor_db  this project's -120 dB multiplicative floor
                      (kMinPhaseFloorDb, D5) when a number; scipy's OWN
                      additive `1e-7 * min(|H|>0)` floor (read from its
                      source this session) when None -- used only by
                      _assert_matches_scipy to reproduce scipy's public
                      output bit-for-bit as a self-check on this port.
    """
    h_lin_padded = np.zeros(n_fft)
    h_lin_padded[: len(h_lin)] = h_lin
    h_lin_spectrum = np.fft.fft(h_lin_padded)
    magnitude = np.abs(h_lin_spectrum)
    if floor_db is None:
        magnitude = magnitude + 1e-7 * np.min(magnitude[magnitude > 0])
    else:
        floor_linear = 10.0 ** (floor_db / 20.0)
        magnitude = np.maximum(magnitude, floor_linear)

    log_magnitude = np.log(magnitude)
    cepstrum = np.real(np.fft.ifft(log_magnitude))
    cepstrum_folded = cepstrum * _fold_window(n_fft)
    h_min_spectrum = np.exp(np.fft.fft(cepstrum_folded))
    h_min_full = np.real(np.fft.ifft(h_min_spectrum))
    return h_min_full, h_min_spectrum, h_lin_spectrum


def truncation_loss_db(h_min_full: np.ndarray, n: int) -> float:
    """Mirrors FirResult::truncationLossDb's definition exactly (FirDesign.h):
    10*log10(energy at or past sample N / total energy), reported at the
    floor rather than -inf when nothing was lost."""
    energy_all = float(np.sum(h_min_full.astype(np.float64) ** 2))
    energy_kept = float(np.sum(h_min_full[:n].astype(np.float64) ** 2))
    energy_lost = max(energy_all - energy_kept, 0.0)
    fraction = energy_lost / energy_all if energy_all > 0.0 else 0.0
    return 10.0 * np.log10(fraction) if fraction > 0.0 else FLOOR_DB


def _assert_matches_scipy(h_lin: np.ndarray, n_fft: int) -> None:
    """Self-check: this module's reimplementation, using SCIPY's OWN floor
    convention instead of this project's -120 dB one, must reproduce
    scipy.signal.minimum_phase's public (truncated) output -- otherwise the
    port has a bug, and the sweep/truncation numbers it produces are not
    trustworthy. This is a script-internal assertion, not a committed test."""
    h_min_full_scipy_floor, _, _ = homomorphic_reconstruct_full(h_lin, n_fft, floor_db=None)
    n = len(h_lin)
    scipy_taps = signal.minimum_phase(h_lin, method="homomorphic", n_fft=n_fft, half=False)
    residual = np.max(np.abs(h_min_full_scipy_floor[:n] - scipy_taps))
    peak = max(np.max(np.abs(scipy_taps)), 1e-30)
    if residual / peak > 1e-9:
        raise AssertionError(
            f"gen_fir.py's homomorphic port disagrees with scipy.signal.minimum_phase: "
            f"relative residual {residual / peak:.3e} at n_fft={n_fft}")


def sweep_oversampling_factor(h_lin: np.ndarray) -> tuple[list[tuple[int, float]], int]:
    """D6: sweep n_fft/N over SWEEP_FACTORS, measure the magnitude-identity
    residual on the FULL grid (before truncation), return the table and the
    smallest factor whose relative residual clears FLOAT32_FLOOR.

    MEASURED FINDING (flag to the record owner, same treatment as D5/OQ-A):
    Re(FFT(fold(c)))[k] == FFT(c)[k] EXACTLY for any real, conjugate-even
    cepstrum c (a pure linear-algebra identity -- fft(ifft(x)) == x -- not an
    approximation that improves with n_fft), and c is always real+even here
    because log|H_lin| is always exactly conjugate-symmetric for a real
    h_lin. So this magnitude-identity residual sits at the float64 rounding
    floor (~1e-15/1e-16) at EVERY swept factor, including the smallest
    (4x) -- for every fixture this script tried, including a -115 dB notch
    deliberately added to stress it. It does not discriminate a factor that
    is "big enough" from one that is not; cepstral aliasing shows up in
    truncationLossDb instead (record Sec.4's own next paragraph), which this
    script also measures per factor and prints, and which was equally
    insensitive within the swept range for every fixture tried. Given the
    sweep's inability to find an aliasing floor at all, `chosen` below takes
    the smallest factor clearing FLOAT32_FLOOR and reports it AS MEASURED,
    but build() ships the NEXT factor up as a margin, not the bare minimum
    (see chosen_factor_global vs chosen_factor_nNNNN in the golden)."""
    n = len(h_lin)
    table: list[tuple[int, float]] = []
    chosen = SWEEP_FACTORS[-1]
    found = False
    for factor in SWEEP_FACTORS:
        n_fft = next_pow2(factor * n)
        h_min_full, h_min_spectrum, h_lin_spectrum = homomorphic_reconstruct_full(h_lin, n_fft)
        residual = np.max(np.abs(np.abs(h_min_spectrum) - np.abs(h_lin_spectrum)))
        peak = np.max(np.abs(h_lin_spectrum))
        relative = residual / peak
        table.append((factor, relative))
        if not found and relative < FLOAT32_FLOOR:
            chosen = factor
            found = True
    if not found:
        raise AssertionError(
            f"no swept factor in {SWEEP_FACTORS} clears the float32 floor {FLOAT32_FLOOR:.1e} "
            f"for N={n} -- widen SWEEP_FACTORS")
    return table, chosen


def factor_with_margin(measured: int) -> int:
    """One step above the measured minimum (SWEEP_FACTORS is ascending) --
    not the bare minimum, since the sweep above was found NOT to discriminate
    a truly insufficient factor from a sufficient one within the tested
    range (see sweep_oversampling_factor's docstring); still two-plus orders
    of magnitude below scipy's uncontrolled ~200x default (record Sec.4),
    which is the thing D6 forbids copying."""
    idx = SWEEP_FACTORS.index(measured)
    return SWEEP_FACTORS[min(idx + 1, len(SWEEP_FACTORS) - 1)]


def build(out_path: pathlib.Path) -> str:
    lines = [
        "# rta_core golden vectors -- FIR export (lane L7, sub-lane L7-FIR, G10)",
        "# generated by tools/gen_fir.py from numpy.fft.irfft (frequency sampling,",
        "# a second author of core/src/dsp/FirDesign.cpp's own algorithm) and",
        "# scipy.signal.minimum_phase (homomorphic, half=False -- the minimum-phase",
        "# taps, scipy as the second author, record Sec.7 item 7).",
        "# DO NOT EDIT BY HAND. Regenerate and review the diff.",
        "#",
        "# Conventions pinned (record Sec.7, docs/HANDOFF.md's generator-asymmetry",
        "# warning -- FIR taps are ABSOLUTE numbers, not ratios, so every one of",
        "# these must match on both sides):",
        "#   window form:      scipy.signal.get_window(..., fftbins=True) -- PERIODIC,",
        "#                      matching rta::dsp::Window; fftbins=False is the trap.",
        "#   IDFT normalisation: numpy.fft.irfft includes 1/N, matching RealFft::inverse.",
        "#   circular-shift:    build taps[0..(N-1)/2] explicitly, mirror the WINDOWED",
        "#                      value (not the raw sample) to taps[N-1-n].",
        "#   floor:             -120 dB (kMinPhaseFloorDb), multiplicative on |H|.",
        "#   half:              False (scipy) -- |H_min| == |H_lin|, not its square root.",
        "#   normalization:     as_designed (no peak-0dBFS trim baked into these taps).",
        "",
    ]

    sweep_case = ["case fir_oversampling_sweep"]
    chosen_factors: dict[int, int] = {}
    fixture_h_lin: dict[int, np.ndarray] = {}
    fixture_m: dict[int, int] = {}

    for n in FIXTURE_TAPS:
        h_lin, m = design_linear_phase(n)
        fixture_h_lin[n] = h_lin
        fixture_m[n] = m
        table, measured_min = sweep_oversampling_factor(h_lin)
        chosen_factors[n] = measured_min
        loss_by_factor = []
        for factor, _ in table:
            n_fft = next_pow2(factor * n)
            h_min_full, _, _ = homomorphic_reconstruct_full(h_lin, n_fft)
            loss_by_factor.append(truncation_loss_db(h_min_full, n))
        sweep_case.append(f"factors_n{n} " + fmt(f for f, _ in table))
        sweep_case.append(f"residuals_n{n} " + fmt(r for _, r in table))
        sweep_case.append(f"truncation_loss_db_by_factor_n{n} " + fmt(loss_by_factor))
        sweep_case.append(f"measured_minimum_factor_n{n} {measured_min}")
        print(f"  N={n}: measured minimum factor clearing the float32 floor = {measured_min}x "
              f"(residual and truncation-loss both flat across the whole swept range -- "
              f"see sweep_oversampling_factor's docstring)")

    # The single constant the C++ side ships (D6): one step of margin above
    # the measured minimum (factor_with_margin's docstring explains why the
    # bare minimum is not shipped), taken as the max over both fixtures so
    # neither one is left uncovered.
    global_factor = max(factor_with_margin(f) for f in chosen_factors.values())
    sweep_case.append(f"float32_floor {FLOAT32_FLOOR}")
    sweep_case.append(f"chosen_factor_global {global_factor}")
    lines.extend(sweep_case)
    lines.append("end")
    lines.append("")

    for n in FIXTURE_TAPS:
        h_lin = fixture_h_lin[n]
        _assert_matches_scipy(h_lin, next_pow2(global_factor * n))

        lines.append(f"case fir_freqsamp_{n}")
        lines.append(f"size {n}")
        lines.append(f"sample_rate {FS}")
        lines.append(f"design_fft_size {fixture_m[n]}")
        lines.append("taps " + fmt(h_lin))
        lines.append("end")
        lines.append("")

        n_fft = next_pow2(global_factor * n)
        h_min_full, _, _ = homomorphic_reconstruct_full(h_lin, n_fft)
        min_taps = h_min_full[:n]
        loss_db = truncation_loss_db(h_min_full, n)

        # scipy as the second author of the minimum-phase TAPS themselves
        # (record Sec.7 item 7) -- called directly, its own floor convention,
        # not this module's reconstruction. The fixture stays clear of -120 dB
        # so the two floors agree; _assert_matches_scipy above is the proof.
        scipy_taps = signal.minimum_phase(h_lin, method="homomorphic", n_fft=n_fft, half=False)
        cross_check = float(np.max(np.abs(scipy_taps - min_taps)))
        cross_check_peak = max(float(np.max(np.abs(scipy_taps))), 1e-30)
        cross_check_relative = cross_check / cross_check_peak
        print(f"  N={n}: golden vs scipy-own-floor minimum-phase taps, "
              f"max abs diff {cross_check:.3e} (relative {cross_check_relative:.3e})")
        # The two floor CONVENTIONS differ (this project's -120 dB
        # multiplicative floor vs scipy's `1e-7*min(|H|>0)` additive one, D5),
        # so even with the fixture nowhere near either floor the two
        # reconstructions are not bit-identical -- only close, because the
        # floor never actually clamps anything on this fixture. 1e-4 relative
        # catches a real algorithmic disagreement (wrong fold, wrong log
        # base, wrong n_fft) while tolerating that expected floor-choice
        # noise, which measured at ~1e-8 relative for both fixtures.
        if cross_check_relative > 1e-4:
            raise AssertionError(
                f"golden minimum-phase taps (this project's -120 dB floor) disagree with "
                f"scipy's own floor by {cross_check_relative:.3e} relative at N={n} -- this "
                f"is bigger than floor-choice noise, suspect an algorithmic bug")

        lines.append(f"case fir_minphase_{n}")
        lines.append(f"size {n}")
        lines.append(f"sample_rate {FS}")
        lines.append(f"design_fft_size {n_fft}")
        lines.append("taps " + fmt(min_taps))
        lines.append(f"truncation_loss_db {loss_db}")
        lines.append("end")
        lines.append("")

    text = NEWLINE.join(lines) + NEWLINE
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(text, encoding="utf-8")
    return text


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                      formatter_class=argparse.RawDescriptionHelpFormatter)
    default_out = pathlib.Path(__file__).resolve().parent.parent / "core" / "tests" / "golden" / "fir.txt"
    parser.add_argument("--out", type=pathlib.Path, default=default_out,
                         help="output path for the golden file")
    parser.add_argument("--check", action="store_true",
                         help="diff a fresh regeneration against --out instead of writing it")
    args = parser.parse_args()

    if args.check:
        with tempfile.TemporaryDirectory() as tmp:
            text = build(pathlib.Path(tmp) / "fir.txt")
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
