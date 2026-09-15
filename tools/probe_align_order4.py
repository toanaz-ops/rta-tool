#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""L7-ALIGN Sec.13.1: the independent grid cell for the order-4 sign contradiction.

    .venv/Scripts/python.exe tools/probe_align_order4.py --section all

Writes nothing. Every section is opt-in through argparse, and NOTHING runs on
import -- `memory/a-gen-script-runs-the-moment-you-invoke-it.md`: the L4a-era
`tools/gen_*.py` had no argparse at all, so `--help` ran the pipeline and
overwrote a golden.

## The contradiction this exists to settle

`docs/dsp/2026-09-06-l7-alignment-wizard.md` Sec.3 states a closed form:

    H_HP(s) / H_LP(s) = s^N   for a matched-cutoff Butterworth pair,
    so arg H_HP - arg H_LP = N * 90 degrees at EVERY frequency.

At N = 4 that is 360 degrees, i.e. in phase, so a cross-correlation of the two
impulse responses should peak POSITIVE for a correctly wired pair.
`docs/dsp/2026-08-30-sweep-ir-l4a.md` ("What G21 promises", amendment 2)
records the opposite: a correctly wired sub/main pair "reads the opposite sign
at crossover orders 2 and 4, and the sign flips again at orders 1 and 8".

Orders 1, 2 and 8 agree with the identity (90 deg is a null, 180 deg is a
negative peak, 720 deg is in phase). Order 4 does not. The L4a fixture is not
in the repo -- `tools/probe_polarity_*.py` are absolute-polarity probes on
single band-pass boxes and none of them correlates two systems -- so the
finding cannot be re-read from a committed artefact. This script rebuilds the
question from scratch instead.

## Sections

  identity  the closed form itself, analog (zpk, s-plane) and digital (SOS,
            48 kHz), Butterworth N = 1..8 and Linkwitz-Riley 2/4/8.
  sum       |H_HP + H_LP| at fc, with and without one polarity flip -- the row
            the station-1 research got backwards (D1/D6 say the UN-inverted
            BW2 sum peaks +3 dB; Sec.1 of the ALIGN record says it is a null).
  xcorr     the L4a rule -- sign of the cross-correlation peak between the two
            impulse responses -- across FIVE filter geometries, only one of
            which is the matched-cutoff crossover the identity describes.
  repo      the same pair read through this repo's dual-FFT convention
            (Sxy = conj(X) * Y, phase = atan2(Im h, Re h)), and through the
            conjugate of it, to see whether a convention flip can even reach
            the order-4 sign.

