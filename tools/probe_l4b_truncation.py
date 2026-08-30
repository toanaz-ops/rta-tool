#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Step 0 for L4b, part two: truncation, and the axis part one was missing.

    .venv/Scripts/python.exe tools/probe_l4b_truncation.py

Writes nothing.

## What part one got wrong, and why it matters more than what it got right

`probe_l4b_filter_mode.py` reported T30 reading +13% to +73% long with a 55 dB
SNR fixture and no truncation, and concluded that missing truncation dominates
the choice of filter mode. The ORDERING survives. The RANGE does not, and the
reason is a variable that probe never held still.

It sized its buffer as `max(2.0, 2.5*T60)` seconds. In units of T60 that is
5.0x at T60 = 0.4 s and 2.5x everywhere else, so its rows were never comparable
to each other. Holding the tail length fixed instead, on the same fixture and
the same seed:

    T60=0.4  buffer 2.5xT60   T30 err   +21.2%
    T60=0.4  buffer 5.0xT60   T30 err   +28.0%
    T60=0.4  buffer 7.5xT60   T30 err  +943.2%
    T60=0.8  buffer 7.5xT60   T30 err  +571.9%
    T60=1.2  buffer 7.5xT60   T30 err  +525.8%

The error is not a property of "55 dB SNR without truncation". It is a property
of **how much noise-only tail sits inside the buffer**, because the Schroeder
integral accumulates every sample of it into the plateau the fit then reads.

**So no truncation-bias figure may be quoted without its (SNR, tail length,
band) alongside.** A bare percentage range in a record is a number that will be
re-used in a regime where it is false.

## The two capture-length rules are ONE decision

L4a decision 9 fixed capture length from below: the capture must outlast the
sweep by at least 1.5x the reverberation time, or the decay is cut off before it
finishes and the answer is short. The table above is the same quantity from
above: every noise-only sample past the decay is energy the backward integral
adds to the plateau, and T30 reads long.

Written in two separate places, a later session optimises one end and breaks the
other. They belong in one section of the record, stated as a two-sided bound
whose upper side is REMOVED by truncation rather than by shortening the capture
-- which is why Lundeby is the main event and not a refinement.

## Lundeby here is DERIVED, not transcribed

