#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""The G24 excess-phase oversampling MEASUREMENT (lane L7, sub-lane L7-EQ,
plan docs/plans/2026-09-07-L7-eq-impl-plan.md Task A) -- reimplements
ExcessPhase.cpp's homomorphic kernel in numpy to sweep the oversampling
factor against RAW, UNWINDOWED measured magnitude: the analytic two-path
comb H(theta) = 1 + a*e^{-j theta D}, never windowed, so its notches are as
sharp as the physics allows -- unlike FIR's kCepstralOversamplingFactor=8,
which is safe only because designLinearPhaseCore windows the target FIRST
(FirDesign.cpp:16-36, the caveat this task exists to re-justify).

Two things this sweep measures that gen_fir_algo.py's own sweep did NOT --
that script's own docstring: "the magnitude-identity residual ... is NOT
evidence ... an algebraic tautology, Re(FFT(fold(c))) == FFT(c) for any
real, even cepstrum c":
  1. RECONSTRUCTED EXCESS-PHASE SWING S(F) -- the quantity G24 actually
     gates on (DipClassifier's S, docs/dsp/2026-09-06-l7-auto-eq.md Sec.4.3.5).
  2. MIN-PHASE IMPULSE TAIL ENERGY past nFft/2 -- the operational analogue
     of FirResult::truncationLossDb, where cepstral aliasing wraps energy
     into the causal window (MinimumPhase.cpp's fold splits exactly there).

Pure functions and constants only, no argparse, no file writes -- mirrors
gen_fir_algo.py's own split from its CLI driver (gen_autoeq.py here); memory/
a-gen-script-runs-the-moment-you-invoke-it.md is the warning about the OTHER
half of that split, not this one.
"""

from __future__ import annotations

import numpy as np

FS = 48000.0

# Same -120 dB as core/include/rta/dsp/MinimumPhase.h's kMinPhaseFloorDb --
# one constant for one physical reason, not re-derived here.
FLOOR_DB = -120.0

# Plan Task A names this exact set: F in {1,2,4,8,16,32,64,128,256}.
SWEEP_FACTORS = [1, 2, 4, 8, 16, 32, 64, 128, 256]
REFERENCE_FACTOR = SWEEP_FACTORS[-1]

# a in {1.25, 2, 4}: the NMP side (a>1), sharpest for large a -- record
# Sec.1.2's identity |1+a e^{-jw}| == a|1+(1/a)e^{-jw}| means these three
# magnitudes are matched, bin for bin, by a<1 minimum-phase twins the C++
# test (test_excess_phase.cpp, case B1/B2) builds from the same a values.
COMB_A_VALUES = [1.25, 2.0, 4.0]

# Several D: Task B's own two-path fixture uses D=144 at 48 kHz; a shorter
# and a longer delay are added so the measured factor is not tuned to one
# case (a longer D packs the comb's notches closer together in bins, which
# is what stresses cepstral aliasing -- Sec.1.2's whole point is that the
# NOTCH SHAPE, not the absolute frequency, is what a fixed oversampling
# factor must resolve).
COMB_D_SAMPLES = [37, 144, 511]

# The "realistic bin count" the plan asks the golden's interpolation-error
# figure to be reported at -- a base FFT size representative of the
# fixed-engine grid (record Sec.7: "M ~ 8193" bins DC..Nyquist implies
# nFft = 2*(M-1) = 16384) kept smaller here (2048) so the sweep to F=256 (a
# 524288-point transform) runs in about a second rather than minutes. What
# drives cepstral aliasing is the notch's SHARPNESS relative to the FFT bin
# spacing (set by D and a relative to nFft), not nFft's absolute size, so a
# smaller representative nFft is not a weaker test of the same phenomenon.
BASE_NFFT = 2048

# Plan Task A's own convergence rule for the swing metric.
SWING_CONVERGE_DEG = 0.5

# The plan states no analogous number for the tail-energy metric ("stops
# falling with F"); this project labels judgments rather than leaving them
# implicit (CLAUDE.md).
#
# MEASURED FINDING while choosing this (flag to the record owner, same
# treatment as gen_fir_algo.py's own D6 finding): a threshold relative to
# the F=256 reference (e.g. "within 1 dB of tail_db(256)") does NOT work --
# once the true tail energy drops below roughly -300 dB it is dominated by
# DOUBLE-PRECISION FFT/IFFT ROUND-TRIP NOISE (float64 eps ~1e-16, squared
# and log10'd lands near -300 to -320 dB), which jitters by more than 1 dB
# factor-to-factor with no downward trend at all -- chasing it drives every
# fixture to the reference factor itself, which discriminates nothing (the
# same failure mode gen_fir_algo.sweep_oversampling_factor's docstring
# documents for the magnitude-identity residual). An ABSOLUTE floor sidesteps
# this: -200 dB is 80 dB under kMinPhaseFloorDb's own -120 dB measurement
# floor (MinimumPhase.h) -- energy that quiet cannot move any bin this
# project ever floors or displays, so "converged" here means "physically
# inconsequential", not "indistinguishable from the reference's own noise".
TAIL_CONVERGE_FLOOR_DB = -200.0


def next_pow2(n: int) -> int:
    m = 1
    while m < n:
        m <<= 1
    return m


def comb_h_and_magnitude(a: float, d: int, n_fft: int) -> tuple[np.ndarray, np.ndarray]:
    """H and |H| for the analytic, UNWINDOWED two-path comb
    H(theta) = 1 + a*e^{-j theta D}, evaluated EXACTLY (no interpolation) on
    the full n_fft-point circle -- theta_k = 2*pi*k/n_fft, D counted in
    samples of this same grid. This is the sharpest fixture G24 will meet: a
    real reflection has no windowing step ahead of it the way FIR's target
    curve does (designLinearPhaseCore, FirDesign.cpp)."""
    k = np.arange(n_fft)
    theta = 2.0 * np.pi * k / n_fft
    h = 1.0 + a * np.exp(-1j * theta * d)
    return h, np.abs(h)


def fold_window(n_fft: int) -> np.ndarray:
    """[1, 2, 2, ..., 2, 1] -- the SAME fold MinimumPhase.cpp implements
    (Sec.4 step 4), reused verbatim from gen_fir_algo.py's own port of it."""
    win = np.zeros(n_fft)
    win[0] = 1.0
    half = n_fft // 2
    win[1:half] = 2.0
    win[half] = 1.0 + (n_fft % 2)
    return win


def homomorphic_reconstruct_full(magnitude: np.ndarray, n_fft: int,
                                  floor_db: float = FLOOR_DB
                                  ) -> tuple[np.ndarray, np.ndarray]:
    """The SAME five steps as MinimumPhase.cpp: floor before the log, log
    NOT halved, real cepstrum via IFFT, fold, exponentiate (FFT then exp).
    Returns (h_min_full time-domain, H_min frequency-domain spectrum)."""
    floor_linear = 10.0 ** (floor_db / 20.0)
    mag = np.maximum(magnitude, floor_linear)
    log_mag = np.log(mag)
    cepstrum = np.real(np.fft.ifft(log_mag))
    folded = cepstrum * fold_window(n_fft)
    h_min_spectrum = np.exp(np.fft.fft(folded))
    h_min_full = np.real(np.fft.ifft(h_min_spectrum))
    return h_min_full, h_min_spectrum


def excess_phase_swing_deg(a: float, d: int, n_fft: int) -> float:
    """Peak-to-peak reconstructed excess-phase swing, in degrees, after
    removing the D-sample delay ANALYTICALLY (D is known exactly here,
    unlike the noisy measured case ExcessPhase.cpp handles by a median
    group-delay estimate) -- by ONE complex multiply-then-angle, never by
    adding angles arithmetically, so no unwrap bookkeeping is needed: for
    a>1 the true peak amplitude is 2*arcsin(1/a) < pi/2 (record Sec.4.3.6's
    own table tops out at 140 deg for a 30 dB notch), safely inside angle()'s
    principal branch, so a plain wrapped angle() is exact, not an
    approximation that could hide a wrap-around bug."""
    h, magnitude = comb_h_and_magnitude(a, d, n_fft)
    _, h_min_spectrum = homomorphic_reconstruct_full(magnitude, n_fft)
    k = np.arange(n_fft)
    theta = 2.0 * np.pi * k / n_fft
    ratio = (h / h_min_spectrum) * np.exp(1j * theta * d)
    excess = np.angle(ratio)
    return float(np.degrees(np.max(excess) - np.min(excess)))


def tail_energy_db(a: float, d: int, n_fft: int) -> float:
    """Energy past the causal window's own midpoint (nFft/2), as a ratio in
    dB of total impulse energy -- the operational analogue of
    FirResult::truncationLossDb, except excessPhase never ships an impulse
    to truncate, so nFft/2 stands in as the boundary cepstral aliasing wraps
    energy across (the fold in Sec.4 step 4 splits the cepstrum exactly
    there: [1,2,2,...,2,1], weight 0 past the midpoint)."""
    _, magnitude = comb_h_and_magnitude(a, d, n_fft)
    h_min_full, _ = homomorphic_reconstruct_full(magnitude, n_fft)
    half = n_fft // 2
    energy_all = float(np.sum(h_min_full.astype(np.float64) ** 2))
    energy_tail = float(np.sum(h_min_full[half:].astype(np.float64) ** 2))
    ratio = energy_tail / energy_all if energy_all > 0.0 else 0.0
    return 10.0 * np.log10(ratio) if ratio > 0.0 else FLOOR_DB


def sweep_one(a: float, d: int) -> dict[str, dict[int, float]]:
    """One (a, D) fixture's full sweep table, keyed by factor."""
    swings: dict[int, float] = {}
    tails: dict[int, float] = {}
    for factor in SWEEP_FACTORS:
        n_fft = next_pow2(factor * BASE_NFFT)
        swings[factor] = excess_phase_swing_deg(a, d, n_fft)
        tails[factor] = tail_energy_db(a, d, n_fft)
    return {"swings": swings, "tails": tails}


def fixture_minimum_factor(a: float, d: int) -> tuple[dict[str, dict[int, float]], int]:
    """The smallest F where BOTH convergence criteria hold, for one (a, D)
    fixture: the swing metric against the F=256 reference (plan Task A's own
    rule), the tail-energy metric against the absolute floor above (this
    module's own finding for why a relative-to-reference rule fails it)."""
    table = sweep_one(a, d)
    swing_ref = table["swings"][REFERENCE_FACTOR]
    for factor in SWEEP_FACTORS:
        swing_ok = abs(table["swings"][factor] - swing_ref) < SWING_CONVERGE_DEG
        tail_ok = table["tails"][factor] <= TAIL_CONVERGE_FLOOR_DB
        if swing_ok and tail_ok:
            return table, factor
    raise AssertionError(
        f"no swept factor in {SWEEP_FACTORS} converges both metrics for "
        f"a={a}, D={d} -- widen SWEEP_FACTORS")


def measure_minimum_factor() -> dict:
    """Sweeps every (a, D) combination named above and returns the per-
    fixture tables plus the measured minimum (the WORST -- i.e. largest --
    of each fixture's own minimum, so every fixture is covered) and that
    minimum with one step of margin (mirrors gen_fir_algo.factor_with_margin)."""
    per_fixture = {}
    worst_idx = 0
    for a in COMB_A_VALUES:
        for d in COMB_D_SAMPLES:
            table, min_factor = fixture_minimum_factor(a, d)
            per_fixture[(a, d)] = {**table, "min_factor": min_factor}
            worst_idx = max(worst_idx, SWEEP_FACTORS.index(min_factor))
    measured_minimum = SWEEP_FACTORS[worst_idx]
    return {
        "per_fixture": per_fixture,
        "measured_minimum": measured_minimum,
        "chosen_with_margin": factor_with_margin(measured_minimum),
    }


def factor_with_margin(measured: int) -> int:
    """One step above the measured minimum -- not the bare minimum, the
    same margin policy gen_fir_algo.py's own factor_with_margin uses and for
    the same reason: the sweep's convergence test can only prove a factor
    IS enough, never prove the next one down is not (record Sec.4 caveat)."""
    idx = SWEEP_FACTORS.index(measured)
    return SWEEP_FACTORS[min(idx + 1, len(SWEEP_FACTORS) - 1)]
