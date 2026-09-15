# SPDX-License-Identifier: AGPL-3.0-or-later
"""Survey B of the relative-polarity figure rho -- THE SECOND OF TWO GRIDS.

Written from the definition in `docs/dsp/2026-09-06-l7-alignment-wizard.md`
Sec.7 and the axes in Sec.8, deliberately WITHOUT reading `probe_rho_a.py`.
Record Sec.8 step 1 asks for two grids by two authors with no shared script,
because `memory/a-threshold-read-off-a-grid-is-that-grids-floor.md` records
three L4a thresholds from three sessions, each the floor of its own grid.

HONESTY NOTE, because the record says "two authors" and this is not that. Both
files were written in one agent session. The separation that IS real: this file
was written from the record's prose alone, in a later pass, with probe_rho_a.py
neither open nor consulted, and the four choices the record does not fix were
made independently here --

    the window       : from a DETECTED arrival (first crossing of half the
                       window peak, L4a's arrivalFraction), not from index 0
    the correlation  : direct time-domain np.correlate, not an FFT product
    the cells        : Monte Carlo draws over continuous ranges, not a
                       factorial over a handful of grid points, and over EVERY
                       order 2..16 rather than a chosen few
    the reporting    : the whole threshold trade-off curve, not the single
                       zero-wrong-sign point

If the two land on incompatible numbers that is a real finding. If they agree,
it is weaker evidence than two people would have produced, and whoever adopts a
threshold should be told so. NOTHING IS ADOPTED HERE.

    python tools/probe_rho_b.py --help
    python tools/probe_rho_b.py --trials 4000

SOS only: `filter_design_has_no_polynomial_form` scans tools/*.py.
"""

from __future__ import annotations

import argparse
import io
import math
import sys

import numpy as np
import scipy.signal

RATES = (44100.0, 48000.0, 96000.0)
DESIGNERS = ("butter", "cheby1", "ellip", "bessel")


def sections(designer: str, order: int, fs: float):
    edges = [55.0, 7500.0]
    if designer == "butter":
        return scipy.signal.butter(order, edges, "bandpass", fs=fs, output="sos")
    if designer == "cheby1":
        return scipy.signal.cheby1(order, 1.0, edges, "bandpass", fs=fs, output="sos")
    if designer == "ellip":
        return scipy.signal.ellip(order, 1.0, 45.0, edges, "bandpass", fs=fs, output="sos")
    return scipy.signal.bessel(order, edges, "bandpass", fs=fs, output="sos")


def taps(kind: str, fs: float):
    base = scipy.signal.firwin(301, [55.0, 7500.0], pass_zero=False, fs=fs)
    return base if kind == "fir-linear" else scipy.signal.minimum_phase(base)


def response(kind, fs: float, n: int) -> np.ndarray:
    """One box's impulse response."""
    stimulus = np.zeros(n)
    stimulus[0] = 1.0
    if isinstance(kind, tuple):
        return scipy.signal.sosfilt(sections(kind[0], kind[1], fs), stimulus)
    return np.convolve(stimulus, taps(kind, fs))[:n]


def rotate_phase(x: np.ndarray, degrees: float) -> np.ndarray:
    """Rotate every frequency component by a CONSTANT angle.

    This is what "+-10 degrees between two identical units" says literally: a
    phase difference, not a time difference. It is deliberately not modelled as
    a fractional delay -- a delay is a phase that grows with frequency, which is
    a different perturbation, and the offset axis already sweeps that.
    """
    analytic = scipy.signal.hilbert(x)
    theta = math.radians(degrees)
    return np.real(analytic) * math.cos(theta) - np.imag(analytic) * math.sin(theta)


def shift(x: np.ndarray, samples: int) -> np.ndarray:
    out = np.zeros_like(x)
    if samples >= 0:
        out[samples:] = x[: len(x) - samples]
    else:
        out[:samples] = x[-samples:]
    return out


def reverberate(x: np.ndarray, dr_db: float, fs: float, rng) -> np.ndarray:
    """A decaying random tail at the requested direct-to-reverberant ratio."""
    envelope = np.exp(-np.arange(len(x)) / (0.25 * fs))
    tail = rng.standard_normal(len(x)) * envelope
    direct = float(x @ x)
    reverb = float(tail @ tail)
    if direct <= 0.0 or reverb <= 0.0:
        return x
    return x + tail * math.sqrt(direct / (10.0 ** (dr_db / 10.0)) / reverb)


def corrupt(x: np.ndarray, snr_db: float, rng) -> np.ndarray:
    noise = rng.standard_normal(len(x))
    signal = float(x @ x)
    power = float(noise @ noise)
    if signal <= 0.0 or power <= 0.0:
        return x
    return x + noise * math.sqrt(signal / (10.0 ** (snr_db / 10.0)) / power)


