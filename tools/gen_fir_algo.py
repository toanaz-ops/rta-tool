#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""The FirDesign algorithm, reimplemented in numpy/scipy -- a second author
of core/src/dsp/FirDesign.cpp's own steps (record
docs/dsp/2026-09-06-l7-fir-export.md Sec.4, Sec.7 item 7). Pure functions and
constants only, no argparse, no file writes -- tools/gen_fir.py (the CLI
driver) is the thing memory/a-gen-script-runs-the-moment-you-invoke-it.md's
warning applies to, not this module: importing this one does nothing.

Split out of gen_fir.py (task F6) once that file crossed the project's
400-line hard cap -- CLAUDE.md: "a file past that is doing more than one
job, split it along the seam that made it long", and algorithm vs. CLI
driver/golden-writer is exactly that seam.

Two things this module does NOT do, and why:
  - It does not call scipy.signal.minimum_phase for the oversampling sweep or
    the truncation-loss number: that function always returns the TRUNCATED
    output (`h_minimum[:n_out]`, read from scipy's own source this session),
    and both of those need the FULL, pre-truncation nFft-length reconstruction.
    homomorphic_reconstruct_full below is a faithful port of scipy's
    algorithm (same log-magnitude floor placement, same fold window, same
    forward/inverse order) that stops one step earlier, self-checked against
    scipy's public output in assert_matches_scipy.
  - It does not use scipy's own floor (`1e-7 * min(|H|>0)`) for that
    reconstruction: it uses this project's -120 dB floor (MinimumPhase.h's
    kMinPhaseFloorDb), because the SWEEP and the truncation-loss number are
    both things the actual C++ pipeline produces, and the golden's minimum-
    phase TAPS (where scipy genuinely is "the second author") are computed
    by calling scipy.signal.minimum_phase directly (in gen_fir.py). The
    fixture gen_fir.py builds stays well clear of -120 dB so the two floor
    conventions never actually disagree on it -- verified by
    assert_matches_scipy.
"""

from __future__ import annotations

import numpy as np
from scipy import signal

FS = 48000.0

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


def fmt(values) -> str:
    return " ".join(repr(float(v)) for v in values)


def next_pow2(n: int) -> int:
    m = 1
    while m < n:
        m <<= 1
    return m


def interpolate_target_db(f: np.ndarray) -> np.ndarray:
    """Mirrors core/src/dsp/FirDesign.cpp's interpolateFirTargetDb -- linear
    in log10(f), linear in dB, clamped to the edge gain outside the
    breakpoints (record Sec.6, plan T10)."""
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
                      assert_matches_scipy to reproduce scipy's public
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


def assert_matches_scipy(h_lin: np.ndarray, n_fft: int) -> None:
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
            f"gen_fir_algo's homomorphic port disagrees with scipy.signal.minimum_phase: "
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
    truncationLossDb instead (record Sec.4's own next paragraph), which
    gen_fir.py's build() also measures per factor and prints, and which was
    equally insensitive within the swept range for every fixture tried.
    Given the sweep's inability to find an aliasing floor at all, `chosen`
    below takes the smallest factor clearing FLOAT32_FLOOR and reports it AS
    MEASURED, but gen_fir.py's build() ships the NEXT factor up as a margin,
    not the bare minimum (see factor_with_margin)."""
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
