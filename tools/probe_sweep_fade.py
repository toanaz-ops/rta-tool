#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Measures the pre-arrival sidelobe floor of the Farina analysis pulse.

    .venv/Scripts/python.exe tools/probe_sweep_fade.py

Writes nothing. This is not a golden-vector generator -- it produces the
measured table in `docs/dsp/2026-08-30-sweep-ir-l4a.md` decision 5, which two
C++ tests lock against as an empirical law rather than a closed form. It is
committed so that table can be re-derived instead of trusted.

WHAT IS BEING MEASURED
----------------------
`sweep (x) inverseFilter` is not a Dirac; it is a band-limited pulse whose
sidelobes extend both directions in time. The region well BEFORE the pulse is
where harmonic distortion products land (at -L*ln N), so the sidelobe level
there is the noise floor against which any distortion measurement -- and any
reverberant decay in lane L4b -- must be read. It is reported relative to the
peak, more than 50 ms before it, which is beyond the last sidelobe of interest
and before the H2 packet for every duration tested here.

The sweep and the inverse filter are transcribed from
core/include/rta/gen/Sweep.h. Deliberately transcribed, not bound to the C++:
if this and the C++ disagree, that disagreement is the finding.
"""

import numpy as np

FS = 48000.0
F1 = 20.0
F2 = 20000.0


def linear_convolve(a, b):
    """Zero-padded FFT convolution, length len(a)+len(b)-1 -- the linear (not
    cyclic) deconvolution decision 1 specifies."""
    size = 1 << int(np.ceil(np.log2(len(a) + len(b) - 1)))
    return np.fft.irfft(np.fft.rfft(a, size) * np.fft.rfft(b, size), size)[:len(a) + len(b) - 1]


def sweep_and_inverse(duration_sec, fade_in_octaves, fade_out_octaves):
    """The excitation and its Farina inverse filter, with fades given in octaves
    of sweep travel rather than seconds -- see why that is the governing unit
    in decision 5 of the record."""
    length_l = duration_sec / np.log(F2 / F1)          # L = T / ln(f2/f1)
    phase_k = 2.0 * np.pi * F1 * length_l              # K = 2*pi*f1*L
    n_samples = int(round(duration_sec * FS))
    n = np.arange(n_samples)

    # expm1 rather than exp(x)-1: for a short sweep the exponent is small at
    # the start and the subtraction would lose the leading digits.
    phase = phase_k * np.expm1(n / (FS * length_l))
    inst_hz = F1 * np.exp(n / (FS * length_l))
    sweep = 0.5 * np.sin(phase)

    # Time to travel k octaves is k*ln2*L, independent of where in the sweep.
    fade_in = int(round(fade_in_octaves * np.log(2.0) * length_l * FS))
    fade_out = int(round(fade_out_octaves * np.log(2.0) * length_l * FS))
    envelope = np.ones(n_samples)
    if fade_in > 1:
        envelope[:fade_in] = 0.5 * (1 - np.cos(np.pi * np.arange(fade_in) / (fade_in - 1)))
    if fade_out > 1:
        tail = np.arange(fade_out, 0, -1)
        envelope[n_samples - fade_out:] = 0.5 * (1 - np.cos(np.pi * (tail - 1) / (fade_out - 1)))
    sweep *= envelope

    # inv[m] = x[N-1-m] * (instantaneousFrequency(N-1-m) / endHz): the +6 dB/oct
    # envelope tracks the frequency present at the ORIGINAL index, before
    # reversal. See the long comment in Sweep.h before touching this.
    inverse = (sweep * (inst_hz / F2))[::-1]
    return sweep, inverse, length_l


def analysis_pulse(duration_sec, fade_in_octaves, fade_out_octaves):
    """`sweep (x) inverse`, plus the index at which t = 0 sits."""
    sweep, inverse, _ = sweep_and_inverse(duration_sec, fade_in_octaves, fade_out_octaves)
    return linear_convolve(sweep, inverse), len(inverse) - 1


def pre_arrival_floor_db(duration_sec, fade_in_octaves, fade_out_octaves):
    pulse, origin = analysis_pulse(duration_sec, fade_in_octaves, fade_out_octaves)
    before = np.abs(pulse[:origin - int(0.05 * FS)])
    return 20.0 * np.log10(before.max() / abs(pulse[origin]))


def main():
    print("Pre-arrival sidelobe floor, dB relative to the analysis pulse peak.")
    print("20 Hz - 20 kHz at 48 kHz. Two durations, to show the floor does not")
    print("depend on seconds once the fades are expressed in octaves.\n")

    print("fade-in swept, fade-out held at 0.5 octave")
    print("  octaves      T=2s      T=10s")
    for octaves in (0.25, 0.5, 1.0, 1.5, 2.0, 3.0):
        a = pre_arrival_floor_db(2.0, octaves, 0.5)
        b = pre_arrival_floor_db(10.0, octaves, 0.5)
        print(f"  {octaves:7.2f}  {a:8.1f}  {b:9.1f}")

    print("\nfade-out swept, fade-in held at 2 octaves")
    print("  octaves      T=2s      T=10s")
    for octaves in (0.25, 0.5, 1.0, 2.0, 3.0):
        a = pre_arrival_floor_db(2.0, 2.0, octaves)
        b = pre_arrival_floor_db(10.0, 2.0, octaves)
        print(f"  {octaves:7.2f}  {a:8.1f}  {b:9.1f}")

    print("\nThe two fades pull in opposite directions: wider fade-IN improves")
    print("the floor, wider fade-OUT degrades it. Hypothesis in the record; the")
    print("asymmetry itself is measured here and is not in dispute.")

    # Pulse geometry -- decision 3 rests on these three facts, so they are
    # measured here rather than reasoned about somewhere else.
    print("\npulse geometry at T=2s, 2 octaves in / 0.5 out (decision 3):")
    pulse, origin = analysis_pulse(2.0, 2.0, 0.5)
    peak = int(np.argmax(np.abs(pulse)))
    print(f"  peak index      {peak}   expected Ninv-1 = {origin}"
          f"   {'OK' if peak == origin else 'MISMATCH'}")
    print(f"  peak sign       {np.sign(pulse[peak]):+.0f}"
          "   (positive => zero phase, so polarity is readable)")
    half = int(0.002 * FS)                       # +/- 2 ms about the peak
    left = pulse[peak - half:peak]
    right = pulse[peak + 1:peak + 1 + half][::-1]
    err = np.max(np.abs(left - right)) / np.max(np.abs(pulse))
    print(f"  symmetry error  {err:.1e} of peak over +/-2 ms")

    # Decision 3's closed form: the Nth harmonic lands at -L*ln(N). Driven by a
    # memoryless square law (H2 only) and a cubic (H3), so the log spacing is
    # exercised at two orders rather than fitted at one.
    print("\nharmonic packet position vs -L*ln(N) (decision 3):")
    sweep, inverse, length_l = sweep_and_inverse(2.0, 2.0, 0.5)
    for order, distorted in ((2, sweep + 0.10 * sweep ** 2),
                             (3, sweep + 0.10 * sweep ** 3)):
        deconvolved = np.abs(linear_convolve(distorted, inverse))
        expected = origin - length_l * np.log(order) * FS
        # Search strictly before the previous packet so H3 cannot re-find H2.
        upper = int(origin - length_l * np.log(order) * FS + 0.005 * FS)
        lower = int(origin - length_l * np.log(order + 1) * FS + 0.005 * FS)
        found = lower + int(np.argmax(deconvolved[lower:upper]))
        print(f"  H{order}  found {found:7d}   expected {expected:9.1f}"
              f"   error {found - expected:+6.1f} samples")

    # Precision check: if these two columns differ, the arithmetic has become
    # the limit and decision 8 needs revisiting.
    print("\nsame quantity in float32 vs float64 (decision 8):")
    for octaves in (0.5, 2.0, 3.0):
        pulse, origin = analysis_pulse(2.0, octaves, 0.5)
        before = np.abs(pulse[:origin - int(0.05 * FS)])
        f64 = 20.0 * np.log10(before.max() / abs(pulse[origin]))
        p32 = pulse.astype(np.float32)
        b32 = np.abs(p32[:origin - int(0.05 * FS)])
        f32 = 20.0 * np.log10(b32.max() / abs(p32[origin]))
        print(f"  {octaves:4.2f} oct   float64 {f64:8.1f}   float32 {f32:8.1f}")


if __name__ == "__main__":
    main()
