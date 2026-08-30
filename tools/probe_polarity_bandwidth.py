#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Task 5 step 0: does the 2.5-octave polarity gate survive being measured on
the axis the CODE will actually gate on?

    .venv/Scripts/python.exe tools/probe_polarity_bandwidth.py

Writes nothing.

## Why this script had to exist before Polarity.cpp

`docs/dsp/2026-08-30-sweep-ir-l4a.md` decision 6 chose 2.5 octaves as the
minimum bandwidth for an ABSOLUTE polarity verdict. That boundary was read off
`verify_l4a_measurement.py` section L, whose `width` axis is the filter's
DESIGN bandwidth: it builds `lo = centre / 2**(w/2)`, `hi = centre * 2**(w/2)`,
so `log2(hi/lo) = w` by construction, at the -3 dB points of a Butterworth.

The implementation plan gates on a different quantity: the -10 dB width of the
recovered arrival's OWN spectrum. Several effects separate the two:

  1. a -10 dB crossing sits outside a -3 dB crossing on any finite skirt;
  2. the 50 ms analysis window truncates the ringing, smearing the spectrum;
  3. the recovered arrival carries the analysis pulse's spectrum, not only the
     filter's;
  4. against those, the sweep's own band limits and the peak-referenced
     threshold can push the measurement DOWN.

The first draft of this docstring claimed all the effects pushed the number up,
"all in the same direction". That is false, and this script's own output refutes
it: at nominal 2.5 octaves the measured value ranges from 1.32 oct (butter 4 at
40 Hz, where the 20 Hz sweep start clips the low skirt) to 3.05 oct. The two
axes differ; the SIGN of the difference is not uniform. Do not reason about one
from the other in either direction.

Calibrating a threshold on one axis and gating on the other is how a well
measured constant lands in the wrong place. The failure it produces is the one
decision 6 exists to prevent -- a CONFIDENT WRONG verdict on a narrowband
system, which sends an operator to rewire a working loudspeaker.

So this script measures the gating quantity directly, by transcribing the
planned C++ `arrivalBandwidthOctaves` into NumPy, and re-reads the boundary on
that axis. It also widens the survey to more filter families and orders, which
is the condition the owner attached when approving G21: decision 6's own text
concedes 2.5 was observed with "one filter family and one filter order".

## What the answer looks like, and the wrong way to ask it

The two errors here are NOT symmetric, and an early draft of this script treated
them as if they were. It required a gate to sit above every DISAGREEING
measurement *and below every AGREEING one*, and reported "the populations
overlap, no threshold works". That test is too strong. A gate above an agreeing
cell produces a FALSE REFUSAL -- a usability cost, and one with an exit, because
a refused operator is sent to the relative comparison. A gate below a
disagreeing cell produces a CONFIDENT WRONG ANSWER, which sends someone to
rewire a working loudspeaker. Only the second is a defect.

So the question this script actually asks is asymmetric: **what is the lowest
gate that admits NO wrong answer, and how many correct answers does it refuse to
buy that?** Both numbers are printed.

The reason the plan still cannot be built as written is not "the populations
overlap". It is that the widest disagreeing measurement climbs with filter order
with no sign of a ceiling -- 0.85 oct at order 2, 2.50 at order 4, 4.18 at
order 8 -- so any constant read off any grid is safe only until someone measures
a steeper filter, and a gate set high enough to be safe on THIS grid refuses
most ordinary loudspeakers anyway.

