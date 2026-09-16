# SPDX-License-Identifier: AGPL-3.0-or-later
"""Survey A of the relative-polarity figure rho -- ONE of TWO INDEPENDENT GRIDS.

Record `docs/dsp/2026-09-06-l7-alignment-wizard.md` Sec.8 requires two grids by
two authors with no shared script, and
`memory/a-threshold-read-off-a-grid-is-that-grids-floor.md` is why: lane L4a
produced three thresholds in three sessions, each honestly labelled and each
the floor of whatever grid its measurer happened to build.

THIS SCRIPT SHIPS NO NUMBER. It prints two distributions -- rho over cells whose
sign came out right and rho over cells whose sign came out wrong -- and the
candidate threshold that would give zero wrong signs ON THIS GRID, published
beside the fraction of GOOD cells that threshold would refuse. L4a's margin gate
was zero-lie and refused 74-88% of loudspeakers that answer correctly; that is
the failure mode to look for, not a clean separation.

Nothing here is adopted until `probe_rho_b.py` agrees.

    python tools/probe_rho_a.py --help
    python tools/probe_rho_a.py --quick
    python tools/probe_rho_a.py --csv out.csv

Filter design is SOS throughout: `filter_design_has_no_polynomial_form`
(core/tests/CMakeLists.txt:92) scans tools/*.py, and the (b, a) polynomial form
is not a less convenient representation of these filters, it is a wrong one.
"""

from __future__ import annotations

import argparse
import csv
import io
import math
import sys

import numpy as np
import scipy.signal

# --- the model -------------------------------------------------------------
#
# A "box" is a loudspeaker's electro-acoustic response, modelled as a band-pass
# filter applied to an impulse. A "unit" is one physical example of that box:
# the same design, plus the small gain and time differences two cabinets off
# the same line actually show.


def box_sos(family: str, order: int, low_hz: float, high_hz: float, fs: float):
    """Band-pass second-order sections for one box design."""
    band = [low_hz, high_hz]
    if family == "butter":
        return scipy.signal.butter(order, band, "bandpass", fs=fs, output="sos")
    if family == "cheby1":
        return scipy.signal.cheby1(order, 0.5, band, "bandpass", fs=fs, output="sos")
    if family == "ellip":
        return scipy.signal.ellip(order, 0.5, 40.0, band, "bandpass", fs=fs, output="sos")
    if family == "bessel":
        return scipy.signal.bessel(order, band, "bandpass", fs=fs, output="sos")
    raise ValueError(f"unknown family {family!r}")


def fir_taps(phase: str, low_hz: float, high_hz: float, fs: float, numtaps: int = 255):
    """Linear- or minimum-phase FIR band-pass taps."""
    taps = scipy.signal.firwin(numtaps, [low_hz, high_hz], pass_zero=False, fs=fs)
    if phase == "linear":
        return taps
    if phase == "minimum":
        return scipy.signal.minimum_phase(taps)
    raise ValueError(f"unknown phase {phase!r}")


def impulse_response(design, length: int) -> np.ndarray:
    """The box's IR: an impulse through the design, as SOS or as FIR taps."""
    x = np.zeros(length)
    x[0] = 1.0
    kind, payload = design
    if kind == "sos":
        return scipy.signal.sosfilt(payload, x)
    return np.convolve(x, payload)[:length]


def fractional_delay(signal: np.ndarray, samples: float) -> np.ndarray:
    """Shift by a possibly fractional number of samples, in the DFT domain.

    Exact for fractional shifts and introduces no interpolation error of its
    own, which matters because the OFFSET axis is one of the things being swept
    and an interpolation artefact would look like a rho degradation.
    """
    n = len(signal)
    spectrum = np.fft.rfft(signal)
    bins = np.fft.rfftfreq(n, d=1.0)
    return np.fft.irfft(spectrum * np.exp(-2j * np.pi * bins * samples), n)


def add_tail(signal: np.ndarray, direct_to_reverb_db: float, rng, fs: float) -> np.ndarray:
    """Add a synthetic exponentially decaying reverberant tail at a stated D/R."""
    n = len(signal)
    decay = np.exp(-np.arange(n) / (0.35 * fs))  # ~0.35 s time constant
    tail = rng.standard_normal(n) * decay
    direct_energy = float(np.sum(signal**2))
    tail_energy = float(np.sum(tail**2))
    if tail_energy <= 0.0 or direct_energy <= 0.0:
        return signal
    wanted = direct_energy / (10.0 ** (direct_to_reverb_db / 10.0))
    return signal + tail * math.sqrt(wanted / tail_energy)


def add_noise(signal: np.ndarray, snr_db: float, rng) -> np.ndarray:
    noise = rng.standard_normal(len(signal))
    signal_energy = float(np.sum(signal**2))
    noise_energy = float(np.sum(noise**2))
    if noise_energy <= 0.0 or signal_energy <= 0.0:
        return signal
    wanted = signal_energy / (10.0 ** (snr_db / 10.0))
    return signal + noise * math.sqrt(wanted / noise_energy)