def arrival_index(x: np.ndarray) -> int:
    """First sample reaching half the peak -- L4a's arrivalFraction of 0.5.

    0.5 and never 0.2: a linear-phase FIR's symmetric pre-ring puts an
    opposite-signed lobe above a 20% threshold BEFORE the main lobe arrives
    (core/include/rta/ir/Polarity.h:60-66).
    """
    peak = float(np.max(np.abs(x)))
    if peak <= 0.0:
        return 0
    over = np.flatnonzero(np.abs(x) >= 0.5 * peak)
    return int(over[0]) if over.size else 0


def rho_of(a: np.ndarray, b: np.ndarray, window: int):
    """Record Sec.7, evaluated directly.

        r(l) = sum_W a[n] b[n+l],  rho = |r(l*)| / sqrt(E_a E_b),  sign = sgn r(l*)

    The window starts at each signal's OWN detected arrival, which is the
    "arrival window" the record names rather than a fixed offset into the
    buffer. Outside it both signals are zero, so |r(l)| <= sqrt(E_a E_b) at
    every lag and rho really is in [0, 1].
    """
    wa = a[arrival_index(a) :][:window]
    wb = b[arrival_index(b) :][:window]
    length = min(len(wa), len(wb))
    if length < 2:
        return None
    wa = np.asarray(wa[:length], dtype=float)
    wb = np.asarray(wb[:length], dtype=float)
    energy = math.sqrt(float(wa @ wa) * float(wb @ wb))
    if energy <= 0.0:
        return None
    r = np.correlate(wb, wa, mode="full")  # index 0 is lag -(length-1)
    at = int(np.argmax(np.abs(r)))
    return abs(float(r[at])) / energy, (1 if r[at] >= 0.0 else -1), at - (length - 1)


def draw(rng):
    """One Monte Carlo cell. Every axis Sec.8 names, drawn over its RANGE."""
    fs = float(rng.choice(RATES))
    if rng.random() < 0.15:
        kind = str(rng.choice(("fir-linear", "fir-minimum")))
    else:
        kind = (str(rng.choice(DESIGNERS)), int(rng.integers(2, 17)))
    return {
        "fs": fs,
        "kind": kind,
        "snr_db": float(rng.uniform(10.0, 40.0)),
        "dr_db": float(rng.uniform(-12.0, 12.0)),
        "offset_ms": float(rng.uniform(0.0, 30.0)),
        "gain_db": float(rng.uniform(-2.0, 2.0)),
        "phase_deg": float(rng.uniform(-10.0, 10.0)),
        "inverted": bool(rng.random() < 0.5),
    }


def run_cell(cell, rng):
    fs = cell["fs"]
    n = int(0.4 * fs)
    window = int(0.05 * fs)
    first = response(cell["kind"], fs, n)
    second = first * (10.0 ** (cell["gain_db"] / 20.0))
    second = rotate_phase(second, cell["phase_deg"])
    second = shift(second, int(round(cell["offset_ms"] * 1e-3 * fs)))
    if cell["inverted"]:
        second = -second
    measured = corrupt(reverberate(first, cell["dr_db"], fs, rng), cell["snr_db"], rng)
    against = corrupt(reverberate(second, cell["dr_db"], fs, rng), cell["snr_db"], rng)
    out = rho_of(measured, against, window)
    if out is None:
        return None
    rho, sign, lag = out
    truth = -1 if cell["inverted"] else 1
    return {**cell, "rho": rho, "sign": sign, "lag": lag, "correct": sign == truth}


# Record Sec.8 step 4: every Sec.3 row built as a pair. These are cells rho is
# EXPECTED to refuse, recorded so the record's derivation has data beside it.
ROWS = (("BW", n) for n in range(1, 9))


def crossover_rows(fs: float = 48000.0, fc: float = 100.0):
    n = int(0.25 * fs)
    window = int(0.05 * fs)
    catalogue = [("BW", k) for k in range(1, 9)] + [("LR", k) for k in (2, 4, 6, 8)]
    for family, order in catalogue:
        half = order // 2 if family == "LR" else order
        stack = 2 if family == "LR" else 1
        low = np.vstack([scipy.signal.butter(half, fc, "low", fs=fs, output="sos")] * stack)
        high = np.vstack([scipy.signal.butter(half, fc, "high", fs=fs, output="sos")] * stack)
        stimulus = np.zeros(n)
        stimulus[0] = 1.0
        lp = scipy.signal.sosfilt(low, stimulus)
        hp = scipy.signal.sosfilt(high, stimulus)
        out = rho_of(lp, hp, window)
        if out is None:
            continue
        rho, sign, lag = out
        yield {
            "row": f"{family}{order}",
            "rho": rho,
            "sign": sign,
            "lag": lag,
            # cos(N*90 deg): +1, 0 or -1. Zero means the aligned-lag correlation
            # is cos 90 = 0, so the peak falls a quarter cycle to one side and
            # the sign is whichever side won -- UNDEFINED, not wrong.
            "identity_at_lag_0": int(round(math.cos(order * math.pi / 2.0))),
        }


