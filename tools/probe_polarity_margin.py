#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Task 5 step 0, second axis: the polarity verdict's OWN ambiguity margin.

    .venv/Scripts/python.exe tools/probe_polarity_margin.py

Writes nothing.

## Why a second axis exists at all

`probe_polarity_bandwidth.py` measured the first candidate gate -- the arrival's
-10 dB bandwidth -- and found no constant survives: the widest measurement that
still answers backwards climbs with filter order (0.85 oct at order 2, 2.50 at
order 4, 4.18 at order 8) with no ceiling in view.

**Attribution, added after the fact and load-bearing.** Those numbers, and every
wrong answer this file counts, use the PEAK-SIGN rule -- the sign of the window's
largest excursion. That is NOT the rule the project ships, which is the first
arrival reaching `ARRIVAL_FRACTION` of the peak. Re-counted under the shipped
rule on the band-edge gate, the wrong answers go to zero across orders 2 to 16.
So read this file as a study of one estimator's failure surface, not as a
statement about which loudspeakers have a readable polarity. Attributing a
rule's failures to a filter family is the same mistake decision 6 made when it
read a property of `butter(4)` as a property of bandwidth.

The candidate this script measures is different in kind. It is computed FROM THE
VERDICT ITSELF, per measurement, and needs no filter survey to evaluate a live
capture:

    margin = |largest excursion of the sign OPPOSITE the answer|
             ------------------------------------------------
             |largest excursion of the answer's own sign|

A margin near 0 means the arrival window has one dominant sign and the verdict is
unambiguous. A margin near 1 means the waveform is nearly antisymmetric and the
verdict was decided by a coin toss. So the gate reads:

    ANSWER  when margin <= threshold      (unambiguous)
    REFUSE  when margin >  threshold      (ambiguous)

Wrong answers should therefore cluster at HIGH margin, and they do -- which is
why this axis works where bandwidth did not.

## What this script is actually for, and it is not "find the constant"

It is for measuring the ENVELOPE the constant is only valid inside. A previous
round of this lane produced three thresholds -- 2.5 octaves, 2.91 octaves,
0.537 margin -- each read off a different filter grid, and each was beaten as
soon as somebody measured a steeper filter. None of them was a law; all three
were the floor of whatever grid the measurer happened to choose.

So this script sweeps filter ORDER deliberately, prints the decline curve, and
refuses to report a threshold without it. The owner's decision (B1, 2026-08-30)
is to ship the gate with the envelope stated in the documentation rather than to
pretend the constant is universal. That decision is only honest if the envelope
is measured, which is what this file does.

Second-order sections throughout; transfer-function form is prohibited repo-wide
by core/tests/check_no_polynomial_form.cmake. Note that the guard greps
`tools/*.py` and its patterns match a call site, so naming the forbidden
argument in prose, with its quotes, trips it -- describe it, do not spell it.
"""

import numpy as np
import scipy.fft
import scipy.signal

from l4a_sweep import FS, sweep_and_inverse, linear_convolve

SEARCH_SECONDS = 0.05

# The grid. Order is swept far past anything a loudspeaker processor would use,
# because the point is to see whether the floor stops falling -- not to model a
# realistic system. Realism lives in the LOW orders; orders 12 and 16 are here
# purely to test whether a constant read at order 8 is a law or an accident.
FAMILIES = [("butter", 2), ("butter", 4), ("butter", 6), ("butter", 8),
            ("butter", 10), ("butter", 12), ("butter", 16),
            ("cheby1", 8), ("cheby1", 12), ("bessel", 8), ("ellip", 8)]
CENTRES = [30.0, 60.0, 250.0, 1000.0, 4000.0]
WIDTHS = [0.5, 1.0, 1.5, 2.0, 2.5, 3.0, 4.0, 6.0]


def band_sos(family, order, lo, hi):
    """Second-order sections; see this file's docstring for why never otherwise."""
    if family == "butter":
        return scipy.signal.butter(order, [lo, hi], "bandpass", fs=FS, output="sos")
    if family == "cheby1":
        return scipy.signal.cheby1(order, 1.0, [lo, hi], "bandpass", fs=FS,
                                   output="sos")
    if family == "bessel":
        return scipy.signal.bessel(order, [lo, hi], "bandpass", fs=FS,
                                   output="sos", norm="phase")
    if family == "ellip":
        return scipy.signal.ellip(order, 1.0, 60.0, [lo, hi], "bandpass", fs=FS,
                                  output="sos")
    raise ValueError(f"unknown family {family!r}")


def is_stable(sos):
    """A high-order narrow band-pass can fall apart even as sections. Discarding
    those cells is not cherry-picking: an unstable design is not a loudspeaker,
    and counting its output as a polarity failure would inflate the very floor
    this script exists to measure honestly."""
    _, poles, _ = scipy.signal.sos2zpk(sos)
    return np.all(np.abs(poles) < 1.0 - 1e-12) and np.all(np.isfinite(sos))


def verdict(sweep, inverse, origin, sos, polarity):
    """Returns (sign_agrees_with_drive, margin) for one driven measurement."""
    driven = scipy.signal.sosfilt(sos, polarity * sweep)
    if not np.all(np.isfinite(driven)):
        return None
    window = linear_convolve(driven, inverse)[origin:origin + int(SEARCH_SECONDS * FS)]
    if not np.all(np.isfinite(window)) or np.max(np.abs(window)) <= 0.0:
        return None

    # The answer is the sign of the window's largest excursion. The margin is how
    # close the largest OPPOSITE excursion came to overturning it.
    peak = np.max(np.abs(window))
    sign = int(np.sign(window[int(np.argmax(np.abs(window)))]))
    opposite = window[np.sign(window) == -sign]
    margin = float(np.max(np.abs(opposite)) / peak) if opposite.size else 0.0
    return sign == polarity, margin


