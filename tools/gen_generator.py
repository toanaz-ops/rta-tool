#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Golden vectors for rta::gen (Prng, Noise, Sweep, Mls), independent of the
C++ transcriptions being written in parallel.

    .venv/Scripts/python.exe tools/gen_generator.py

Writes core/tests/golden/generator.txt (committed; C++ needs neither Python
nor NumPy at build time). Format matches tools/gen_golden.py:
`case <name>` / `size <n>` / `<key> <v0> <v1> ...` / `end`.

PCG32 below is reimplemented from the published spec, not the C++ source: if this translated
core/include/rta/gen/Prng.h, a bug shared by both transcriptions would never be caught --
the value of a golden fixture is that two independent readings of the same spec agree.
O'Neill 2014, pcg-random.org, pcg_setseq_64_xsh_rr_32.

Two format traps (core/tests/support/Golden.h parses every row as a double): no value above
2**53 as one token -- a uint64 checksum splits into `_hi`/`_lo` 32-bit halves; rows are
whitespace-separated numerics only, no hex or labels inside a row.
"""

from __future__ import annotations

import pathlib
import sys
import numpy as np
import scipy.integrate
import scipy.signal

OUT_DIR = pathlib.Path(__file__).resolve().parent.parent / "core" / "tests" / "golden"
MASK64 = 0xFFFFFFFFFFFFFFFF
def fmt(values) -> str:
    if np.isscalar(values):
        values = [values]
    return " ".join(repr(float(v)) for v in values)

def case_block(name: str, size: int, rows: dict) -> str:
    lines = [f"case {name}", f"size {size}"]
    for key, values in rows.items():
        lines.append(f"{key} {fmt(values)}")
    lines.append("end")
    return "\n".join(lines)

# --- 1. PCG32. Every arithmetic step is masked to 64 bits: Python ints do
# not wrap on overflow the way a fixed-width uint64_t does.
class Pcg32:
    """pcg_setseq_64_xsh_rr_32, O'Neill 2014 (pcg-random.org, imneme/pcg-c)."""
    def __init__(self, seed: int, seq: int):
        self.state = 0
        self.inc = ((seq << 1) | 1) & MASK64
        self._step()
        self.state = (self.state + seed) & MASK64
        self._step()

    def _step(self) -> None:
        self.state = (self.state * 6364136223846793005 + self.inc) & MASK64

    def next_uint32(self) -> int:
        old = self.state
        self._step()
        xorshifted = (((old >> 18) ^ old) >> 27) & 0xFFFFFFFF
        rot = (old >> 59) & 0xFFFFFFFF
        return ((xorshifted >> rot) | (xorshifted << ((-rot) & 31))) & 0xFFFFFFFF

    def next_uniform(self) -> np.float32:
        # Pinned mapping, must match the C++ side bit-for-bit: affine map
        # computed in double, narrowed to float32 once at the very end.
        x = self.next_uint32()
        return np.float32(x / 2**32 * 2 - 1)

def build_pcg32_case() -> str:
    # Verified correction (2026-08-27): station A's real test file constructs a FRESH
    # Pcg32(42, 54) for each row -- u32 and uniform do NOT share one continued stream.
    # Continuing one stream would offset uniform by a 32-sample head start: a failure that
    # LOOKS like a PRNG bug but is really row-construction skew.
    u32_stream = Pcg32(42, 54)
    u32 = [u32_stream.next_uint32() for _ in range(32)]
    uniform_stream = Pcg32(42, 54)
    uniform = [uniform_stream.next_uniform() for _ in range(16)]
    return case_block("pcg32_seed42_seq54", 32, {"u32": u32, "uniform": uniform})