pyrato's `intersection_time_lundeby` is the most complete reference available,
and it is not the source used here. Two reasons, and the second is the one that
matters. First, a public issue against that function reports a broken
convergence check -- and the reviewing session then measured current `main` and
found convergence working, with a dead variable left behind, so the issue itself
had gone stale: a public bug report has a date, and code must be read at a
commit. Second and more important: transcribing an implementation means
inheriting whichever of its choices were accidents, and this lane cannot then
tell an accident from a decision. So the steps below are written from the
algorithm's description and the constants are named and defended here; pyrato is
consulted afterwards, as a check, not as a source.
"""

from __future__ import annotations

import numpy as np
from scipy import signal

FS = 48_000.0
SEEDS = 24  # ensemble size: one realisation is not a measurement (part one, finding 3)


# ---------------------------------------------------------------- fixture


def make_decay(t60: float, fs: float, tail_multiple: float, seed: int,
               snr_db: float, lead_in_sec: float = 0.20):
    """Synthetic IR with a known T60. Returns `(h, origin)`.

    `tail_multiple` is the buffer length in units of T60 -- an ARGUMENT rather
    than a derived convenience, because part one derived it and thereby hid it.

    Noise is specified as SNR against this fixture's own direct sound, never as
    an absolute amplitude: an absolute figure makes the SNR depend on whatever
    peak the signal happens to have, so two pipelines generating "the same"
    fixture can sit 6 dB apart, which moves the truncation point, which can
    change whether the answer is a number or a refusal.

    The lead-in is not cosmetic. A zero-phase filter is non-causal and pads by
    odd extension; around a direct sound at sample 0 that fabricates 38.7 dB of
    energy exactly where EDT reads. Measured in part one.
    """
    rng = np.random.default_rng(seed)
    origin = int(round(lead_in_sec * fs))
    n_tail = int(round(tail_multiple * t60 * fs))
    n = origin + n_tail

    h = np.zeros(n)
    t = np.arange(n_tail) / fs
    tau = t60 / (3.0 * np.log(10.0))
    tail = rng.standard_normal(n_tail) * np.exp(-t / tau)
    tail[: int(round(0.006 * fs))] = 0.0

    h[origin:] = tail
    h[origin] = 1.0
    h += rng.standard_normal(n) * (abs(h[origin]) * 10.0 ** (-snr_db / 20.0))
    return h, origin


def octave_sos(centre: float, fs: float, order: int = 8):
    lo, hi = centre / np.sqrt(2.0), centre * np.sqrt(2.0)
    return signal.butter(order // 2, [lo / (fs / 2), hi / (fs / 2)],
                         btype="bandpass", output="sos")


# ---------------------------------------------------------------- Lundeby


def lundeby_crosspoint(sq: np.ndarray, fs: float,
                       intervals_per_10db: float = 5.0,
                       db_above_noise: float = 10.0,
                       fit_range_db: float = 20.0,
                       noise_margin_db: float = 10.0,
                       max_iter: int = 30,
                       tol_sec: float = 0.01) -> tuple[int, float] | None:
    """Index where the decay meets the noise floor, and the noise power there.

    Returns None when the geometry never forms -- no decaying fit, or the
    crossing falls outside the record. A refusal, not a fallback: a crosspoint
    invented for a response that never decayed is the failure this whole probe
    exists to avoid.

    ## The constants, and why each is what it is

    `intervals_per_10dB = 5` sets the smoothing window from the decay rate
    itself rather than from a fixed millisecond figure. A fixed window cannot be
    right at both ends -- 30 ms is a third of the decay of a dry room and a
    twentieth of a hall's -- and this is the same defect, in a different
    quantity, that L4a measured in its fixed 50 ms polarity window, which read a
    50-71 Hz subwoofer as 46.9-18270 Hz.

    `db_above_noise = 10` keeps the regression clear of the knee. Fitting into
    the curved region where decay and noise are comparable pulls the slope flat,
    which reads as a longer T60 -- the error is one-sided, so it does not
    average out over bands.

    `fit_range_db = 20` is the span the late-decay fit uses. It has to be wide
    enough that slope error is small and narrow enough to stay above the knee;
    20 dB below a 10 dB standoff needs 30 dB of usable range, which is where
    T30's own dynamic-range requirement comes from.

    `tol_sec = 0.01` and `max_iter = 30` bound the loop. Convergence is checked
    against the PREVIOUS crossing point each pass -- stated explicitly because
    the best-known reference implementation shipped a version where that
    variable was assigned once and never updated, so the check could not fire.
    """
    n = sq.size

    def smooth(win_samples: int):
        w = max(1, int(win_samples))
        cut = (n // w) * w
        if cut < 2 * w:
            return None, None
        blocks = sq[:cut].reshape(-1, w).mean(axis=1)
        centres = (np.arange(blocks.size) + 0.5) * w
        return blocks, centres

    # Step 1: a first smoothing at a fixed window, only to get a first slope.
    blocks, centres = smooth(0.030 * fs)
    if blocks is None:
        return None

    # Step 2: first noise estimate from the last 10% of the record.
    noise = float(np.mean(sq[int(0.9 * n):]))
    if noise <= 0.0:
        return None

    crossing = None
    for _ in range(max_iter):
        db = 10.0 * np.log10(np.maximum(blocks, np.finfo(float).tiny))
        noise_db = 10.0 * np.log10(noise)

        usable = np.where(db > noise_db + db_above_noise)[0]
        if usable.size < 2:
            return None
        i1 = usable[-1]
        top = db[0]
        within = np.where((db <= top) & (db >= top - fit_range_db))[0]
        within = within[within <= i1]
        if within.size < 2:
            return None

        slope, intercept = np.polyfit(centres[within], db[within], 1)
        if slope >= 0.0:
            return None

        new_crossing = (noise_db - intercept) / slope
        if crossing is not None and abs(new_crossing - crossing) / fs < tol_sec:
            crossing = new_crossing
            break
        crossing = new_crossing

        # The decay rate, used both to size the smoothing window and to place
        # the noise-estimate margin. One quantity, one place it is derived.
        db_per_sample = -slope
        if db_per_sample <= 0.0:
            return None

        # Step 5: resize the smoothing window from the slope just measured, so
        # the window is a fixed number of intervals per 10 dB of decay.
        win = (10.0 / db_per_sample) / intervals_per_10db
        blocks, centres = smooth(win)
        if blocks is None:
            return None

        # Step 7: re-estimate the noise from beyond the crossing point.
        #
        # The margin past the crossing is expressed in DECIBELS OF DECAY and
        # converted to samples through the slope just measured -- never as a
        # fraction of the buffer. A buffer-fraction margin would tie the noise
        # estimate to the record's length, which is the exact axis this whole
        # probe exists to remove: the answer would then depend on how long the
        # operator left the recorder running, one level down from where that
        # dependency was just eliminated. An earlier draft of this function used
        # `0.10 * n` and was harmless only because the fixture's noise is
        # stationary; the reviewing session caught it before it reached C++.
        margin_samples = noise_margin_db / db_per_sample
        start = int(min(n - 1, max(0, new_crossing + margin_samples)))
        if n - start < 16:
            # Not enough record past the crossing to re-estimate from. Refuse
            # rather than fall back to a fraction of the buffer -- a fallback
            # here would silently reintroduce the dependency just removed.
            return None
        noise = float(np.mean(sq[start:]))
        if noise <= 0.0:
            return None

    if crossing is None or not (0 < crossing < n):
        return None
    return int(crossing), noise


def edc_db(x: np.ndarray, origin: int, truncate: bool, fs: float) -> np.ndarray | None:
    """Backward-integrated decay curve, optionally truncated at Lundeby's point.

    With `truncate`, the integral stops at the crossing and a correction is
    added for the energy beyond it, assuming the late decay continues as a
    single exponential. Without the correction the curve bends down at the end
    and the fit reads short -- swapping one bias for another.
    """
    sq = x[origin:] ** 2
    if not truncate:
        e = np.cumsum(sq[::-1])[::-1]
    else:
        found = lundeby_crosspoint(sq, fs)
        if found is None:
            return None
        idx, noise = found
        head = sq[:idx]
        if head.size < 16:
            return None
        e = np.cumsum(head[::-1])[::-1]

        # Lundeby's correction for the energy between the crossing point and
        # infinity, under the assumption that the late decay continues as a
        # SINGLE exponential. If power decays as p(t) = p_i * exp(-t/tau'),
        # the remaining energy is exactly p_i * tau'; with the slope measured
        # in dB per sample, tau' in samples is -10/(slope*ln 10). So the whole
        # correction is one product, and it is added as a constant to every
        # point of the curve because every point's integral is missing the
        # same tail.
        #
        # The assumption is the load-bearing part, not the algebra. A room with
        # two decay rates -- a coupled space, a hard rear wall -- has a tail
        # this term underestimates, and the resulting curve bends. That is why
        # the record must say the correction assumes one exponential rather
        # than presenting it as a neutral tidy-up.
        db = 10.0 * np.log10(np.maximum(e, np.finfo(float).tiny))
        span = min(db.size - 1, max(1, db.size // 2))
        slope_db_per_sample = (db[span] - db[0]) / span
        if slope_db_per_sample < 0.0:
            tau_samples = -10.0 / (slope_db_per_sample * np.log(10.0))
            e = e + float(head[-1]) * tau_samples
    e = np.maximum(e, np.finfo(float).tiny)
    return 10.0 * np.log10(e / e[0])


def decay_time(curve: np.ndarray, fs: float, upper: float, lower: float):
    if curve is None or curve[-1] > lower:
        return None
    i0 = int(np.argmax(curve <= upper))
    i1 = int(np.argmax(curve <= lower))
    if i1 <= i0 + 8:
        return None
    t = np.arange(i0, i1) / fs
    slope, _ = np.polyfit(t, curve[i0:i1], 1)
    return None if slope >= 0.0 else -60.0 / slope


# ---------------------------------------------------------------- report


def ensemble(t60, band, tail_mult, snr, mode, truncate, param, third=False):
    """Median error and inter-quartile spread over SEEDS realisations.

    Median, not mean: a single realisation that refuses or lands far out should
    not drag the summary, and the question being asked is what a typical
    measurement does, not what the average of typical and pathological does.
    The spread is reported because a bias smaller than the scatter is not a
    bias anyone can act on.
    """
    upper, lower = param
    sos = (third_sos(band) if third else octave_sos(band, FS)) if band else None
    errs = []
    refused = 0
    for s in range(SEEDS):
        h, o = make_decay(t60, FS, tail_mult, 20260830 + s, snr)
        if sos is not None:
            if mode == "forward":
                h = signal.sosfilt(sos, h)
            elif mode == "zero-phase":
                h = signal.sosfiltfilt(sos, h)
            else:
                h = signal.sosfilt(sos, h[::-1])[::-1]
        v = decay_time(edc_db(h, o, truncate, FS), FS, upper, lower)
        if v is None:
            refused += 1
        else:
            errs.append(100.0 * (v - t60) / t60)
    if not errs:
        return None, None, refused
    q1, med, q3 = np.percentile(errs, [25, 50, 75])
    return med, q3 - q1, refused


T30 = (-5.0, -35.0)
T20 = (-5.0, -25.0)
EDT = (0.0, -10.0)
MODES = ["forward", "zero-phase", "time-reversed"]


def mode_table(snr: float, tail_mult: float) -> None:
    """The filter-mode comparison, run only now that truncation is in place.

    Part one ran this comparison with no truncation and found the three modes
    agreeing to within about 2 %. That agreement was not evidence they are
    equivalent: it was two small numbers being compared underneath a large one.
    With the tail-length term removed, whatever separates the modes is no longer
    hiding under a factor of thirty.

    EDT is included because it is the parameter REW's documentation singles out:
    time-reversed filtering "greatly reduces the filter's own contribution", but
    "EDT figures using Time-Reversed filters may not be valid". That is a claim
    with a direction, so it can be checked rather than believed.
    """
    print(f"=== filter modes WITH Lundeby truncation, SNR {snr:.0f} dB, "
          f"tail {tail_mult:.1f}xT60 " + "=" * 12)
    print(f"{'T60':>5} {'band':>6} {'param':>5} "
          + "".join(f"{m:>20}" for m in MODES))
    for t60 in (0.4, 1.2):
        for band in (125.0, 1000.0):
            for name, param in (("EDT", EDT), ("T20", T20), ("T30", T30)):
                cells = []
                for m in MODES:
                    med, iqr, ref = ensemble(t60, band, tail_mult, snr,
                                             m, True, param)
                    if med is None:
                        cells.append(f"{'ref ' + str(ref):>20}")
                    else:
                        tag = f"{med:+6.1f}% (IQR {iqr:4.1f})"
                        if ref:
                            tag += f" r{ref}"
                        cells.append(f"{tag:>20}")
                print(f"{t60:5.1f} {band:6.0f} {name:>5} " + "".join(cells))
    print()


def third_sos(centre: float, fs: float = FS):
    f = 2.0 ** (1.0 / 6.0)
    return signal.butter(4, [(centre / f) / (fs / 2), (centre * f) / (fs / 2)],
                         btype="bandpass", output="sos")


def filter_own_t60(sos, fs: float = FS) -> float:
    """T60 of the band-pass filter's OWN impulse response, measured its own way.

    This is the number that decides whether the filtering mode matters at all,
    and it is measured by the same T20 fit used on rooms rather than taken from
    a bandwidth formula -- so that the comparison below is between two
    quantities produced by one procedure.
    """
    x = np.zeros(int(fs))
    x[0] = 1.0
    y = signal.sosfilt(sos, x)
    e = np.cumsum(y[::-1] ** 2)[::-1]
    db = 10.0 * np.log10(np.maximum(e, np.finfo(float).tiny) / e[0])
    i0, i1 = int(np.argmax(db <= -5)), int(np.argmax(db <= -25))
    if i1 <= i0:
        return float("nan")
    slope, _ = np.polyfit(np.arange(i0, i1) / fs, db[i0:i1], 1)
    return -60.0 / slope


def ring_table() -> None:
    """Why the octave-band mode comparison above found nothing.

    A band-pass filter has its own decay, and it adds to the room's. Whether
    that matters is not a question about frequency, it is a question about the
    RATIO of the filter's decay to the room's -- and the grid above never got
    that ratio above about 0.28, which is why all three modes agreed there.
    """
    print("=== filter's own T60 vs the room's -- the ratio that decides =========")
    print(f"{'band':>14} {'BW (Hz)':>9} {'filter T60':>12}"
          + "".join(f"{'/ ' + str(r) + 's':>10}" for r in (0.3, 0.4, 1.2)))
    for label, centre, frac in (("octave 125", 125.0, 1), ("octave 1000", 1000.0, 1),
                                ("1/3-oct 125", 125.0, 3), ("1/3-oct 63", 63.0, 3),
                                ("1/3-oct 40", 40.0, 3)):
        f = 2.0 ** (1.0 / (2 * frac))
        lo, hi = centre / f, centre * f
        sos = (octave_sos(centre, FS) if frac == 1 else third_sos(centre))
        r = filter_own_t60(sos)
        cells = "".join(f"{r / room:9.2f} " for room in (0.3, 0.4, 1.2))
        print(f"{label:>14} {hi - lo:9.1f} {r * 1000:10.1f}ms " + cells)
    print()


def regime_table() -> None:
    """The mode comparison run where the ratio is large enough to matter.

    REW documents that time-reversed filtering "greatly reduces the filter's own
    contribution" but that "EDT figures using Time-Reversed filters may not be
    valid". Both halves are directional claims, so both can be checked.

    ## Read the refusal counts before the percentages

    At ratio 0.93, forward filtering refuses on all 24 seeds. That is not a
    failure of the probe; it is the answer. The fitted late slope is so shallow
    -- because it is largely the filter's own decay, not the room's -- that the
    10 dB noise margin no longer fits inside the record, and the estimator
    declines rather than inventing a crossing. The other two modes return
    numbers on most seeds.

    ## An honest note about how much this depends on the estimator

    An earlier draft placed the noise-estimate margin at a fixed fraction of the
    buffer instead of a fixed amount of decay. Under that draft this same cell
    returned +129.1 % rather than refusing. The change from "confidently wrong"
    to "declines to answer" came from one line of the estimator, not from any
    change to the physics.

    Two things follow, and the record must carry both. First, the ranking
    between zero-phase and time-reversed is NOT established here: their medians
    differ by less than the inter-quartile spread, so the honest statement is
    about FORWARD versus the other two. Second, a refusal produced by an
    estimator limit is not the same claim as a refusal produced by physics, and
    conflating them invites a later session to "improve the estimator" into a
    regime where the variance never allowed an answer.
    """
    print("=== filter modes where ring/decay is LARGE (1/3-octave), SNR 55 dB ===")
    print(f"{'T60':>5} {'band':>6} {'ratio':>6} {'param':>4} "
          + "".join(f"{m:>20}" for m in MODES))
    for t60, centre in ((0.4, 63.0), (0.4, 125.0), (1.2, 63.0)):
        ratio = filter_own_t60(third_sos(centre)) / t60
        for name, param in (("EDT", EDT), ("T30", T30)):
            cells = []
            for m in MODES:
                med, iqr, ref = ensemble(t60, centre, 2.5, 55.0, m, True, param,
                                         third=True)
                if med is None:
                    cells.append(f"{'ref ' + str(ref):>20}")
                else:
                    tag = f"{med:+6.1f}% (IQR {iqr:5.1f})"
                    if ref:
                        tag += f" r{ref}"
                    cells.append(f"{tag:>20}")
            print(f"{t60:5.1f} {centre:6.0f} {ratio:6.2f} {name:>4} " + "".join(cells))
    print()


def main() -> None:
    print("Step 0 part two: truncation, with tail length as an explicit axis.")
    print(f"fs = {FS:.0f} Hz, ensemble of {SEEDS} seeds, octave band-pass order 8.")
    print("Cells are MEDIAN T30 error % (IQR), or 'ref N' when N seeds refused.")
    print()
    print("Tail length is in units of T60. Part one varied it accidentally")
    print("between rows and read the result as a filter-mode effect.")
    print()

    for snr in (45.0, 55.0):
        print(f"=== SNR {snr:.0f} dB below the direct sound " + "=" * 30)
        print(f"{'T60':>6} {'band':>7} {'tail':>6} "
              f"{'no truncation':>22} {'Lundeby truncation':>22}")
        for t60 in (0.4, 1.2):
            for band in (125.0, 1000.0):
                for mult in (2.5, 5.0, 7.5):
                    cells = []
                    for trunc in (False, True):
                        med, iqr, ref = ensemble(t60, band, mult, snr,
                                                 "forward", trunc, T30)
                        if med is None:
                            cells.append(f"{'ref ' + str(ref):>22}")
                        else:
                            tag = f"{med:+7.1f}% (IQR {iqr:5.1f})"
                            if ref:
                                tag += f" r{ref}"
                            cells.append(f"{tag:>22}")
                    print(f"{t60:6.1f} {band:7.0f} {mult:5.1f}x " + "".join(cells))
        print()

    mode_table(55.0, 2.5)
    mode_table(45.0, 2.5)
    ring_table()
    regime_table()


if __name__ == "__main__":
    main()