def rho_and_sign(a: np.ndarray, b: np.ndarray, window: int) -> tuple[float, int, int]:
    """rho = |r(l*)| / sqrt(E_a E_b), the sign at that peak, and the lag.

    Both signals are taken as ZERO outside the arrival window, which is what
    makes the Cauchy-Schwarz bound exact at every lag rather than only at zero.
    """
    wa = np.zeros(window)
    wb = np.zeros(window)
    wa[: min(window, len(a))] = a[:window]
    wb[: min(window, len(b))] = b[:window]
    size = 1 << int(math.ceil(math.log2(2 * window)))
    r = np.fft.irfft(np.conj(np.fft.rfft(wa, size)) * np.fft.rfft(wb, size), size)
    index = int(np.argmax(np.abs(r)))
    lag = index if index <= size // 2 else index - size
    peak = float(r[index])
    denominator = math.sqrt(float(np.sum(wa**2)) * float(np.sum(wb**2)))
    if denominator <= 0.0:
        return 0.0, 0, 0
    return abs(peak) / denominator, (1 if peak >= 0.0 else -1), lag


# --- the grid --------------------------------------------------------------

FAMILIES = ("butter", "cheby1", "ellip", "bessel")
ORDERS = (2, 4, 8, 16)
FIR_PHASES = ("linear", "minimum")
SNR_DB = (10.0, 20.0, 30.0, 40.0)
DIRECT_TO_REVERB_DB = (-12.0, -6.0, 0.0, 6.0, 12.0)
OFFSET_MS = (0.0, 5.0, 10.0, 20.0, 30.0)
SAMPLE_RATES = (44100.0, 48000.0, 96000.0)
GAIN_SPREAD_DB = (-2.0, 2.0)
PHASE_SPREAD_DEG = (-10.0, 10.0)


def designs(fs: float):
    """Every box design on the grid, as (label, design) pairs."""
    out = []
    for family in FAMILIES:
        for order in ORDERS:
            out.append((f"{family}{order}", ("sos", box_sos(family, order, 60.0, 8000.0, fs))))
    for phase in FIR_PHASES:
        out.append((f"fir-{phase}", ("fir", fir_taps(phase, 60.0, 8000.0, fs))))
    return out


def same_system_cells(quick: bool, seed: int):
    """Two units of one model, which is the question rho MAY answer."""
    rng = np.random.default_rng(seed)
    snrs = SNR_DB[::2] if quick else SNR_DB
    drs = DIRECT_TO_REVERB_DB[::2] if quick else DIRECT_TO_REVERB_DB
    offsets = OFFSET_MS[::2] if quick else OFFSET_MS
    rates = SAMPLE_RATES[1:2] if quick else SAMPLE_RATES

    for fs in rates:
        length = int(0.5 * fs)
        window = int(0.05 * fs)
        for label, design in designs(fs):
            reference = impulse_response(design, length)
            for snr in snrs:
                for dr in drs:
                    for offset_ms in offsets:
                        for gain_db in GAIN_SPREAD_DB:
                            for phase_deg in PHASE_SPREAD_DEG:
                                for inverted in (False, True):
                                    # The second unit: same design, a small gain
                                    # and time spread, an offset, a room and some
                                    # noise -- and possibly wired backwards.
                                    unit = reference * (10.0 ** (gain_db / 20.0))
                                    # +-10 degrees at the band's geometric mean
                                    # is the time spread two cabinets show.
                                    centre = math.sqrt(60.0 * 8000.0)
                                    spread = (phase_deg / 360.0) * (fs / centre)
                                    shift = offset_ms * 1e-3 * fs + spread
                                    unit = fractional_delay(unit, shift)
                                    if inverted:
                                        unit = -unit
                                    unit = add_tail(unit, dr, rng, fs)
                                    unit = add_noise(unit, snr, rng)

                                    measured = add_noise(
                                        add_tail(reference.copy(), dr, rng, fs), snr, rng
                                    )
                                    rho, sign, lag = rho_and_sign(measured, unit, window)
                                    truth = -1 if inverted else 1
                                    yield {
                                        "kind": "same-system",
                                        "box": label,
                                        "fs": fs,
                                        "snr_db": snr,
                                        "dr_db": dr,
                                        "offset_ms": offset_ms,
                                        "gain_db": gain_db,
                                        "phase_deg": phase_deg,
                                        "truth": truth,
                                        "sign": sign,
                                        "rho": rho,
                                        "lag": lag,
                                        "correct": sign == truth,
                                    }


# Record Sec.3's table, built as real pairs. These are EXPECTED REFUSALS: across
# a crossover no time-domain sign is authoritative, so every row here is a cell
# rho must not be trusted on, and the point of measuring them is to see what rho
# actually does rather than to take the record's derivation on faith.
SECTION_3_ROWS = (
    ("BW", 1), ("BW", 2), ("BW", 3), ("BW", 4),
    ("BW", 5), ("BW", 6), ("BW", 7), ("BW", 8),
    ("LR", 2), ("LR", 4), ("LR", 6), ("LR", 8),
)