# --- 2. Kellett pink filter. Plain sample-by-sample loop -- no scipy
# lfilter (banned by check_no_polynomial_form.cmake here, and it could not
# express this non-standard 7-term structure directly anyway).
def kellett_pink_filter(white: np.ndarray) -> np.ndarray:
    """Paul Kellett's refined ("instrumentation grade") 7-term IIR, musicdsp.org. State
    starts at exactly 0.0 -- the golden depends on that. b0..b5 update top-to-bottom from
    their own previous value; `out` is then formed from the just-updated b0..b5 plus the
    OLD b6 (previous sample), and only after `out` is read does b6 advance -- that
    one-sample lag on b6 is load-bearing for pink_rms_gain's closed form, which treats b6
    as driven by w[n-1].
    """
    b0 = b1 = b2 = b3 = b4 = b5 = b6 = 0.0
    pink = np.empty(len(white), dtype=np.float64)
    for i in range(len(white)):
        w = float(white[i])
        b0 = 0.99886 * b0 + w * 0.0555179
        b1 = 0.99332 * b1 + w * 0.0750759
        b2 = 0.96900 * b2 + w * 0.1538520
        b3 = 0.86650 * b3 + w * 0.3104856
        b4 = 0.55000 * b4 + w * 0.5329522
        b5 = -0.76160 * b5 - w * 0.0168980
        pink[i] = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362
        b6 = w * 0.115926
    return pink

def build_pink_kellet_case() -> str:
    # A FRESH Pcg32(seed=7, seq=1), unrelated to the seed=42/seq=54 case. White travels with
    # pink because test 14 REQUIREs white matches bit-exactly before it ever looks at pink:
    # a mismatch is then diagnosed as "wrong PRNG stream", never mistaken for a coefficient
    # bug.
    rng = Pcg32(7, 1)
    white = np.array([rng.next_uniform() for _ in range(4096)], dtype=np.float32)
    pink = kellett_pink_filter(white)
    return case_block("pink_kellet", 4096, {"white": white, "pink": pink})

# --- 3. pink_rms_gain: the exact mean-square gain of the filter above for
# unit-variance white input, derived two independent ways, must agree to
# 1e-10 before writing.
POLES = [0.99886, 0.99332, 0.96900, 0.86650, 0.55000, -0.76160]
GAINS = [0.0555179, 0.0750759, 0.1538520, 0.3104856, 0.5329522, -0.0168980]
DIRECT_GAIN = 0.5362     # w[n] term, zero lag
DELAY_GAIN = 0.115926    # b6: w[n-1] term, one-sample lag (see filter above)

def pink_rms_gain_closed_form() -> float:
    """Each one-pole path is causal: b_i[n]=p_i*b_i[n-1]+g_i*w[n], so in steady state b_i[n]
    = sum_{k=0..inf} g_i*p_i**k*w[n-k]. Output is out[n] = sum_i b_i[n] + g6*w[n-1] +
    gd*w[n]. Group by lag w[n-m] into c[m]:

        c[0] = sum_i g_i + gd    c[1] = sum_i g_i*p_i + g6    c[m>=2] = sum_i g_i*p_i**m

    w[n-m] are uncorrelated across m (unit-variance iid white), so Var(out) = sum_m c[m]**2:
    a double sum over the six paths, a geometric series in (p_i*p_j) per pair (|p_i|<1
    always), sum_i sum_j g_i*g_j/(1-p_i*p_j); plus cross terms with the direct path (gd
    correlates with b_i[n] at strength g_i, since E[y_i[n]*w[n]]=g_i, zero lag, no p_i
    factor) and the delayed path (g6, driven by w[n-1], correlates at strength g_i*p_i --
    one extra step of decay). p5/g5 stay signed throughout; the sign is not special-cased.
    """
    variance = 0.0
    for gi, pi_ in zip(GAINS, POLES):
        for gj, pj_ in zip(GAINS, POLES):
            variance += gi * gj / (1.0 - pi_ * pj_)
    sum_g = sum(GAINS)
    sum_g_p = sum(g * p for g, p in zip(GAINS, POLES))
    variance += 2.0 * DIRECT_GAIN * sum_g + DIRECT_GAIN**2
    variance += 2.0 * DELAY_GAIN * sum_g_p + DELAY_GAIN**2
    return variance