Filters are second-order sections throughout. Transfer-function (polynomial)
form is prohibited repo-wide by core/tests/check_no_polynomial_form.cmake; the
reason is in docs/dsp/2026-08-27-filterbank.md and it is not a style
preference. Note that the guard greps `tools/*.py` as well as `core/`, and its
patterns match a CALL SITE -- so naming the forbidden argument in prose, with
its quotes, trips it. This paragraph tripped it once. Describe the prohibition;
do not spell it.
"""

import numpy as np
import scipy.fft
import scipy.signal

from l4a_sweep import FS, sweep_and_inverse, linear_convolve

SEARCH_SECONDS = 0.05      # PolarityConfig::searchSeconds
ARRIVAL_FRACTION = 0.5     # PolarityConfig::arrivalFraction
MINUS_10_DB = 10.0 ** (-10.0 / 20.0)


def arrival_bandwidth_octaves(samples, origin, sample_rate=FS):
    """Transcribed from the planned C++ `arrivalBandwidthOctaves`.

    Any divergence from that function makes this whole survey measure a
    quantity the gate does not use, which is the exact mistake being corrected.
    Kept deliberately literal, including the DC skip and the ratio of BIN
    INDICES rather than of frequencies -- for a real FFT with a fixed size the
    two are identical, because the bin spacing cancels in the ratio, and
    transcribing the code's own expression is what makes this a check of the
    code rather than of an idea about the code.
    """
    last = min(origin + int(round(SEARCH_SECONDS * sample_rate)), len(samples))
    span = last - origin
    if span <= 0:
        return 0.0
    fft_size = 1 << int(np.ceil(np.log2(span)))
    padded = np.zeros(fft_size)
    padded[:span] = samples[origin:last]
    bins = np.abs(scipy.fft.rfft(padded))

    peak = bins.max()
    if peak <= 0.0:
        return 0.0
    above = np.nonzero(bins[1:] >= peak * MINUS_10_DB)[0] + 1   # skip DC
    if above.size == 0:
        return 0.0
    low_bin, high_bin = int(above[0]), int(above[-1])
    if low_bin == 0 or high_bin <= low_bin:
        return 0.0
    return float(np.log2(high_bin / low_bin))


def first_arrival_sign(samples, origin, sample_rate=FS):
    """The planned C++ rule: first index at or after the origin whose magnitude
    reaches `arrivalFraction` of the largest magnitude in the search window."""
    last = min(origin + int(round(SEARCH_SECONDS * sample_rate)), len(samples))
    window = samples[origin:last]
    largest = np.max(np.abs(window))
    if largest <= 0.0:
        return 0
    idx = int(np.argmax(np.abs(window) >= ARRIVAL_FRACTION * largest))
    return int(np.sign(window[idx]))


def band_sos(family, order, lo, hi):
    """Second-order sections only. See this file's docstring."""
    if family == "butter":
        return scipy.signal.butter(order, [lo, hi], "bandpass", fs=FS, output="sos")
    if family == "bessel":
        # A different phase law, which is the variable the whole decision turns
        # on: the sign of a band-limited arrival is set by the system's phase
        # response. A survey of one family is a survey of one phase law.
        return scipy.signal.bessel(order, [lo, hi], "bandpass", fs=FS,
                                   output="sos", norm="phase")
    if family == "cheby1":
        # 1 dB passband ripple. Real crossovers are not maximally flat.
        return scipy.signal.cheby1(order, 1.0, [lo, hi], "bandpass", fs=FS,
                                   output="sos")
    if family == "linkwitz":
        # Linkwitz-Riley is a Butterworth of half the order applied twice, so
        # its sections are Butterworth sections stacked -- still SOS.
        half = scipy.signal.butter(order // 2, [lo, hi], "bandpass", fs=FS,
                                   output="sos")
        return np.vstack([half, half])
    raise ValueError(f"unknown family {family!r}")


def cell(sweep, inverse, origin, family, order, centre, width, polarity):
    """One driven measurement: returns (measured octaves, sign agrees?)."""
    lo = centre / 2.0 ** (width / 2.0)
    hi = min(centre * 2.0 ** (width / 2.0), 0.49 * FS)
    sos = band_sos(family, order, lo, hi)
    driven = scipy.signal.sosfilt(sos, polarity * sweep)
    recovered = linear_convolve(driven, inverse)
    measured = arrival_bandwidth_octaves(recovered, origin)
    agrees = first_arrival_sign(recovered, origin) == polarity
    return measured, agrees


