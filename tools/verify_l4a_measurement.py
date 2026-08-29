#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Falsification runs on MEASUREMENTS made with the analysis pulse, for the
claims in docs/dsp/2026-08-30-sweep-ir-l4a.md.

    .venv/Scripts/python.exe tools/verify_l4a_measurement.py

Writes nothing. Every section here drives a known system -- a filter, a
loudspeaker-shaped passband, a synthetic room -- through the deconvolution and
asks whether what comes back is what went in.
Sections D -> decisions 1, 3 and 7.  E, H -> decision 6.  K -> decision 9.
G -> the hand-off note to lane L4b.

Companion: tools/verify_l4a_pulse.py, which measures the pulse itself.
"""

import numpy as np
import scipy.fft
import scipy.signal

from l4a_sweep import (FS, sweep_and_inverse, linear_convolve,
                       pre_arrival, split_pre_and_main, band_ratio_db)


# ---------------------------------------------------------------- D
def section_d():
    """Round trip: does a known impulse response come back, at the right index?"""
    print("\nD. Round trip of a known response (dec. 1 and 3)")
    sweep, inverse, _ = sweep_and_inverse(2.0, 20.0, 20000.0, 2.0, 0.5)
    origin = len(inverse) - 1
    reference = linear_convolve(sweep, inverse)

    delay, amplitude = 137, 0.8
    response = np.zeros(len(sweep) + delay)
    response[delay:] = amplitude * sweep
    recovered = linear_convolve(response, inverse)
    peak = int(np.argmax(np.abs(recovered)))
    print(f"   pure delay {delay} + gain {amplitude}:  peak at {peak}"
          f"   expected {origin + delay}   error {peak - origin - delay:+d} samples")
    print(f"   recovered gain {np.abs(recovered[peak]) / np.abs(reference[origin]):.4f}"
          f"   expected {amplitude}")

    # The comparison band stays INSIDE [f_lo, f_hi] = [80 Hz, 14142 Hz] for this
    # fade. A first version compared up to 14 kHz -- on the edge, where the
    # analysis pulse is already rolling off -- and read that rolloff as a 3 dB
    # error in the round trip. The band edge is not a defect in the round trip.
    # The analysis pulse is SYMMETRIC about t=0 (probe_sweep_fade.py measures the
    # symmetry). So the deconvolved signal h (x) p has content on BOTH sides of
    # originIndex, and a transform starting exactly at originIndex slices the
    # pulse down its middle. The lead-in below is how much of that left half is
    # kept; 0 reproduces the naive window that record decision 7 first specified.
    size = 1 << 18
    lo_hz, hi_hz = 100.0, 10000.0
    freqs = np.arange(size // 2 + 1) * FS / size
    sel = (freqs > lo_hz) & (freqs < hi_hz)

    print(f"   magnitude error over {lo_hz:.0f} Hz-{hi_hz/1000:.0f} kHz,"
          " by how much of the pulse's left half the window keeps:")
    for lead_ms in (0.0, 5.0, 20.0, 50.0):
        lead = int(lead_ms * FS / 1000)
        start = origin - lead
        scalar = np.abs(scipy.fft.rfft(reference[start:start + size], size))[sel].mean()
        line = f"   lead-in {lead_ms:5.1f} ms  "
        for corner, order, kind, label in ((1000.0, 2, 'lowpass', "LP1k"),
                                           (300.0, 4, 'highpass', "HP300")):
            sos = scipy.signal.butter(order, corner, kind, fs=FS, output='sos')
            recovered = linear_convolve(scipy.signal.sosfilt(sos, sweep), inverse)
            got = scipy.fft.rfft(recovered[start:start + size], size) / scalar
            _, want = scipy.signal.sosfreqz(sos, worN=freqs, fs=FS)
            err = 20 * np.log10(np.abs(got[sel])) - 20 * np.log10(np.abs(want[sel]))
            line += f"   {label} {err.min():+6.2f}..{err.max():+6.2f} dB"
        print(line)

    # Is the required lead-in a fixed number of milliseconds, or a fixed number
    # of CYCLES at the bottom of the valid band? Only the second is derivable
    # from the Config the caller already supplied, so only the second can become
    # a rule rather than a constant somebody picked.
    print("\n   required lead-in expressed in cycles of f_lo, two fade widths:")
    for fade_in_oct in (0.5, 2.0):
        s2, inv2, _ = sweep_and_inverse(2.0, 20.0, 20000.0, fade_in_oct, 0.5)
        origin2 = len(inv2) - 1
        f_lo = 20.0 * 2.0 ** fade_in_oct
        reference2 = linear_convolve(s2, inv2)
        sos = scipy.signal.butter(4, 300.0, 'highpass', fs=FS, output='sos')
        recovered = linear_convolve(scipy.signal.sosfilt(sos, s2), inv2)
        _, want = scipy.signal.sosfreqz(sos, worN=freqs, fs=FS)
        line = f"   f_lo {f_lo:5.1f} Hz  "
        for cycles in (1, 2, 4, 8):
            start = origin2 - int(cycles / f_lo * FS)
            scalar = np.abs(scipy.fft.rfft(reference2[start:start + size], size))[sel].mean()
            got = scipy.fft.rfft(recovered[start:start + size], size) / scalar
            err = 20 * np.log10(np.abs(got[sel])) - 20 * np.log10(np.abs(want[sel]))
            worst = max(abs(err.min()), abs(err.max()))
            line += f"   {cycles}cyc {worst:5.2f} dB"
        print(line)


# ---------------------------------------------------------------- E
def section_e():
    """Can the 'first sample above a fraction of the peak' rule pick a PRECURSOR
    sidelobe of the wrong sign? That is the failure decision 6 must survive."""
    print("\nE. Polarity rule against precursor ringing (dec. 6)")
    sweep, inverse, _ = sweep_and_inverse(2.0, 20.0, 20000.0, 2.0, 0.5)
    origin = len(inverse) - 1
    # Three systems of increasing hostility. The subwoofer is the real test: a
    # 30-120 Hz passband rings for tens of milliseconds, so its recovered pulse
    # has large excursions of BOTH signs and no crisp first arrival.
    systems = (("full range 60 Hz-15 kHz", scipy.signal.butter(
                   4, [60.0, 15000.0], 'bandpass', fs=FS, output='sos')),
               ("horn 500 Hz-8 kHz     ", scipy.signal.butter(
                   4, [500.0, 8000.0], 'bandpass', fs=FS, output='sos')),
               ("subwoofer 30-120 Hz   ", scipy.signal.butter(
                   4, [30.0, 120.0], 'bandpass', fs=FS, output='sos')))
    for label, sos in systems:
        for polarity in (+1, -1):
            driven = scipy.signal.sosfilt(sos, polarity * sweep)
            recovered = linear_convolve(driven, inverse)
            window = recovered[origin:origin + int(0.05 * FS)]
            largest = np.max(np.abs(window))
            reads = []
            for fraction in (0.2, 0.3, 0.5, 0.7):
                idx = int(np.argmax(np.abs(window) >= fraction * largest))
                reads.append((fraction, idx, int(np.sign(window[idx]))))
            wrong = [f"{f:.1f}" for f, _, s in reads if s != polarity]
            spread = f"{reads[0][1]}..{reads[-1][1]}"
            status = "ok" if not wrong else "WRONG SIGN at fraction " + ",".join(wrong)
            print(f"   {label}  true {polarity:+d}"
                  f"   arrival +{spread:>9s} samples   {status}")


# ---------------------------------------------------------------- G
def section_g():
    """Muller's warning: linear deconvolution turns steady noise into a DECAYING,
    progressively low-passed tail that looks like reverberation. Measured here so
    lane L4b inherits a number rather than a caution."""
    print("\nG. Shape of the deconvolved noise tail (hand-off to L4b)")
    rng = np.random.default_rng(20260830)
    sweep, inverse, _ = sweep_and_inverse(2.0, 20.0, 20000.0, 2.0, 0.5)
    origin = len(inverse) - 1
    noisy = sweep + rng.normal(0.0, 0.01, len(sweep))   # about -34 dB of white noise
    recovered = linear_convolve(noisy, inverse)
    peak = abs(recovered[origin])
    print("   ms after t=0     level      centroid of that slice")
    for start_ms in (100, 400, 800, 1400):
        a = origin + int(start_ms * FS / 1000)
        slice_ = recovered[a:a + int(0.1 * FS)]
        mag = np.abs(scipy.fft.rfft(slice_))
        freqs = np.arange(len(mag)) * FS / len(slice_)
        centroid = float((freqs * mag).sum() / mag.sum())
        print(f"   {start_ms:6d}      {20*np.log10(np.sqrt(np.mean(slice_**2))/peak):7.1f} dB"
              f"      {centroid:8.0f} Hz")
    print("   A falling level with a falling centroid is the artefact, not a room.")

    # The decay rate is not empirical. The inverse filter's +6 dB/oct envelope
    # spans exactly 20*log10(f2/f1) dB across the sweep's length T, and the
    # deconvolved noise inherits it, so
    #     decay = 20*log10(f2/f1) / T   dB/s      RT60 = 3*T / log10(f2/f1)
    # Three configurations, so the formula is tested rather than one coincidence
    # (T = 2 s over three decades happens to give RT60 = T exactly, which is
    # precisely the sort of accident that gets mistaken for a law).
    print("\n   apparent RT60 of the artefact vs the closed form 3T/log10(f2/f1):")
    for duration, f1, f2 in ((2.0, 20.0, 20000.0), (4.0, 20.0, 20000.0), (2.0, 100.0, 10000.0)):
        s, inv, _ = sweep_and_inverse(duration, f1, f2, 2.0, 0.5)
        o = len(inv) - 1
        rec = linear_convolve(s + rng.normal(0.0, 0.01, len(s)), inv)
        times, levels = [], []
        for start_ms in (100, 200, 300, 400, 500):
            a = o + int(start_ms * FS / 1000)
            chunk = rec[a:a + int(0.05 * FS)]
            times.append(start_ms / 1000.0)
            levels.append(20 * np.log10(np.sqrt(np.mean(chunk ** 2))))
        slope = -np.polyfit(times, levels, 1)[0]
        predicted = 3.0 * duration / np.log10(f2 / f1)
        print(f"   T={duration:.0f}s  {f1:5.0f}-{f2:.0f} Hz"
              f"   measured RT60 {60/slope:5.2f} s   closed form {predicted:5.2f} s"
              f"   error {100*(60/slope - predicted)/predicted:+5.1f}%")
    print("   The mechanism is exact; the number is good to about 10 percent over")
    print("   this range. Use it to recognise the artefact, never to subtract it.")


# ---------------------------------------------------------------- H
def section_h():
    """Polarity on systems that are NOT minimum phase.

    Record decision 6 admits this case is untested. Two questions: does the sign
    still read correctly, and -- the more important one -- does the CONFIDENCE
    figure decision 6 defines actually notice when it does not? A confidence that
    stays high through a wrong answer is worse than no confidence at all.
    """
    print("\nH. Polarity on non-minimum-phase systems (dec. 6, previously untested)")
    sweep, inverse, length_l = sweep_and_inverse(2.0, 20.0, 20000.0, 2.0, 0.5)
    origin = len(inverse) - 1
    band = scipy.signal.butter(4, [60.0, 15000.0], 'bandpass', fs=FS, output='sos')

    def fir_pair(taps):
        return np.asarray(taps, dtype=float)

    systems = [
        ("minimum phase   [1, 0.5]  ", fir_pair([1.0, 0.5]), True),
        ("maximum phase   [0.5, 1]  ", fir_pair([0.5, 1.0]), True),
        ("2nd-order allpass at 500 Hz", None, True),
        ("inverted reflection 1.5x  ", None, True),
        ("narrowband 200-250 Hz     ", None, False),
    ]
    print("      system                 true  read   confidence   symmetry")
    for label, taps, use_band in systems:
        for polarity in (+1, -1):
            driven = polarity * sweep
            if taps is not None:
                driven = np.convolve(driven, taps)[:len(sweep)]
            elif "allpass" in label:
                b, a = scipy.signal.iirfilter(2, [400.0, 600.0], btype='bandstop',
                                              ftype='butter', fs=FS)
                # A true allpass: flat magnitude, non-minimum phase. Built as the
                # ratio of a polynomial to its own reversal.
                a_ap = np.array([1.0, -1.6, 0.81])
                b_ap = a_ap[::-1]
                driven = scipy.signal.lfilter(b_ap, a_ap, driven)
            elif "reflection" in label:
                # Direct arrival plus a LARGER inverted arrival 3 ms later, so the
                # dominant peak carries the opposite sign to the direct sound.
                # Written as one explicit shift: an earlier version composed a
                # slice with np.roll and cancelled its own delay, producing a
                # plain sign inversion that then "failed" the test tautologically.
                delay = int(0.003 * FS)
                echo = np.zeros(len(driven))
                echo[delay:] = -1.5 * driven[:len(driven) - delay]
                driven = driven + echo
            if use_band:
                driven = scipy.signal.sosfilt(band, driven)
            else:
                narrow = scipy.signal.butter(4, [200.0, 250.0], 'bandpass',
                                             fs=FS, output='sos')
                driven = scipy.signal.sosfilt(narrow, driven)

            recovered = linear_convolve(driven, inverse)
            window = recovered[origin:origin + int(0.05 * FS)]
            largest = np.max(np.abs(window))
            idx = int(np.argmax(np.abs(window) >= 0.5 * largest))
            read = int(np.sign(window[idx]))
            # Confidence exactly as decision 6 defines it: peak over the RMS of
            # the harmonic-free window between -L*ln2 and t=0.
            noise = recovered[origin - int(length_l * np.log(2.0) * FS):origin]
            confidence = 20 * np.log10(largest / np.sqrt(np.mean(noise ** 2)))
            # Proposed extra observable: how nearly equal the largest positive and
            # largest negative excursions are. Near 0 dB means "no dominant sign".
            symmetry = 20 * np.log10(abs(window.max()) / abs(window.min()))
            flag = " " if read == polarity else "  <-- WRONG"
            print(f"   {label}  {polarity:+d}    {read:+d}"
                  f"   {confidence:7.1f} dB   {abs(symmetry):5.1f} dB{flag}")


# ---------------------------------------------------------------- K
def section_k():
    """How much silence must follow the sweep?

    The rule 'post-sweep quiet must exceed the reverberation time' comes from a
    secondary summary of AES-2id, not from a measurement made here. Measured
    against a synthetic room whose RT60 is known exactly.
    """
    print("\nK. Required silence after the sweep, vs a known RT60 (secondary source)")
    rng = np.random.default_rng(20260830)
    rt60, duration = 1.0, 2.0
    sweep, inverse, _ = sweep_and_inverse(duration, 20.0, 20000.0, 2.0, 0.5)
    origin = len(inverse) - 1

    # Exponentially decaying band-limited noise: a room with RT60 exactly 1.0 s.
    ir_len = int(3.0 * FS)
    decay = 10.0 ** (-3.0 * np.arange(ir_len) / (rt60 * FS))
    room = scipy.signal.sosfilt(
        scipy.signal.butter(4, [60.0, 15000.0], 'bandpass', fs=FS, output='sos'),
        rng.normal(0.0, 1.0, ir_len)) * decay
    room[0] += 3.0                                     # a clear direct arrival

    full = np.convolve(sweep, room)
    size = 1 << 18
    freqs = np.arange(size // 2 + 1) * FS / size
    sel = (freqs > 100.0) & (freqs < 10000.0)
    lead = int(2.0 / 80.0 * FS)                        # decision 7's lead-in

    reference = linear_convolve(sweep, inverse)
    scalar = np.abs(scipy.fft.rfft(reference[origin - lead:origin - lead + size],
                                   size))[sel].mean()
    truth = scipy.fft.rfft(room, size)

    print("   gap after sweep   gap/RT60   magnitude error over 100 Hz-10 kHz")
    for gap_sec in (0.0, 0.25, 0.5, 1.0, 1.5, 2.0):
        captured = full[:len(sweep) + int(gap_sec * FS)]
        recovered = linear_convolve(captured, inverse)
        start = origin - lead
        got = scipy.fft.rfft(recovered[start:start + size], size) / scalar
        err = 20 * np.log10(np.abs(got[sel])) - 20 * np.log10(np.abs(truth[sel]))
        rms = float(np.sqrt(np.mean(err ** 2)))
        print(f"   {gap_sec:9.2f} s     {gap_sec/rt60:6.2f}"
              f"      rms {rms:5.2f} dB   worst {np.abs(err).max():6.2f} dB")

# ---------------------------------------------------------------- L
def section_l():
    """Is the symmetry figure a test of "does this waveform have a sign", or is
    it just a bandwidth meter?

    Section H found that a 200-250 Hz system answers polarity backwards while
    its confidence reads highest in the set, and that a symmetry figure -- the
    largest positive excursion over the largest negative one -- separated it.
    A 1.0 dB gate was proposed on FIVE systems. Five is not a survey, and a gate
    that refuses a legitimate measurement is a worse failure than one that never
    fires: an operator who is told "unknown" for a real subwoofer stops using
    the feature.

    So: sweep bandwidth against centre frequency and look at the whole surface.

    Read the "disagrees" marker carefully. It does NOT mean the code is broken.
    Flipping the drive polarity negates the recovered response exactly -- that is
    linearity, and it means a RELATIVE polarity comparison is always sound. What
    the marker shows is that for a band-limited system the sign of the first
    arrival is a property of that system's own phase response, so an ABSOLUTE
    verdict ("this box is wired backwards") is not a question the measurement can
    answer. The tool must decline it rather than answer it confidently.
    """
    print("\nL. Symmetry vs bandwidth -- is the 1.0 dB gate a real boundary?")
    sweep, inverse, _ = sweep_and_inverse(2.0, 20.0, 20000.0, 2.0, 0.5)
    origin = len(inverse) - 1

    print("   symmetry in dB, and whether the SIGN agrees with the drive polarity")
    print("   width ->    0.50oct   1.00oct   2.00oct   2.50oct   3.00oct"
          "   4.00oct   8.00oct")
    for centre in (60.0, 250.0, 1000.0, 4000.0):
        cells = []
        for width in (0.5, 1.0, 2.0, 2.5, 3.0, 4.0, 8.0):
            lo = centre / 2.0 ** (width / 2.0)
            hi = min(centre * 2.0 ** (width / 2.0), 0.49 * FS)
            sos = scipy.signal.butter(4, [lo, hi], 'bandpass', fs=FS, output='sos')
            worst_sym, all_correct = 1e9, True
            for polarity in (+1, -1):
                rec = linear_convolve(scipy.signal.sosfilt(sos, polarity * sweep), inverse)
                window = rec[origin:origin + int(0.05 * FS)]
                largest = np.max(np.abs(window))
                idx = int(np.argmax(np.abs(window) >= 0.5 * largest))
                if int(np.sign(window[idx])) != polarity:
                    all_correct = False
                sym = abs(20 * np.log10(abs(window.max()) / abs(window.min())))
                worst_sym = min(worst_sym, sym)
            cells.append(f"{worst_sym:6.2f}{'  ' if all_correct else ' X'}")
        print(f"   {centre:6.0f} Hz  " + "  ".join(cells))
    # A 3-octave minimum refuses every real subwoofer -- a 30-120 Hz sub is two
    # octaves -- and the coarse grid above has 2 oct at 60 Hz reading CORRECTLY.
    # One cell is not a licence to carve an exception, so the low corner gets
    # its own grid: is the bass region reliable at two octaves, or was that cell
    # lucky? This is the measurement the product decision needs.
    print("\n   low-frequency corner, where a polarity checker earns its keep:")
    print("   width ->    1.00oct   1.50oct   2.00oct   2.50oct   3.00oct")
    for centre in (30.0, 45.0, 60.0, 90.0, 120.0):
        cells = []
        for width in (1.0, 1.5, 2.0, 2.5, 3.0):
            lo = centre / 2.0 ** (width / 2.0)
            hi = centre * 2.0 ** (width / 2.0)
            sos = scipy.signal.butter(4, [lo, hi], 'bandpass', fs=FS, output='sos')
            worst_sym, all_correct = 1e9, True
            for polarity in (+1, -1):
                rec = linear_convolve(scipy.signal.sosfilt(sos, polarity * sweep), inverse)
                window = rec[origin:origin + int(0.05 * FS)]
                largest = np.max(np.abs(window))
                idx = int(np.argmax(np.abs(window) >= 0.5 * largest))
                if int(np.sign(window[idx])) != polarity:
                    all_correct = False
                worst_sym = min(worst_sym,
                                abs(20 * np.log10(abs(window.max()) / abs(window.min()))))
            cells.append(f"{worst_sym:6.2f}{'  ' if all_correct else ' X'}")
        print(f"   {centre:6.0f} Hz  " + "  ".join(cells))

    print("\n   X marks a first-arrival sign that DISAGREES with the drive polarity.")
    print("   Compare the two populations: cells that agree span roughly 0.3 to 11 dB")
    print("   of symmetry, cells that disagree span roughly 0.3 to 4.2 dB. They")
    print("   overlap almost completely, so symmetry does not separate them and a")
    print("   gate on it -- at 1.0 dB or any other value -- decides nothing.")


if __name__ == "__main__":
    section_d()
    section_e()
    section_g()
    section_h()
    section_k()
    section_l()