def pink_rms_gain_numeric() -> float:
    """Sum the 8 components' frequency responses (six one-poles + direct + delayed-direct)
    via scipy.signal.freqz, then (1/pi)*integral_0^pi |H|**2 dw (Parseval; only [0,pi]
    since the real-input spectrum is symmetric). A uniform-grid trapezoid needs an
    infeasible point count here (pole p=0.99886 sits 1.14e-3 from the unit circle, an
    O(1/N) rule needs ~2**30 points for 1e-10); scipy.integrate.quad's adaptive
    Gauss-Kronrod quadrature refines exactly where |H|**2 peaks and reaches 1e-14 in a
    few hundred evaluations instead, on this same integral and these same components.
    """
    def integrand(w: float) -> float:
        total = 0.0 + 0.0j
        for gi, pi_ in zip(GAINS, POLES):
            num, den = [gi], [1.0, -pi_]
            _, resp = scipy.signal.freqz(num, den, worN=[w])
            total += resp[0]
        _, direct = scipy.signal.freqz([DIRECT_GAIN], [1.0], worN=[w])
        total += direct[0]
        _, delayed = scipy.signal.freqz([0.0, DELAY_GAIN], [1.0], worN=[w])
        total += delayed[0]
        return abs(total) ** 2

    integral, _ = scipy.integrate.quad(integrand, 0.0, np.pi, limit=500,
                                        epsabs=1e-14, epsrel=1e-13)
    return integral / np.pi

def build_pink_rms_gain_case() -> str:
    closed_form = pink_rms_gain_closed_form()
    numeric = pink_rms_gain_numeric()
    rel_diff = abs(closed_form - numeric) / abs(closed_form)
    if rel_diff >= 1e-10:
        raise SystemExit(
            f"pink_rms_gain: closed form ({closed_form!r}) and numeric "
            f"({numeric!r}) disagree by {rel_diff:.3e} relative, >= 1e-10. "
            "Refusing to write an unverified golden value.")
    # closed_form/numeric are Var(out); "value" must be its sqrt -- the name
    # (and PinkNoise::kRmsGainVsWhite) mean the RMS gain, not the variance.
    rms_gain = closed_form ** 0.5
    print(f"  pink_rms_gain: closed_form={closed_form!r} numeric={numeric!r} "
          f"rel_diff={rel_diff:.3e} rms_gain={rms_gain!r}")
    return case_block("pink_rms_gain", 1, {
        "closed_form": closed_form, "numeric": numeric, "value": rms_gain,
    })

# --- 4. MLS: Galois-form LFSR from the shared primitive-polynomial table, checked against
# scipy's Fibonacci-form reference via FFT correlation.
# Extra tap DEGREES (not bit positions), beyond the implicit leading term x^order and the
# implicit constant term x^0 -- TAP_MASK_DEVIATION_NOTE explains bit (degree-1), not (degree).
EXTRA_TAP_DEGREES = {15: [14], 16: [15, 13, 4], 17: [14], 18: [11]}

TAP_MASK_DEVIATION_NOTE = """Deviation, verified empirically: the brief's extra-tap bit
positions read literally as final tap_mask bit indices give a DEGENERATE LFSR (order 15
cycles back to start after 15 steps, not 2**15-1). Fix: standard Galois<->polynomial
correspondence (checked against the textbook 16-bit example, mask 0xB400) -- tap_mask bit
(order-1) reinserts the discarded term; each OTHER nonzero term of degree d contributes bit
(d-1), not bit d. With that correction, every order 15-18 hits ones_count == 2**(order-1)
and the full period 2**order-1 -- both verified below before any golden row is written."""