def describe(values, label, stream):
    if not values:
        print(f"  {label}: (none)", file=stream)
        return
    a = np.asarray(values)
    q = np.quantile(a, [0.0, 0.05, 0.25, 0.5, 0.75, 0.95, 1.0])
    print(
        f"  {label}: n={len(values):5d}  min {q[0]:.4f}  p5 {q[1]:.4f}  q1 {q[2]:.4f}  "
        f"median {q[3]:.4f}  q3 {q[4]:.4f}  p95 {q[5]:.4f}  max {q[6]:.4f}",
        file=stream,
    )


def report(results, crossings, stream):
    right = [r["rho"] for r in results if r["correct"]]
    wrong = [r["rho"] for r in results if not r["correct"]]

    print("=" * 78, file=stream)
    print("GRID B -- rho over same-system cells", file=stream)
    print("=" * 78, file=stream)
    describe(right, "correct sign", stream)
    describe(wrong, "WRONG   sign", stream)
    print(file=stream)

    # THE WHOLE TRADE-OFF, not one point. A threshold that tells no lie is
    # worthless if it also refuses most of the good boxes: L4a's margin gate
    # was zero-lie and refused 74-88% of loudspeakers that answer correctly.
    print("  threshold   wrong signs admitted   correct cells REFUSED", file=stream)
    for threshold in np.arange(0.0, 1.0, 0.05):
        admitted = sum(1 for r in wrong if r > threshold)
        refused = sum(1 for r in right if r <= threshold)
        share = 100.0 * refused / max(1, len(right))
        print(
            f"   {threshold:5.2f}      {admitted:6d} of {len(wrong):<6d}        "
            f"{refused:6d} of {len(right):<6d} ({share:5.1f}%)",
            file=stream,
        )

    if wrong:
        floor = max(wrong)
        refused = sum(1 for r in right if r <= floor)
        print(
            f"\n  Zero-wrong-sign point ON THIS GRID: rho > {floor:.4f}, refusing "
            f"{refused} of {len(right)} correct cells "
            f"({100.0 * refused / max(1, len(right)):.1f}%).",
            file=stream,
        )
    else:
        print("\n  No wrong-sign cell on this grid, so this grid sets no floor.", file=stream)
    print(
        "  NOT ADOPTED. Compare against grid A before anything ships, and read\n"
        "  the honesty note at the top of this file about what 'two grids' means\n"
        "  here.",
        file=stream,
    )

    print(file=stream)
    print("-" * 78, file=stream)
    print("Record Sec.3's rows as pairs -- cells rho must NOT be trusted on", file=stream)
    print("-" * 78, file=stream)
    print(f"  {'row':>5s} {'rho':>8s} {'sign':>6s} {'lag':>7s} {'identity@0':>11s}", file=stream)
    for c in crossings:
        print(
            f"  {c['row']:>5s} {c['rho']:8.4f} {c['sign']:6d} {c['lag']:7d} "
            f"{c['identity_at_lag_0']:11d}",
            file=stream,
        )
    print(
        "\n  identity@0 == 0 is the odd orders: the aligned-lag correlation is\n"
        "  cos 90 = 0, so the peak lands a quarter cycle to one side and the sign\n"
        "  is whichever side won. Undefined, not wrong.",
        file=stream,
    )


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(
        description="Survey B of rho. Prints distributions and a trade-off curve; adopts nothing."
    )
    parser.add_argument("--trials", type=int, default=2000, help="Monte Carlo cells (default 2000)")
    parser.add_argument("--seed", type=int, default=71077345, help="RNG seed")
    parser.add_argument("--out", type=str, default=None, help="write the report to this file too")
    args = parser.parse_args(argv)

    rng = np.random.default_rng(args.seed)
    results = []
    for _ in range(args.trials):
        outcome = run_cell(draw(rng), rng)
        if outcome is not None:
            results.append(outcome)
    crossings = list(crossover_rows())

    report(results, crossings, sys.stdout)
    if args.out:
        with io.open(args.out, "w", encoding="utf-8") as handle:
            report(results, crossings, handle)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