def main():
    print("Task 5 step 0 -- the polarity gate, measured on the axis the code gates on.")
    print(f"  arrival window {SEARCH_SECONDS * 1000:.0f} ms, "
          f"threshold -10 dB, arrival fraction {ARRIVAL_FRACTION}")

    sweep, inverse, _ = sweep_and_inverse(2.0, 20.0, 20000.0, 2.0, 0.5)
    origin = len(inverse) - 1

    families = [("butter", 2), ("butter", 4), ("butter", 8),
                ("bessel", 4), ("cheby1", 4), ("linkwitz", 4)]
    centres = [40.0, 60.0, 125.0, 250.0, 1000.0, 4000.0]
    widths = [0.5, 1.0, 1.5, 2.0, 2.5, 3.0, 4.0, 6.0, 8.0]

    agree_widths, disagree_widths = [], []
    disagree_rows = []

    for family, order in families:
        print(f"\n  {family} order {order} -- measured -10 dB octaves; "
              f"X = first-arrival sign DISAGREES with drive")
        header = "".join(f"{w:>9.1f}oct" for w in widths)
        print(f"   nominal ->{header}")
        for centre in centres:
            cells = []
            for width in widths:
                worst_measured, all_agree = None, True
                for polarity in (+1, -1):
                    measured, agrees = cell(sweep, inverse, origin, family,
                                            order, centre, width, polarity)
                    if not agrees:
                        all_agree = False
                        disagree_widths.append(measured)
                        disagree_rows.append((family, order, centre, width,
                                              polarity, measured))
                    else:
                        agree_widths.append(measured)
                    # Report the reading that most endangers the gate: for a
                    # disagreeing cell that is the WIDEST measurement, because
                    # a wide reading is what opens the gate on a cell that
                    # answers backwards.
                    if worst_measured is None:
                        worst_measured = measured
                    elif all_agree:
                        worst_measured = min(worst_measured, measured)
                    else:
                        worst_measured = max(worst_measured, measured)
                cells.append(f"{worst_measured:8.2f}{'  ' if all_agree else ' X'}")
            print(f"   {centre:6.0f} Hz" + "".join(cells))

    print("\n" + "=" * 78)
    print("The question this survey exists to answer")
    print("=" * 78)

    if not disagree_widths:
        print("  No cell disagreed anywhere in the grid. Either the grid does not")
        print("  reach narrow enough, or the sweep configuration differs from the")
        print("  one section L used. Do NOT read this as 'the gate is unnecessary'.")
        return

    worst_disagree = max(disagree_widths)
    best_agree = min(agree_widths) if agree_widths else float("nan")
    print(f"  measurements that AGREE   : {len(agree_widths):4d} cells, "
          f"narrowest {best_agree:.2f} oct, widest {max(agree_widths):.2f} oct")
    print(f"  measurements that DISAGREE: {len(disagree_widths):4d} cells, "
          f"narrowest {min(disagree_widths):.2f} oct, widest {worst_disagree:.2f} oct")
    print()
    # The two errors are not symmetric -- see this file's docstring. A gate
    # above an agreeing cell costs a false refusal; a gate below a disagreeing
    # one ships a confident wrong answer. So the figure of merit is the lowest
    # gate that admits NO wrong answer, and what it costs in refusals.
    safe_gate = worst_disagree
    refused = sum(1 for w in agree_widths if w <= safe_gate)
    admitted = len(agree_widths) - refused
    print(f"  Lowest gate admitting NO wrong answer on THIS grid: "
          f"{safe_gate:.2f} measured oct")
    print(f"    it admits {admitted} correct answers and falsely refuses "
          f"{refused} ({100.0 * refused / len(agree_widths):.0f}% of them).")
    print(f"    margin to the next disagreeing cell below it: see the list under.")
    print()
    print("  Every count in this file uses the FIRST-ARRIVAL rule at the")
    print("  fraction named above. The companion margin probe uses the")
    print("  PEAK-SIGN rule and its numbers do not transfer here; see its")
    print("  docstring. The shipped gate is the band-edge box of decision 6b,")
    print("  not this axis.")
    print()
    print("  That number is NOT a boundary to ship. The widest disagreeing")
    print("  measurement climbs with filter order and shows no ceiling on this")
    print("  grid, so any constant read off it is safe only until someone")
    print("  measures a steeper filter. Report it as what it is: the best value")
    print("  for the systems surveyed here, observed and not derived.")

    print("\n  Widest-reading cells that answered BACKWARDS -- each one is a system")
    print("  a gate set below its measurement would answer confidently and wrongly:")
    for family, order, centre, width, polarity, measured in sorted(
            disagree_rows, key=lambda r: -r[5])[:10]:
        print(f"    {family:9s} order {order}  centre {centre:7.0f} Hz  "
              f"nominal {width:4.1f} oct  drive {polarity:+d}  "
              f"MEASURED {measured:6.2f} oct")

    print("\n  Nominal vs measured, for the record: the two axes are not the same")
    print("  quantity, and the gap is the whole reason this script exists.")


if __name__ == "__main__":
    main()