def galois_sequence(order: int) -> tuple[np.ndarray, int]:
    mask = 1 << (order - 1)
    for degree in EXTRA_TAP_DEGREES[order]:
        mask |= 1 << (degree - 1)
    period = (1 << order) - 1
    state = 1
    seq = np.empty(period, dtype=np.int8)
    for i in range(period):
        out = state & 1
        state >>= 1
        if out:
            state ^= mask
        seq[i] = 1 if out else -1
    if state != 1:
        raise SystemExit(f"MLS order {order}: state did not return to 1 after "
                          f"the full period -- tap_mask {mask:#x} is not primitive")
    return seq, mask

def fnv1a64(bits_pm1: np.ndarray) -> int:
    h = 0xCBF29CE484222325
    prime = 0x100000001B3
    for v in bits_pm1:
        byte = 1 if v == 1 else 0
        h = ((h ^ byte) * prime) & MASK64
    return h

def find_alignment_shift(order: int, galois_seq_arr: np.ndarray,
                          scipy_seq: np.ndarray) -> tuple[int, bool]:
    """Shift s (+ orientation) such that np.roll(candidate, -s) == scipy_seq exactly, i.e.
    candidate[(i+s) % period] == scipy_seq[i] for every i. `candidate` is galois_seq_arr or
    its time-reversal -- both are tried, since a Galois and a Fibonacci LFSR for the same
    polynomial can differ by a bit-reversal as well as a phase shift. The candidate shift is
    the argmax of the circular cross-correlation (FFT), which peaks at exactly N for a
    perfect alignment of two +-1 sequences of period N -- but the argmax is only ever a
    CANDIDATE, verified by an exact np.array_equal.
    """
    period = len(galois_seq_arr)
    scipy_f = np.fft.rfft(scipy_seq.astype(np.float64))
    for reversed_ in (False, True):
        candidate = galois_seq_arr[::-1].copy() if reversed_ else galois_seq_arr
        corr = np.fft.irfft(np.fft.rfft(candidate.astype(np.float64)) * np.conj(scipy_f),
                             n=period)
        shift = int(np.argmax(corr))
        if np.array_equal(np.roll(candidate, -shift), scipy_seq):
            return shift, reversed_
    raise SystemExit(
        f"MLS order {order}: no cyclic shift (forward or reversed) aligns the "
        "Galois sequence to scipy.signal.max_len_seq's Fibonacci sequence -- "
        "the single most likely real bug in the generator module, so "
        "refusing to write a golden that can't be trusted.")

def build_mls_case(order: int) -> str:
    seq, mask = galois_sequence(order)
    ones_count = int(np.count_nonzero(seq == 1))
    expected_ones = 1 << (order - 1)
    if ones_count != expected_ones:
        raise SystemExit(f"MLS order {order}: ones_count {ones_count} != "
                          f"expected {expected_ones} -- not a balanced m-sequence")
    scipy_bits, _ = scipy.signal.max_len_seq(order)
    scipy_seq = np.where(scipy_bits == 1, 1, -1).astype(np.int8)
    shift, was_reversed = find_alignment_shift(order, seq, scipy_seq)
    # Every order 15-18 needed the reversed orientation, never forward, with this tap_mask
    # construction -- reported here, not assumed.
    print(f"  mls_{order}: tap_mask={mask:#x} shift={shift} reversed={was_reversed}")
    checksum = fnv1a64(seq)
    return case_block(f"mls_{order}", 8192, {
        "order": order, "tap_mask": mask, "head": seq[:8192],
        "ones_count": ones_count, "fnv1a_hi": checksum >> 32,
        "fnv1a_lo": checksum & 0xFFFFFFFF, "scipy_shift": shift,
    })

# --- 5. Sweep: Farina exponential sweep, rendered and deconvolved independently of the
# C++ Sweep class per the closed forms in docs/dsp/2026-08-27-generator.md.
def sweep_length_constants(f1: float, f2: float, T: float) -> tuple[float, float]:
    length_l = T / np.log(f2 / f1)
    phase_k = 2.0 * np.pi * f1 * length_l
    return length_l, phase_k

