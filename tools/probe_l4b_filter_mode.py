#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Step 0 for L4b: which filtering mode, and what each one costs per parameter.

    .venv/Scripts/python.exe tools/probe_l4b_filter_mode.py

Writes nothing.

## The question this file exists to settle

Every reference implementation band-pass filters the impulse response before
Schroeder integration. They do not agree on HOW, and REW's own documentation
states the trade-off as a contradiction rather than a preference:

  - forward filtering (REW default): no mitigation at all.
  - time-reversed: "greatly reduces the filter's own contribution to the
    measured decay", but "EDT figures using Time-Reversed filters may not be
    valid".
  - zero-phase (filtfilt): reduces the contribution "similar to (though not as
    much as) time reversed filters but without significantly affecting early
    decay time".

If that is true, there is NO single mode that serves both T30 and EDT, and L4b
must either ship two filter paths or state which parameter it is biasing and by
how much. That is an architecture decision, so it gets measured before the
record names a default -- the L4a lane had its record refuted by the survey the
record itself mandated, and the cheapest way to not repeat that is to survey
first.

## Why the ground truth is synthetic and exactly known

A measured room has no true RT60 to compare against, so a measured room cannot
say which mode is biased. Here the decay is CONSTRUCTED: band-limited noise
multiplied by exp(-t/tau), with tau chosen from the target T60 by

    h(t) = n(t) * exp(-t/tau),   energy ~ exp(-2t/tau),
    60 dB of energy decay  =>  T60 = 3*ln(10)*tau

so every deviation the probe prints is filter behaviour, not room behaviour.

## Trap #18 is load-bearing here

L4a learned that a symmetric fixture makes distinct structures produce identical
numbers -- no test caught a duplicated fade layer because every fixture used
fade-in = fade-out. The same hazard applies to a filter mode: a decay that is
already a clean exponential from sample zero gives forward, zero-phase and
time-reversed filtering the same answer, because there is no early transient for
the filter's ring to sit on top of.

So the fixture carries a DIRECT SOUND and a delayed onset: a unit spike, a gap,
then the decaying tail. That asymmetry in time is what the three modes disagree
about, and a probe without it would report a comfortable and meaningless
agreement between all three.

## WHAT THIS PROBE DOES NOT YET ESTABLISH -- read before quoting a number

1. **ONE random realisation is not a measurement.** Every table below runs a
   single seed. EDT reads over the first 10 dB, which on a noise tail is a
   handful of samples, so its scatter between seeds is large and is NOT
   separated here from bias between modes. Measured symptom: noise-free at
   125 Hz, EDT reads -0.5 / -11.6 / -14.2 % at T60 = 0.4 s but +29.5 / +37.9 /
   +32.8 % at T60 = 1.2 s -- a sign flip no filter mechanism explains. That is
   realisation scatter wearing a bias costume. **An ensemble over seeds is
   required before any per-mode figure here is quoted as bias.**

2. **There is no truncation.** The Schroeder integral runs to the end of the
   record, so the noise floor's own energy is integrated into the tail. With
   the 55 dB SNR fixture, T30 reads +13 % to +73 % too long while all three
   filter modes agree with each other to within about 2 %. The ordering that
   falls out of that is worth more than any single cell: **the missing
   truncation dominates the filter mode by an order of magnitude.** Lundeby is
   therefore not a refinement to add later, it is the main event -- and a
   comparison of filter modes run without truncation is comparing two small
   numbers underneath a large one.

3. **The bias figures reported in the literature (60-100 % for T60 =
   0.4-1.2 s) are NOT reproduced here** and must not be cited as if they were.