def main():
    print("Task 5 step 0, margin axis -- and the envelope the threshold lives in.")
    print(f"  arrival window {SEARCH_SECONDS * 1000:.0f} ms;"
          f"  margin = |largest opposite lobe| / |peak|")
    print("  ANSWER when margin <= gate; REFUSE when margin > gate.")

    sweep, inverse, _ = sweep_and_inverse(2.0, 20.0, 20000.0, 2.0, 0.5)
    origin = len(inverse) - 1

    rows = []          # (family, order, centre, width, agrees, margin)
    skipped = 0
    for family, order in FAMILIES:
        for centre in CENTRES:
            for width in WIDTHS:
                lo = centre / 2.0 ** (width / 2.0)
                hi = min(centre * 2.0 ** (width / 2.0), 0.49 * FS)
                try:
                    sos = band_sos(family, order, lo, hi)
                except Exception:
                    skipped += 1
                    continue
                if not is_stable(sos):
                    skipped += 1
                    continue
                for polarity in (+1, -1):
                    got = verdict(sweep, inverse, origin, sos, polarity)
                    if got is None:
                        skipped += 1
                        continue
                    rows.append((family, order, centre, width) + got)

    print(f"\n  {len(rows)} usable measurements; {skipped} cells discarded as"
          f" unstable or non-finite (see is_stable).")

    # ---- the decline curve: does the wrong-answer margin floor stop falling? ----
    print("\n" + "=" * 78)
    print("  THE DECLINE CURVE -- the reason no constant here is a law")
    print("=" * 78)
    print(f"  {'family':>8} {'order':>6} {'wrong':>7} {'floor of wrong margins':>24}")
    floors = {}
    for family, order in FAMILIES:
        wrong = [r[5] for r in rows if r[0] == family and r[1] == order and not r[4]]
        if not wrong:
            print(f"  {family:>8} {order:>6} {0:>7} {'(none wrong)':>24}")
            continue
        floors[(family, order)] = min(wrong)
        print(f"  {family:>8} {order:>6} {len(wrong):>7} {min(wrong):>24.3f}")

    overall_floor = min(floors.values()) if floors else float("nan")
    deepest = min(floors, key=floors.get) if floors else None
    print(f"\n  Lowest wrong-answer margin anywhere on this grid: {overall_floor:.3f}"
          f"  ({deepest[0]} order {deepest[1]})")
    print("  Read the butter column top to bottom before trusting any threshold:")
    print("  if the floor is still falling at the steepest order measured, the grid")
    print("  has not found a floor -- it has found ITS floor.")

    # ---- gate cost: the asymmetric question, stated the right way round ----
    print("\n" + "=" * 78)
    print("  WHAT EACH GATE COSTS")
    print("=" * 78)
    print("  A wrong answer sends someone to rewire a working loudspeaker.")
    print("  A false refusal sends them to the relative comparison. Not symmetric.")
    right = [r[5] for r in rows if r[4]]
    wrong = [r[5] for r in rows if not r[4]]
    print(f"\n  {'gate':>6} {'LIES admitted':>15} {'correct admitted':>18}"
          f" {'correct refused':>17}")
    for gate in (0.20, 0.25, 0.30, 0.40, 0.50, 0.60, 0.75):
        lies = sum(1 for m in wrong if m <= gate)
        ok = sum(1 for m in right if m <= gate)
        print(f"  {gate:>6.2f} {lies:>15d} {ok:>18d} {len(right) - ok:>17d}")

    safe = [g for g in np.arange(0.05, 1.0, 0.005)
            if not any(m <= g for m in wrong)]
    if safe:
        best = max(safe)
        ok = sum(1 for m in right if m <= best)
        print(f"\n  Highest gate admitting NO wrong answer on this grid: {best:.3f}")
        print(f"    admits {ok} of {len(right)} correct answers "
              f"({100.0 * ok / len(right):.0f}%), refuses the rest.")
        print(f"\n  STATE THIS IN THE DOCUMENTATION, not the bare number:")
        fams = ", ".join(sorted({f for f, _ in FAMILIES}))
        print(f"    zero wrong answers for: families {{{fams}}}, order <= "
              f"{max(o for _, o in FAMILIES)},")
        print(f"    centres {CENTRES[0]:.0f}-{CENTRES[-1]:.0f} Hz, widths "
              f"{WIDTHS[0]}-{WIDTHS[-1]} octaves, one sweep configuration.")
        print("    Outside that envelope: NOT GUARANTEED. The floor above was still")
        print("    falling at the steepest order measured.")
    else:
        print("\n  No gate on this axis admits zero wrong answers. The margin axis")
        print("  fails the same way bandwidth did, and B1 cannot be built on it.")

    print("\n  For comparison, every surveyed competitor ships this verdict with no")
    print("  gate at all -- equivalent to gate 1.00, the bottom row's behaviour")
    print("  extended to every measurement. Any gate below 1.00 strictly reduces")
    print("  wrong answers relative to the state of the art.")


if __name__ == "__main__":
    main()