Filters are second-order sections or zeros/poles/gain throughout.
Transfer-function (polynomial) form is prohibited repo-wide by
`core/tests/check_no_polynomial_form.cmake`; its patterns match a CALL SITE,
so this docstring describes the prohibition without spelling it.
"""

from __future__ import annotations

import argparse
import sys

import numpy as np
import scipy.fft
import scipy.signal

FS = 48000.0


def _freqz_sos(sos, worN, fs):
    """scipy renamed sosfreqz to freqz_sos; accept either."""
    fn = getattr(scipy.signal, "freqz_sos", None) or scipy.signal.sosfreqz
    return fn(sos, worN=worN, fs=fs)


def wrap_deg(x):
    """Wrap to (-180, 180]."""
    return -((-np.asarray(x) + 180.0) % 360.0 - 180.0)


# --------------------------------------------------------------------------
# analog prototypes: zeros / poles / gain, evaluated on s = j*omega
# --------------------------------------------------------------------------

def analog_zpk(order, btype, wc=1.0):
    return scipy.signal.butter(order, wc, btype, analog=True, output="zpk")


def eval_zpk(zpk, s):
    z, p, k = zpk
    num = np.ones_like(s, dtype=complex) * k
    for zi in z:
        num = num * (s - zi)
    den = np.ones_like(s, dtype=complex)
    for pi in p:
        den = den * (s - pi)
    return num / den


def analog_pair(order, family):
    """(low-pass zpk, high-pass zpk) for one topology at wc = 1 rad/s.

    Linkwitz-Riley of order N is the Butterworth of order N/2 cascaded with
    itself (Linkwitz 1976), so its zpk is the Butterworth one with every list
    doubled -- still zeros/poles/gain, never a polynomial.
    """
    if family == "butter":
        return analog_zpk(order, "low"), analog_zpk(order, "high")
    if family == "lr":
        if order % 2:
            raise ValueError("Linkwitz-Riley order must be even")
        lo = analog_zpk(order // 2, "low")
        hi = analog_zpk(order // 2, "high")
        square = lambda t: (np.concatenate([t[0], t[0]]),
                            np.concatenate([t[1], t[1]]), t[2] * t[2])
        return square(lo), square(hi)
    raise ValueError(f"unknown family {family!r}")


# --------------------------------------------------------------------------
# digital prototypes: second-order sections at 48 kHz
# --------------------------------------------------------------------------

def digital_pair(order, family, fc, fs=FS):
    if family == "butter":
        return (scipy.signal.butter(order, fc, "low", fs=fs, output="sos"),
                scipy.signal.butter(order, fc, "high", fs=fs, output="sos"))
    if family == "lr":
        if order % 2:
            raise ValueError("Linkwitz-Riley order must be even")
        lo = scipy.signal.butter(order // 2, fc, "low", fs=fs, output="sos")
        hi = scipy.signal.butter(order // 2, fc, "high", fs=fs, output="sos")
        return np.vstack([lo, lo]), np.vstack([hi, hi])
    raise ValueError(f"unknown family {family!r}")


# --------------------------------------------------------------------------
# section: identity
# --------------------------------------------------------------------------

def section_identity(args):
    print("=" * 78)
    print("SECTION 1 -- arg H_HP - arg H_LP, against the closed form N * 90 deg")
    print("=" * 78)
    print()
    print("  ANALOG, matched cutoff wc = 1 rad/s, 101 points over 0.01 .. 100 rad/s")
    print("  (s-plane, zeros/poles/gain evaluated directly -- no bilinear transform)")
    print()
    w = np.logspace(-2, 2, 101)
    s = 1j * w
    print(f"   {'topology':>10s} {'N':>3s} {'predicted':>10s} {'measured':>10s}"
          f" {'max dev':>12s}")
    rows = []
    for family, orders in (("butter", range(1, 9)), ("lr", (2, 4, 8))):
        for n in orders:
            lo, hi = analog_pair(n, family)
            d = np.degrees(np.angle(eval_zpk(hi, s) / eval_zpk(lo, s)))
            pred = wrap_deg(n * 90.0)
            dev = float(np.max(np.abs(wrap_deg(d - n * 90.0))))
            name = "BW" if family == "butter" else "LR"
            print(f"   {name:>10s} {n:>3d} {pred:>9.2f}d {float(np.median(d)):>9.2f}d"
                  f" {dev:>11.2e}d")
            rows.append((name, n, dev))
    worst = max(r[2] for r in rows)
    print()
    print(f"  worst deviation anywhere in the analog grid: {worst:.3e} degrees")
    print("  -> the identity is EXACT in the s-plane, every order, every frequency.")

    print()
    print(f"  DIGITAL, second-order sections at {args.fs:.0f} Hz, 200 points 20 Hz .. 20 kHz")
    print("  Reason it must be checked separately: the bilinear transform is not")
    print("  required to preserve a ratio of two filters. It does here, because a")
    print("  matched-cutoff pair shares its denominator exactly and the numerators")
    print("  are (1 + z^-1)^N and (1 - z^-1)^N, whose ratio on the unit circle is")
    print("  (j tan(w/2))^N -- still N * 90 deg for every 0 < w < pi.")
    print()
    f = np.logspace(np.log10(20.0), np.log10(20000.0), 200)
    f = f[f < 0.5 * args.fs]
    for fc in args.fc:
        print(f"   crossover fc = {fc:.0f} Hz")
        print(f"     {'topology':>10s} {'N':>3s} {'predicted':>10s} {'measured':>10s}"
              f" {'max dev':>12s}")
        for family, orders in (("butter", range(1, 9)), ("lr", (2, 4, 8))):
            for n in orders:
                lo, hi = digital_pair(n, family, fc, args.fs)
                _, hl = _freqz_sos(lo, f, args.fs)
                _, hh = _freqz_sos(hi, f, args.fs)
                d = np.degrees(np.angle(hh / hl))
                dev = float(np.max(np.abs(wrap_deg(d - n * 90.0))))
                name = "BW" if family == "butter" else "LR"
                print(f"     {name:>10s} {n:>3d} {wrap_deg(n * 90.0):>9.2f}d"
                      f" {float(np.median(d)):>9.2f}d {dev:>11.2e}d")
    print()
    print("  RULING FOR THIS SECTION: confirmed, not refuted. The N*90 identity")
    print("  holds analog and digital, Butterworth and Linkwitz-Riley, to machine")
    print("  precision. Nothing about order 4 is special in the topology itself.")
    print()


# --------------------------------------------------------------------------
# section: sum
# --------------------------------------------------------------------------

def section_sum(args):
    print("=" * 78)
    print("SECTION 2 -- |H_HP + H_LP| at fc, with and without one polarity flip")
    print("=" * 78)
    print()
    print("  D1/D6 of the station-1 research say the UN-inverted BW2 sum peaks")
    print("  +3 dB. ALIGN Sec.1 correction 1 says it is a NULL and the +3 dB")
    print("  belongs to the inverted sum. One of the two is wrong; this decides it.")
    print()
    fc = args.fc[0]
    f = np.array([fc])
    print(f"   crossover fc = {fc:.0f} Hz, {args.fs:.0f} Hz, second-order sections")
    print(f"   {'topology':>10s} {'N':>3s} {'HP-LP':>8s} {'sum as-is':>12s}"
          f" {'sum flipped':>14s}")
    for family, orders in (("butter", range(1, 9)), ("lr", (2, 4, 8))):
        for n in orders:
            lo, hi = digital_pair(n, family, fc, args.fs)
            _, hl = _freqz_sos(lo, f, args.fs)
            _, hh = _freqz_sos(hi, f, args.fs)
            offset = float(np.degrees(np.angle(hh[0] / hl[0])))
            plain = 20.0 * np.log10(max(abs(hl[0] + hh[0]), 1e-300))
            flipped = 20.0 * np.log10(max(abs(hl[0] - hh[0]), 1e-300))
            name = "BW" if family == "butter" else "LR"
            print(f"   {name:>10s} {n:>3d} {offset:>7.1f}d {plain:>11.2f}dB"
                  f" {flipped:>13.2f}dB")
    print()
    print("  Read the BW2 row: the un-inverted sum is the null (a number near")
    print("  -300 dB is the double-precision floor of an exact cancellation) and")
    print("  the inverted sum is +3.01 dB. ALIGN Sec.1 correction 1 is right and")
    print("  research D1/D6 are backwards.")
    print()


# --------------------------------------------------------------------------
# section: xcorr -- the L4a rule, across five filter geometries
# --------------------------------------------------------------------------

def impulse_response(sos, n):
    x = np.zeros(n)
    x[0] = 1.0
    return scipy.signal.sosfilt(sos, x)


def band_sos(order, lo, hi, fs=FS):
    return scipy.signal.butter(order, [lo, hi], "bandpass", fs=fs, output="sos")


def geometries(order, fs=FS):
    """Five ways two loudspeakers can be filtered, only one of which is the
    matched-cutoff complementary crossover the Sec.3 identity describes.

    G1/G2 are that pair. G3..G5 are what a survey of BAND-PASS boxes -- which
    is what `tools/probe_polarity_bandwidth.py` builds, and the only box model
    this repo has ever committed -- produces when two of them are correlated
    against each other.
    """
    lp100 = scipy.signal.butter(order, 100.0, "low", fs=fs, output="sos")
    hp100 = scipy.signal.butter(order, 100.0, "high", fs=fs, output="sos")
    lp1k = scipy.signal.butter(order, 1000.0, "low", fs=fs, output="sos")
    hp1k = scipy.signal.butter(order, 1000.0, "high", fs=fs, output="sos")
    return [
        ("G1 matched crossover 100 Hz", lp100, hp100),
        ("G2 matched crossover 1 kHz", lp1k, hp1k),
        ("G3 boxes, overlapping     ", band_sos(order, 30.0, 120.0, fs),
         band_sos(order, 100.0, 8000.0, fs)),
        ("G4 boxes, edges touching  ", band_sos(order, 30.0, 100.0, fs),
         band_sos(order, 100.0, 8000.0, fs)),
        ("G5 sub LP + main band-pass", lp100,
         band_sos(order, 100.0, 8000.0, fs)),
    ]


def xcorr_sign(a, b, phat=False):
    """Sign of the cross-correlation peak of b against a, plus rho and lag.

    rho = |peak| / sqrt(E_a * E_b) -- the bounded figure L4a decision 6b
    settled on, Cauchy-Schwarz in [0, 1].
    """
    n = 1 << int(np.ceil(np.log2(len(a) + len(b))))
    A = scipy.fft.rfft(a, n)
    B = scipy.fft.rfft(b, n)
    cross = np.conj(A) * B
    if phat:
        mag = np.abs(cross)
        cross = np.divide(cross, mag, out=np.zeros_like(cross), where=mag > 0)
    r = scipy.fft.irfft(cross, n)
    idx = int(np.argmax(np.abs(r)))
    lag = idx if idx <= n // 2 else idx - n
    peak = float(r[idx])
    denom = float(np.sqrt(np.sum(a * a) * np.sum(b * b)))
    rho = abs(peak) / denom if denom > 0 else 0.0
    return int(np.sign(peak)), lag, (rho if not phat else float("nan")), float(r[0])


def section_xcorr(args):
    print("=" * 78)
    print("SECTION 3 -- the L4a rule: sign of the cross-correlation peak")
    print("=" * 78)
    print()
    print("  'Correctly wired' means both sources driven with the SAME sign, so a")
    print("  correlation peak that comes out NEGATIVE is the 'wrong sign' L4a")
    print("  reported. The identity predicts, per order N:")
    print("     N = 1, 3, 5, 7 : quadrature -- the value at lag 0 is exactly 0,")
    print("                      so the peak lands a quarter cycle to one side and")
    print("                      its sign is whichever side wins. UNDEFINED.")
    print("     N = 2, 6       : 180 deg -> NEGATIVE peak (wrong sign, correctly)")
    print("     N = 4, 8       : 0 deg   -> POSITIVE peak (right sign)")
    print()
    n = int(args.ir_seconds * args.fs)
    orders = [int(o) for o in args.orders]
    first = geometries(orders[0], args.fs)
    for gi, (gname, _, _) in enumerate(first):
        print(f"   {gname}")
        print(f"     {'N':>3s} {'peak sign':>10s} {'lag':>7s} {'rho':>7s}"
              f" {'r[0] sign':>10s} {'PHAT sign':>10s}   verdict")
        for order in orders:
            _, sub_sos, main_sos = geometries(order, args.fs)[gi]
            a = impulse_response(sub_sos, n)
            b = impulse_response(main_sos, n)
            sign, lag, rho, r0 = xcorr_sign(a, b)
            psign, plag, _, _ = xcorr_sign(a, b, phat=True)
            verdict = "RIGHT" if sign > 0 else ("WRONG" if sign < 0 else "zero")
            print(f"     {order:>3d} {sign:>10d} {lag:>7d} {rho:>7.4f}"
                  f" {int(np.sign(r0)):>10d} {psign:>10d}   {verdict}")
        print()
    print("  What to read here: G1/G2 are the topology the Sec.3 identity")
    print("  describes and they follow it. Any geometry that reads WRONG at BOTH")
    print("  order 2 and order 4 is a candidate for what the L4a fixture was.")
    print()
    print("  READ THE 'PHAT sign' COLUMN -- it is not decoration. 'peak sign' is")
    print("  the UN-WHITENED rule, which is what L4a decision 6b describes")
    print("  (rho = |peak| / sqrt(E1*E2), sweep-ir-l4a.md:1195) and what this")
    print("  repo does NOT ship -- relativePolarity() exists in no file. The")
    print("  correlator this repo DOES ship is PHAT-whitened findDelayPhat")
    print("  (DelayFinder.cpp:34), and on G3/G4 it reads the MIRROR: +1 at order")
    print("  4 where the plain peak reads -1, and -1 at order 8 where the plain")
    print("  peak reads +1. Whitening does not fix the rule, it relocates the")
    print("  failure. Two correlators, one unchanged pair, opposite verdicts at")
    print("  the two orders the question turns on -- which is why the ruling")
    print("  bans reading a topology sign off ANY correlation peak rather than")
    print("  preferring one of them.")
    print()


# --------------------------------------------------------------------------
# section: repo -- this codebase's own transfer-function convention
# --------------------------------------------------------------------------

def section_repo(args):
    print("=" * 78)
    print("SECTION 4 -- the same pair through this repo's dual-FFT convention")
    print("=" * 78)
    print()
    print("  core/src/dsp/DualFftEngine.cpp:252   Sxy = conj(X) * Y, X = reference")
    print("  core/src/dsp/TransferEstimator.cpp:108  phase = atan2(Im h, Re h)")
    print("  so arg H = arg Y - arg X, and with X the common electrical drive the")
    print("  measured offset IS arg H_HP - arg H_LP with no sign to get wrong.")
    print()
    fc = args.fc[0]
    n = int(args.ir_seconds * args.fs)
    nfft = 1 << int(np.ceil(np.log2(n)))
    f = scipy.fft.rfftfreq(nfft, 1.0 / args.fs)
    band = (f >= 0.25 * fc) & (f <= 4.0 * fc)
    drive = np.zeros(n)
    drive[0] = 1.0
    X = scipy.fft.rfft(drive, nfft)
    print(f"   crossover fc = {fc:.0f} Hz, offset read over {0.25 * fc:.0f}"
          f" .. {4.0 * fc:.0f} Hz")
    print(f"   {'N':>3s} {'predicted':>10s} {'repo conv.':>12s}"
          f" {'conjugated':>12s} {'reachable by a flip?':>22s}")
    for order in [int(o) for o in args.orders]:
        lo, hi = digital_pair(order, "butter", fc, args.fs)
        yl = impulse_response(lo, n)
        yh = impulse_response(hi, n)
        Yl = scipy.fft.rfft(yl, nfft)
        Yh = scipy.fft.rfft(yh, nfft)
        sxx = np.abs(X) ** 2
        h_lo = np.conj(X) * Yl / sxx
        h_hi = np.conj(X) * Yh / sxx
        repo = np.degrees(np.angle(h_hi[band] / h_lo[band]))
        # The convention memory/dual-fft-conventions.md item 1 forbids:
        # Sxy = X * conj(Y), which conjugates the whole trace's phase.
        conj_conv = -repo
        pred = wrap_deg(order * 90.0)
        med = float(np.median(repo))
        medc = float(np.median(conj_conv))
        same = abs(float(wrap_deg(med - medc))) < 1e-6
        note = "NO -- both read the same" if same else "yes -- sign differs"
        print(f"   {order:>3d} {pred:>9.2f}d {med:>11.2f}d {medc:>11.2f}d"
              f" {note:>22s}")
    print()
    print("  The last column is the load-bearing one. Conjugating the transfer")
    print("  function negates the offset, and -0 = 0 and -180 = 180 modulo 360.")
    print("  So NO phase-sign, conj-placement or delay-sign convention in this")
    print("  engine can move an EVEN-order crossover's reading from right to")
    print("  wrong. Only the odd orders, where the offset is +-90, are reachable")
    print("  by a convention flip -- and those are exactly the orders whose")
    print("  correlation peak has no defined sign anyway.")
    print()


# --------------------------------------------------------------------------
# section: forensic -- WHY the band-passed geometry reads backwards at order 4
# --------------------------------------------------------------------------

def section_forensic(args):
    print("=" * 78)
    print("SECTION 5 -- forensics on the geometry that reproduces the L4a report")
    print("=" * 78)
    print()
    print("  Two questions, both answered per geometry:")
    print("   (a) is the HP-LP offset CONSTANT in f over the band where the two")
    print("       overlap? The Sec.3 identity says it is, for a matched pair. A")
    print("       band-passed source has skirts of its own and it is not.")
    print("   (b) does the correlation peak sit AT lag 0? The identity only ever")
    print("       constrains the value at lag 0. If a longer, flatter correlation")
    print("       envelope lets a neighbouring lobe win, the peak-sign rule reads")
    print("       that lobe's sign instead -- and adjacent lobes alternate.")
    print()
    n = int(args.ir_seconds * args.fs)
    nfft = 1 << int(np.ceil(np.log2(n)))
    f = scipy.fft.rfftfreq(nfft, 1.0 / args.fs)
    for order in [int(o) for o in args.orders]:
        print(f"   order {order}")
        print(f"     {'geometry':>28s} {'overlap band':>18s} {'offset spread':>14s}"
              f" {'sign@0':>7s} {'sign@pk':>8s} {'pk lag':>9s} {'|pk|/|r0|':>10s}")
        for gname, sub_sos, main_sos in geometries(order, args.fs):
            _, hs = _freqz_sos(sub_sos, f[1:], args.fs)
            _, hm = _freqz_sos(main_sos, f[1:], args.fs)
            ms = np.abs(hs)
            mm = np.abs(hm)
            ref = max(ms.max(), mm.max())
            # "overlap" = both within 10 dB of each other AND above -40 dB of
            # the louder one. This is the band the summation cross-term
            # 2|H_A||H_B|cos(dphi) can actually move (ALIGN Sec.4).
            live = (ms > ref * 1e-2) & (mm > ref * 1e-2)
            ratio_db = np.zeros_like(ms)
            np.divide(ms, mm, out=ratio_db, where=live)
            np.log10(ratio_db, out=ratio_db, where=live & (ratio_db > 0))
            close = live & (np.abs(20.0 * ratio_db) <= 10.0)
            if not np.any(close):
                band_txt = "none"
                spread = float("nan")
            else:
                fl = f[1:][close]
                band_txt = f"{fl.min():6.1f}-{fl.max():7.1f}Hz"
                d = np.degrees(np.angle(hm[close] / hs[close]))
                spread = float(np.max(wrap_deg(d - np.median(d)))
                               - np.min(wrap_deg(d - np.median(d))))
            a = impulse_response(sub_sos, n)
            b = impulse_response(main_sos, n)
            nn = 1 << int(np.ceil(np.log2(2 * n)))
            r = scipy.fft.irfft(np.conj(scipy.fft.rfft(a, nn))
                                * scipy.fft.rfft(b, nn), nn)
            idx = int(np.argmax(np.abs(r)))
            lag = idx if idx <= nn // 2 else idx - nn
            ratio = abs(r[idx]) / abs(r[0]) if r[0] != 0 else float("inf")
            print(f"     {gname:>28s} {band_txt:>18s} {spread:>13.1f}d"
                  f" {int(np.sign(r[0])):>7d} {int(np.sign(r[idx])):>8d}"
                  f" {1000.0 * lag / args.fs:>8.2f}ms {ratio:>10.3g}")
        print()
    print("  A matched pair reads a spread of ~0 deg and a peak at lag 0.00 ms.")
    print("  A pair of band-pass boxes reads tens of degrees of spread, and at")
    print("  the orders where the peak leaves lag 0 the reported sign is the")
    print("  sign of a DIFFERENT lobe -- which is the order-4 'wrong sign'.")
    print()
    print("  A |pk|/|r0| in the 1e12..1e17 range is not a large ratio, it is a")
    print("  ZERO denominator: at the odd orders the matched pair is in exact")
    print("  quadrature, r[0] is numerically 0, and the 'sign at lag 0' printed")
    print("  beside it is the sign of rounding noise. That column is the")
    print("  quadrature proof, not a measurement of anything.")
    print()


# --------------------------------------------------------------------------

SECTIONS = {
    "identity": section_identity,
    "sum": section_sum,
    "xcorr": section_xcorr,
    "repo": section_repo,
    "forensic": section_forensic,
}


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="L7-ALIGN Sec.13.1 probe: the order-4 phase-sign contradiction.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="Writes nothing. Pick a section or pass --section all.")
    parser.add_argument("--section", default="all",
                        choices=list(SECTIONS) + ["all"],
                        help="which block to run (default: all)")
    parser.add_argument("--fs", type=float, default=FS,
                        help="sample rate in Hz (default: 48000)")
    parser.add_argument("--fc", type=float, nargs="+", default=[100.0, 1000.0],
                        help="crossover frequencies in Hz (default: 100 1000)")
    parser.add_argument("--orders", type=int, nargs="+",
                        default=[1, 2, 3, 4, 6, 8],
                        help="Butterworth orders to sweep (default: 1 2 3 4 6 8)")
    parser.add_argument("--ir-seconds", type=float, default=1.0,
                        help="impulse-response length in seconds (default: 1.0)")
    args = parser.parse_args(argv)

    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except (AttributeError, OSError):
        pass

    print("L7-ALIGN order-4 probe -- docs/research/2026-09-15-l7-align-order4-probe.md")
    print(f"  numpy {np.__version__}, scipy {scipy.__version__}, "
          f"fs {args.fs:.0f} Hz")
    print()
    names = list(SECTIONS) if args.section == "all" else [args.section]
    for name in names:
        SECTIONS[name](args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
