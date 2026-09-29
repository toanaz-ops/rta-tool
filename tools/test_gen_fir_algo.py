# SPDX-License-Identifier: AGPL-3.0-or-later
"""Self-test for tools/gen_fir_algo.py's design_linear_phase -- the numpy
second author of core/src/dsp/FirDesign.cpp's designLinearPhaseCore.

It pins the even-N construction (record docs/dsp/2026-09-06-l7-fir-export.md
Sec.4, amendment 2026-09-29): before that fix the function reproduced the
C++'s defect (Sum taps ~ 2 for even N) while its docstring claimed a bit-for-
bit mirror. Expectations are closed form, not "whatever the code printed".

CI's `tools (pytest)` job installs numpy and scipy at the project venv's pinned
versions (ci.yml, d4f36ee), so this module runs there; `importorskip` only
keeps it from failing a bare environment that has pytest alone.
"""

from __future__ import annotations

import sys
from pathlib import Path

import pytest

np = pytest.importorskip("numpy")
pytest.importorskip("scipy")

sys.path.insert(0, str(Path(__file__).resolve().parent))

import gen_fir_algo as algo  # noqa: E402


@pytest.fixture
def flat_target(monkeypatch):
    """A flat 0 dB target: the module's breakpoints kept, every gain zero."""
    monkeypatch.setattr(algo, "TARGET_GAINS_DB", np.zeros_like(algo.TARGET_GAINS_DB))


def _hann(n: int) -> np.ndarray:
    i = np.arange(n)
    return 0.5 - 0.5 * np.cos(2.0 * np.pi * i / n)


@pytest.mark.parametrize("n", [1024, 4096])
def test_even_n_flat_design_has_unit_dc_gain_and_the_half_sample_taps(flat_target, n):
    taps, m = algo.design_linear_phase(n)

    # Bitwise symmetry, by construction.
    assert np.array_equal(taps, taps[::-1])

    # Sum of taps IS H(e^{j0}); the flat target is 1. The defect read 2 - O(1e-5).
    # 1e-3 is the C++ test's own bound (window leakage of the Nyquist null at
    # DC is ~1e-7; double FFT noise is ~1e-15).
    assert abs(taps.sum() - 1.0) <= 1e-3

    # Closed form: with the Nyquist bin forced to 0 the flat grid is 1 on bins
    # 0..M/2-1, so g[j] = (1/M) sum_{k=-(M/2-1)}^{M/2-1} e^{j 2 pi k (j+1/2)/M}
    #                   = (-1)^j cot(pi (j+1/2)/M) / M,
    # and tap N/2+j is g[j] times the periodic Hann at N/2-1-j.
    j = np.arange(n // 2)
    g = ((-1.0) ** j) / (np.tan(np.pi * (j + 0.5) / m) * m)
    expected = g * _hann(n)[n // 2 - 1 - j]
    # Double-precision FFT of length M: eps*log2(M) ~ 1.5e-15; 1e-12 is a
    # stated margin, three orders of magnitude, not a fit.
    assert np.max(np.abs(taps[n // 2:] - expected)) <= 1e-12

    # The two centre taps are the same half-sample value, ~2/pi, never ~1.
    assert taps[n // 2 - 1] == taps[n // 2]
    assert 0.6 < taps[n // 2] < 0.7


def test_odd_n_flat_design_is_the_plain_circular_read_unchanged(flat_target):
    n = 1023
    taps, m = algo.design_linear_phase(n)

    # The pre-fix loop, transcribed as a did-not-move lock (it shares its shape
    # with the code under test, so it is NOT independent evidence): plain irfft,
    # read circularly at offset i - (N-1)/2. For odd N that was always correct
    # and must not move (the committed goldens are odd N). The independent part
    # of this test is the closed-form checks below it: unit DC gain and the
    # argmax at the centre tap.
    magnitude = algo.sample_target_magnitude(m)
    h_zero = np.fft.irfft(magnitude.astype(complex), n=m)
    w = algo.signal.get_window("hann", n, fftbins=True)
    half = (n - 1) // 2
    reference = np.zeros(n)
    for i in range(half + 1):
        value = h_zero[(i - half) % m] * w[i]
        reference[i] = value
        reference[n - 1 - i] = value
    assert np.array_equal(taps, reference)

    # And it is a centred delta of unit DC gain.
    assert abs(taps.sum() - 1.0) <= 1e-3
    assert int(np.argmax(np.abs(taps))) == half