So: this file has settled the lead-in question and the ordering question. It has
NOT settled which filter mode L4b should default to.
"""

from __future__ import annotations

import numpy as np
from scipy import signal

FS = 48_000.0
RNG_SEED = 20260830  # fixed: a probe whose numbers move between runs is not evidence


LEAD_IN_SEC = 0.20   # see make_decay: not cosmetic, worth 38.7 dB
SNR_DB = 55.0        # noise floor, relative to the DIRECT SOUND of this same fixture


def make_decay(t60: float, fs: float, seconds: float, seed: int,
               lead_in_sec: float = LEAD_IN_SEC,
               snr_db: float | None = SNR_DB) -> tuple[np.ndarray, int]:
    """A synthetic room impulse response whose T60 is known by construction.

    Returns `(h, origin)` where `origin` is the index of the direct sound.

    ## The lead-in is worth 38.7 dB, and the first version of this probe lost it

    Written first with the direct sound at sample 0, this fixture reported
    zero-phase filtering as catastrophically wrong at 125 Hz -- EDT off by
    -57.8%, -54.3%, -42.2% at three different T60 values. That flatly
    contradicts REW's documented claim that zero-phase filtering does not
    significantly affect EDT, which should have been the first clue.

    It was not zero-phase filtering. `sosfiltfilt` is NON-CAUSAL and pads the
    signal at both edges (odd extension by default). An odd extension around a
    unit spike at sample 0 reflects to `2*x[0] - x[::-1]`, i.e. a step of
    twice the direct sound, and the filter rings on THAT. Measured directly:

        filtfilt(spike at sample 0)      total energy  24.70
        filtfilt(spike at 200 ms)        total energy   0.0033
        ratio                                        +38.7 dB

    Thirty-eight decibels of energy that no room produced, sitting exactly where
    EDT reads. The fixture was measuring its own left edge.

    This is not a probe-only curiosity. **Trimming an impulse response to start
    at the direct sound is the ordinary thing to do**, and any implementation
    that then applies zero-phase filtering inherits this. L4b must either keep
    pre-arrival samples or pad explicitly -- the same conclusion L4a reached for
    a different reason in `IrSpectrum.h` ("the window starts BEFORE t = 0, and
    that is not a detail"), arrived at from the opposite direction.

    ## Trap #18 is load-bearing here

    A decay that is a clean exponential from its first sample gives all three
    filter modes the same answer, because there is no early transient for the
    filter's ring to sit on. So the fixture carries a direct sound, a 6 ms gap,
    and only then the tail.

    ## The noise floor is specified in SNR, never in absolute amplitude

    Per the fixture rule this lane adopted: noise is set relative to the direct
    sound OF THIS SAME FIXTURE. An absolute amplitude would make the SNR depend
    on whatever peak the signal happened to have, which is how two pipelines
    generating "the same" fixture end up 6 dB apart -- and a shifted noise floor
    moves the truncation point, which changes T30, and can change whether the
    answer is a number or a refusal at all.
    """
    rng = np.random.default_rng(seed)
    origin = int(round(lead_in_sec * fs))
    n = origin + int(round(seconds * fs))

    h = np.zeros(n)
    t = np.arange(n - origin) / fs
    tau = t60 / (3.0 * np.log(10.0))
    tail = rng.standard_normal(n - origin) * np.exp(-t / tau)

    gap = int(round(0.006 * fs))
    tail[:gap] = 0.0

    h[origin:] = tail
    h[origin] = 1.0  # direct sound

    if snr_db is not None:
        # Relative to the direct sound, which is 1.0 by construction here --
        # written as a ratio anyway so the rule survives a change of scale.
        noise_rms = abs(h[origin]) * 10.0 ** (-snr_db / 20.0)
        h = h + rng.standard_normal(n) * noise_rms

    return h, origin


def octave_sos(centre: float, fs: float, order: int = 8):
    """IEC 61260 octave band as an SOS band-pass, the shape every reference uses."""
    lo = centre / np.sqrt(2.0)
    hi = centre * np.sqrt(2.0)
    return signal.butter(order // 2, [lo / (fs / 2), hi / (fs / 2)],
                         btype="bandpass", output="sos")


def filter_mode(h: np.ndarray, sos, mode: str) -> np.ndarray:
    if mode == "forward":
        return signal.sosfilt(sos, h)
    if mode == "zero-phase":
        return signal.sosfiltfilt(sos, h)
    if mode == "time-reversed":
        # Filter the reversed signal, then reverse back. The filter's own decay
        # then trails off into NEGATIVE time relative to the room's decay, which
        # is the whole point -- it stops the filter's ring adding to the tail.
        return signal.sosfilt(sos, h[::-1])[::-1]
    raise ValueError(mode)


def schroeder_db(x: np.ndarray, origin: int) -> np.ndarray:
    """Backward-integrated energy decay curve from `origin`, 0 dB at the start.

    Integration begins at the direct sound, not at sample 0: the lead-in exists
    so the filters have somewhere to put their pre-ring, and integrating over it
    would fold that pre-ring into the answer -- reintroducing, by a different
    route, the very artefact the lead-in was added to remove.
    """
    seg = x[origin:]
    e = np.cumsum(seg[::-1] ** 2)[::-1]
    e = np.maximum(e, np.finfo(float).tiny)
    return 10.0 * np.log10(e / e[0])


def decay_time(edc_db: np.ndarray, fs: float, upper: float, lower: float) -> float | None:
    """Least-squares slope between two dB levels, extrapolated to a 60 dB drop.

    The multiplier every reference implementation applies -- x2 for T30, x3 for
    T20, x6 for EDT -- is NOT applied here and must not be. That factor exists
    to convert a decay measured over a 30/20/10 dB span into a 60 dB figure; a
    least-squares slope in dB per second already carries that conversion, so
    `-60/slope` is the 60 dB time directly. Multiplying as well is a double
    count, and it is a comfortable one because it lands on a plausible number.

    Returns None when the curve never reaches `lower` -- a refusal, not a
    number. Reference implementations that pick the nearest available sample
    instead (python-acoustics 0.2.6 does: `sch_db[abs(sch_db - end).argmin()]`)
    will report a decay time for a curve that never decayed that far, which is
    the failure mode this return type exists to prevent.
    """
    if edc_db[-1] > lower:
        return None
    i0 = int(np.argmax(edc_db <= upper))
    i1 = int(np.argmax(edc_db <= lower))
    if i1 <= i0:
        return None
    t = np.arange(i0, i1) / fs
    slope, _ = np.polyfit(t, edc_db[i0:i1], 1)
    if slope >= 0.0:
        return None
    return -60.0 / slope


PARAMS = [
    ("EDT", 0.0, -10.0),
    ("T20", -5.0, -25.0),
    ("T30", -5.0, -35.0),
]

MODES = ["forward", "zero-phase", "time-reversed"]


def main() -> None:
    print(__doc__.split("\n\n")[0])
    print()
    print(f"fs = {FS:.0f} Hz, seed = {RNG_SEED}, octave band-pass order 8 (SOS)")
    print(f"lead-in {LEAD_IN_SEC*1000:.0f} ms before the direct sound; noise floor {SNR_DB:.0f} dB below it")
    print("Ground truth T60 is exact by construction: h = n(t)*exp(-t/tau),")
    print("tau = T60 / (3*ln 10). Every deviation below is the filter, not the room.")
    print()

    for t60 in (0.4, 0.8, 1.2, 2.0):
        seconds = max(2.0, 2.5 * t60)
        h, origin = make_decay(t60, FS, seconds, RNG_SEED)

        print(f"--- true T60 = {t60:.2f} s " + "-" * 52)
        header = f"{'band':>8} {'param':>5} " + "".join(f"{m:>16}" for m in MODES)
        print(header)

        for centre in (125.0, 250.0, 500.0, 1000.0, 4000.0):
            sos = octave_sos(centre, FS)
            curves = {m: schroeder_db(filter_mode(h, sos, m), origin) for m in MODES}
            for name, upper, lower in PARAMS:
                cells = []
                for m in MODES:
                    v = decay_time(curves[m], FS, upper, lower)
                    if v is None:
                        cells.append(f"{'refused':>16}")
                    else:
                        err = 100.0 * (v - t60) / t60
                        cells.append(f"{v:8.3f}s{err:+6.1f}%")
                print(f"{centre:8.0f} {name:>5} " + "".join(cells))
        print()


if __name__ == "__main__":
    main()
