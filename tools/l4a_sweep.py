#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Shared machinery for the two L4a verification scripts.

Not runnable on its own. `tools/verify_l4a_pulse.py` and
`tools/verify_l4a_measurement.py` both import it, so the sweep transcribed from
core/include/rta/gen/Sweep.h exists in exactly one place -- two copies would
drift, and a drifted copy would quietly measure a signal the C++ never emits.
"""

import numpy as np
import scipy.fft
import scipy.signal

FS = 48000.0


def sweep_and_inverse(duration_sec, f1, f2, fade_in_octaves, fade_out_octaves,
                      whitening=True):
    """Transcribed from core/include/rta/gen/Sweep.h; fades in octaves of travel.

    `whitening=False` drops the +6 dB/oct envelope from the inverse filter,
    leaving plain time reversal. That is NOT a usable deconvolution -- the result
    is no longer spectrally flat -- but it is the control condition section J
    needs to test WHY the two fades behave differently.
    """
    length_l = duration_sec / np.log(f2 / f1)
    phase_k = 2.0 * np.pi * f1 * length_l
    n_samples = int(round(duration_sec * FS))
    n = np.arange(n_samples)
    inst_hz = f1 * np.exp(n / (FS * length_l))
    sweep = 0.5 * np.sin(phase_k * np.expm1(n / (FS * length_l)))

    fade_in = int(round(fade_in_octaves * np.log(2.0) * length_l * FS))
    fade_out = int(round(fade_out_octaves * np.log(2.0) * length_l * FS))
    env = np.ones(n_samples)
    if fade_in > 1:
        env[:fade_in] = 0.5 * (1 - np.cos(np.pi * np.arange(fade_in) / (fade_in - 1)))
    if fade_out > 1:
        tail = np.arange(fade_out, 0, -1)
        env[n_samples - fade_out:] = 0.5 * (1 - np.cos(np.pi * (tail - 1) / (fade_out - 1)))
    sweep = sweep * env
    shaping = (inst_hz / f2) if whitening else np.ones_like(inst_hz)
    return sweep, (sweep * shaping)[::-1], length_l


def linear_convolve(a, b, dtype=np.float64):
    """scipy.fft, not numpy.fft: numpy silently promotes float32 to float64, so a
    'single precision' measurement made with numpy measures double precision and
    rounds the answer. That mistake is the reason section F exists."""
    size = 1 << int(np.ceil(np.log2(len(a) + len(b) - 1)))
    a = np.asarray(a, dtype=dtype)
    b = np.asarray(b, dtype=dtype)
    spectrum = scipy.fft.rfft(a, size) * scipy.fft.rfft(b, size)
    expected = np.complex64 if dtype is np.float32 else np.complex128
    assert spectrum.dtype == expected, (
        f"asked for {dtype.__name__} but the transform produced {spectrum.dtype}; "
        "the precision comparison would be meaningless")
    out = scipy.fft.irfft(spectrum, size)
    return out[:len(a) + len(b) - 1]


def pre_arrival(pulse, origin):
    """Everything more than 50 ms before t=0, and the peak it is measured against."""
    return np.abs(pulse[:origin - int(0.05 * FS)]), abs(pulse[origin])


def split_pre_and_main(pulse, origin, guard_sec=0.05):
    """Two equal-length segments: everything before the guard, and the same
    number of samples centred on t=0.

    Equal lengths matter -- the two are compared bin by bin, so they must have
    the same frequency resolution and the same noise bandwidth.

    NOT done with a bandpass filter, deliberately. A zero-phase Butterworth
    narrow enough to isolate an octave rings for tens of milliseconds and, being
    zero-phase, smears the main pulse BACKWARDS into the very region being
    measured. A first version of this script did exactly that and reported an
    octave band louder than the broadband signal it came from, which is
    impossible and was the tell.
    """
    guard = int(guard_sec * FS)
    length = origin - guard
    pre = pulse[:length]
    start = origin - length // 2
    main = pulse[start:start + length]
    window = np.hanning(length)
    return pre * window, main * window


def band_ratio_db(pre, main, edges_hz):
    """Energy of `pre` relative to `main`, summed over each band in `edges_hz`."""
    size = len(pre)
    pre_mag = np.abs(scipy.fft.rfft(pre)) ** 2
    main_mag = np.abs(scipy.fft.rfft(main)) ** 2
    freqs = np.arange(len(pre_mag)) * FS / size
    out = []
    for lo, hi in zip(edges_hz[:-1], edges_hz[1:]):
        sel = (freqs >= lo) & (freqs < hi)
        if not sel.any() or main_mag[sel].sum() == 0.0:
            out.append((lo, hi, float("nan")))
            continue
        out.append((lo, hi, 10 * np.log10(pre_mag[sel].sum() / main_mag[sel].sum())))
    return out