def raised_cosine_fade(x: np.ndarray, fade_in_len: int, fade_out_len: int) -> np.ndarray:
    """Taper the two ends INDEPENDENTLY, matching core's Sweep.

    One length for both ends was wrong from the start and merely invisible: it
    happened to agree with core only while 2/f1 equalled the default fadeOutSec.
    It matters now because the fade-in has an octave floor and the fade-out does
    not -- measured, a wide fade-out makes the deconvolution's artefact floor
    WORSE by about 3.5 dB per octave while a wide fade-in makes it better by
    about 19, so the two ends cannot share a number.
    """
    window = np.ones_like(x)
    # Divide by (len - 1), not len: the ramp must land EXACTLY on p=1 (gain 1.0)
    # at its last sample -- zero value AND zero slope discontinuity against the
    # flat region it hands off to, the whole reason a raised cosine is used over
    # a linear taper.
    fade_in_len = min(fade_in_len, len(x) // 2)
    fade_out_len = min(fade_out_len, len(x) // 2)
    if fade_in_len > 1:
        window[:fade_in_len] *= 0.5 * (
            1.0 - np.cos(np.pi * np.arange(fade_in_len) / (fade_in_len - 1)))
    if fade_out_len > 1:
        ramp = 0.5 * (1.0 - np.cos(np.pi * np.arange(fade_out_len) / (fade_out_len - 1)))
        window[-fade_out_len:] *= ramp[::-1]
    return x * window


def render_sweep_and_inverse(fs: float, f1: float, f2: float, T: float):
    """Both closed forms repeated verbatim from docs/dsp/2026-08-27-generator.md,
    computed here independently in Python."""
    length_l, phase_k = sweep_length_constants(f1, f2, T)
    n = np.arange(int(round(fs * T)))
    phase = phase_k * (np.exp(n / (fs * length_l)) - 1.0)
    raw = np.sin(phase)
    instantaneous_freq = f1 * np.exp(n / (fs * length_l))

    # Fade-in: three floors, widest wins -- the requested seconds, two cycles at
    # f1 (an unfaded start is a broadband click in the exact band the sweep
    # measures), and fadeInOctaves octaves of travel, which is the unit that
    # actually governs the deconvolution's pre-arrival artefact floor.
    # fadeInOctaves defaults to 2.0 in core, so it defaults to 2.0 here.
    # Fade-out: two cycles at f2, the mirror floor, and deliberately NOT given an
    # octave rule. See docs/dsp/2026-08-30-sweep-ir-l4a.md decision 5.
    fade_in_len = int(round(max(0.02, 2.0 / f1, 2.0 * np.log(2.0) * length_l) * fs))
    fade_out_len = int(round(max(0.02, 2.0 / f2) * fs))
    sweep = raised_cosine_fade(raw.copy(), fade_in_len, fade_out_len)

    # Inverse: the ALREADY-FADED sweep, shaped by f/f2 (+6 dB/oct with frequency;
    # equivalently decaying with time along the reversed signal -- the two
    # phrasings describe the same envelope), then reversed. The taper is
    # INHERITED through that reversal.
    #
    # No second fade, and no peak normalisation. An earlier version did both,
    # following docs/plans/2026-08-27-generator-impl-plan.md, which is now marked
    # superseded at that line: core/include/rta/gen/Sweep.h has always specified
    # inheritance only, and the second layer tapered the kernel's highest
    # frequencies, costing up to 72 dB of in-band flatness once the fade-in
    # widened. Normalisation cancels in every ratio these goldens take.
    inverse = (sweep * (instantaneous_freq / f2))[::-1].copy()
    return sweep, inverse


def build_sweep_params_case(fs: float, f1: float, f2: float, T: float) -> str:
    length_l, phase_k = sweep_length_constants(f1, f2, T)
    n = np.linspace(0, fs * T * 0.999, 10).astype(int)
    phase = phase_k * (np.exp(n / (fs * length_l)) - 1.0)
    return case_block("sweep_params", 1, {
        "fs": fs, "f1": f1, "f2": f2, "T": T,
        "L": length_l, "K": phase_k, "n": n, "phase": phase,
    })

def build_sweep_deconv_case(fs: float, f1: float, f2: float, T: float) -> str:
    sweep, inverse = render_sweep_and_inverse(fs, f1, f2, T)
    ir_index, ir_amp = 1000, 1.0
    ir = np.zeros(3000)
    ir[ir_index] = ir_amp
    ir[1500] = 0.5
    ir[2300] = -0.25
    rendered = scipy.signal.fftconvolve(sweep, ir)

    nfft = 262144
    conv_len = len(rendered) + len(inverse) - 1
    if conv_len > nfft:
        raise SystemExit(f"sweep_deconv: linear convolution length {conv_len} "
                          f"exceeds the chosen FFT size {nfft}")
    spectrum = np.fft.rfft(rendered, nfft) * np.fft.rfft(inverse, nfft)
    recovered = np.fft.irfft(spectrum, nfft)[:conv_len]
    peak_index = int(np.argmax(np.abs(recovered)))
    peak_amp = recovered[peak_index]

    # Exclusion windows: +/- two cycles at f1 around the main peak and each reflection (at
    # the IR's own 500- and 1300-sample offsets). The recovered "impulse" is band-limited
    # (flat only between f1 and f2), so it rings physically for about this many samples --
    # not noise, and the window is derived from f1, not tuned to look good.
    excl = int(round(2.0 * fs / f1))
    mask = np.ones(len(recovered), dtype=bool)
    for offset in (0, 500, 1300):
        centre = peak_index + offset
        lo, hi = max(0, centre - excl), min(len(recovered), centre + excl + 1)
        mask[lo:hi] = False
    residual_ms = float(np.mean(recovered[mask] ** 2))
    snr_db = 10.0 * np.log10(peak_amp**2 / residual_ms)
    if snr_db <= 60.0:
        raise SystemExit(f"sweep_deconv: measured SNR {snr_db:.2f} dB does not "
                          "exceed the 60 dB floor -- fix the sweep/inverse "
                          "construction before writing this golden")
    print(f"  sweep_deconv: peak_index={peak_index} snr_db={snr_db:.3f}")
    return case_block("sweep_deconv", 1, {
        "fs": fs, "f1": f1, "f2": f2, "T": T,
        "ir_index": ir_index, "ir_amp": ir_amp,
        # No peak_amp_norm: it was a hardcoded 1.0, derived from nothing, read
        # by nothing, and its name implied a normalisation the inverse filter
        # does not perform.
        "peak_index": peak_index, "snr_db": snr_db,
    })

def main() -> int:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    print(TAP_MASK_DEVIATION_NOTE)

    blocks = [build_pcg32_case(), build_pink_kellet_case(), build_pink_rms_gain_case()]
    for order in (15, 16, 17, 18):
        blocks.append(build_mls_case(order))
    fs, f1, f2, T = 48000.0, 100.0, 10000.0, 2.0
    blocks.append(build_sweep_params_case(fs, f1, f2, T))
    blocks.append(build_sweep_deconv_case(fs, f1, f2, T))

    header = [
        "# rta_core golden vectors -- signal generator (Prng, Noise, Sweep, Mls)",
        "# generated by tools/gen_generator.py",
        f"# numpy {np.__version__}, scipy {scipy.__version__}",
        "# DO NOT EDIT BY HAND. Regenerate and review the diff.",
        "",
    ]
    path = OUT_DIR / "generator.txt"
    path.write_text("\n".join(header) + "\n".join(blocks) + "\n", encoding="utf-8")

    print(f"wrote {path} -- {len(blocks)} cases, "
          f"numpy {np.__version__}, scipy {scipy.__version__}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