def crossover_cells(fs: float = 48000.0, fc: float = 100.0):
    length = int(0.25 * fs)
    window = int(0.05 * fs)
    for family, order in SECTION_3_ROWS:
        # An LR-N is the BW-(N/2) cascaded with itself.
        half = order // 2 if family == "LR" else order
        repeats = 2 if family == "LR" else 1
        low = scipy.signal.butter(half, fc, "low", fs=fs, output="sos")
        high = scipy.signal.butter(half, fc, "high", fs=fs, output="sos")
        lp = impulse_response(("sos", np.vstack([low] * repeats)), length)
        hp = impulse_response(("sos", np.vstack([high] * repeats)), length)
        rho, sign, lag = rho_and_sign(lp, hp, window)
        expected = int(round(math.cos(order * math.pi / 2.0)))
        yield {
            "kind": "crossover",
            "box": f"{family}{order}",
            "fs": fs,
            "snr_db": float("inf"),
            "dr_db": float("inf"),
            "offset_ms": 0.0,
            "gain_db": 0.0,
            "phase_deg": 0.0,
            "truth": 1,
            "sign": sign,
            "rho": rho,
            "lag": lag,
            "correct": sign == 1,
            "identity_sign_at_lag_0": expected,
        }


# --- reporting -------------------------------------------------------------


def quantiles(values):
    if not values:
        return "(none)"
    array = np.asarray(values)
    q = np.quantile(array, [0.0, 0.05, 0.5, 0.95, 1.0])
    return (
        f"n={len(values):5d}  min {q[0]:.4f}  p5 {q[1]:.4f}  "
        f"median {q[2]:.4f}  p95 {q[3]:.4f}  max {q[4]:.4f}"
    )


def report(cells, stream) -> None:
    same = [c for c in cells if c["kind"] == "same-system"]
    right = [c["rho"] for c in same if c["correct"]]
    wrong = [c["rho"] for c in same if not c["correct"]]

    print("=" * 78, file=stream)
    print("GRID A -- rho over same-system cells (the question rho MAY answer)", file=stream)
    print("=" * 78, file=stream)
    print(f"  correct sign: {quantiles(right)}", file=stream)
    print(f"  WRONG   sign: {quantiles(wrong)}", file=stream)
    print(file=stream)

    if wrong:
        candidate = max(wrong)
        kept = sum(1 for r in right if r > candidate)
        refused = len(right) - kept
        print(
            f"  Candidate threshold ON THIS GRID: rho > {candidate:.4f}\n"
            f"    -> zero wrong signs above it, and it REFUSES {refused} of {len(right)} "
            f"correct cells ({100.0 * refused / max(1, len(right)):.1f}%).",
            file=stream,
        )
        print(
            "    NOT ADOPTED. This is the floor of THIS grid. It ships only if\n"
            "    probe_rho_b.py, written from the record by a separate pass with no\n"
            "    sight of this file, lands on a compatible number.",
            file=stream,
        )
    else:
        print("  No wrong-sign cell on this grid -- so this grid sets no floor at all.", file=stream)

    crossings = [c for c in cells if c["kind"] == "crossover"]
    if crossings:
        print(file=stream)
        print("-" * 78, file=stream)
        print("Record Sec.3's rows as real pairs -- EXPECTED REFUSALS, not answers", file=stream)
        print("-" * 78, file=stream)
        print(f"  {'row':>6s} {'rho':>8s} {'sign':>6s} {'lag':>8s} {'identity@0':>11s}", file=stream)
        for c in crossings:
            print(
                f"  {c['box']:>6s} {c['rho']:8.4f} {c['sign']:6d} {c['lag']:8d} "
                f"{c['identity_sign_at_lag_0']:11d}",
                file=stream,
            )
        print(
            "\n  A sign column that disagrees with identity@0 is the whole point:\n"
            "  across a crossover rho's sign is not evidence about wiring.",
            file=stream,
        )


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(
        description="Survey A of rho. Prints distributions; adopts no threshold."
    )
    parser.add_argument("--quick", action="store_true",
                        help="subsample every axis -- a smoke test, not a survey")
    parser.add_argument("--seed", type=int, default=20260916,
                        help="RNG seed (default: 20260916)")
    parser.add_argument("--csv", type=str, default=None,
                        help="also write every cell to this CSV file")
    parser.add_argument("--no-crossover", action="store_true",
                        help="skip the Sec.3 cross-system rows")
    args = parser.parse_args(argv)

    cells = list(same_system_cells(args.quick, args.seed))
    if not args.no_crossover:
        cells.extend(crossover_cells())

    report(cells, sys.stdout)

    if args.csv:
        fields = sorted({k for c in cells for k in c})
        with io.open(args.csv, "w", encoding="utf-8", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=fields)
            writer.writeheader()
            writer.writerows(cells)
        print(f"\nwrote {len(cells)} cells to {args.csv}", file=sys.stdout)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
