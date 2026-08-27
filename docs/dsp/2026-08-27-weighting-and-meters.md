# A/C/Z weighting, detectors, Leq/Ln, true-peak — decision record

*2026-08-27. Station-2 analysis. Research pass measured everything below against
the repo's own .venv (scipy + python-acoustics); numbers are reproducible, not
quoted from memory.*

## Weighting filter: bilinear SOS cascade, error recorded, compensation deferred

The analog definition (IEC 61672-1 Annex E): poles at f1=20.598997,
f2=107.65265, f3=737.86223, f4=12194.217 Hz; A normalised by −2.000 dB at 1 kHz,
C by −0.062 dB. The analytic curve reproduces the standard's Table 3 within
0.05 dB at the EXACT third-octave frequencies (1000·10^(0.1n)) — using nominal
frequencies instead introduces up to 0.28 dB of spurious "error", a trap the
tests must avoid.

Digitisation choice — measured error of plain bilinear (no prewarp) at 48 kHz:

| f | error | Class 1 envelope (corroborated points) |
|---|---|---|
| ≤4 kHz | ≈0 | ±1.1..±1.9 |
| 8 kHz | −0.54 | wide |
| 10 kHz | −1.22 | +2.6/−3.6 ← eats most of the margin |
| 12.5 kHz | −2.67 | (official limit not yet obtained) |
| 16 kHz | −6.43 | +3.5/**−17** — passes easily |

Matched-Z halves the 16 kHz error (−2.9) but adds +0.03..+1.7 dB where bilinear
was exact. Oversample-filter-decimate pushes the sag out of band at real CPU
cost. **Chosen: bilinear SOS, same as python-acoustics** — so golden vectors and
the reference implementation agree about what "correct" means — with the error
table above committed to this file. Escalation to matched-Z or 2x oversampling
happens only if the official 12.5 kHz Class 1 limit, once obtained, is violated.
An honesty rule follows: the app must not claim "Class 1" anywhere; the claim is
"analytic weighting within 0.05 dB of Table 3; digital filter error as
published in this table". Superiority here is provability, not an unverifiable
conformance badge.

**Open item (blocks a conformance CI test, nothing else):** the full IEC 61672-1
Table 2 tolerance list is paywalled; only 20/1k/10k/16k Hz points are
corroborated. Buy or source the table before writing any test that says
"Class 1".

## Detectors: one-pole on the mean square; closed-form tests

Fast τ=125 ms, Slow τ=1 s, exponential mean square (IEC 61672-1 clause 5).
Tests need no golden vectors:

- step response: ΔL(t) = 10·log10(1 − e^(−t/τ)); at t=τ exactly −1.9895 dB
- decay: linear in dB at 10·log10(e)/τ = 4.3429/τ dB/s → 34.74 (F), 4.343 (S)

Impulse (35 ms rise / 1.5 s decay, 2.895 dB/s) is legacy — outside current IEC
normative scope. Implemented as the two-τ asymmetric approximation and labelled
an approximation of the historical circuit, which none of the surveyed
implementations reproduce either.

## Leq: closed form. Ln: a convention this project must pick, so it is picked here

Leq = 10·log10(mean(p²)/p0²); piecewise signals have the exact energy sum
10·log10(Σ dutyᵢ·10^(Lᵢ/10)). Peak level is 10·log10(max|p|²/p0²) — note the
squared form, a factor-of-2-in-dB transcription trap.

Ln (L10/L50/L90) is not in IEC 61672-1 and python-acoustics does not implement
it; conventions differ by sampling interval and interpolation. **Project
convention, recorded here as the decision:** percentiles are taken over
Fast-weighted levels sampled every 100 ms, with linear interpolation between
order statistics. Rationale: matches the "short-Leq/Fast at ≥10 samples/s"
practice described by instrument vendors, and interpolation makes the value
stable as the observation window grows. The UI must label the convention.

## True-peak: 4x polyphase, verified by closed-form inter-sample peaks

BS.1770's own 48-tap table could not be extracted (dyadic-rational coefficients
confirmed; full table paywalled/unscannable). Rather than transcribe uncertain
numbers: design a 4-phase windowed-sinc interpolator, golden-vector it against
scipy.signal.resample_poly, and pin it with the closed form that matters: a sine
at fs/4 with phase π/4 puts its true peak exactly between samples — the sampled
peak reads 20·log10(cos(π/4)) = −3.01 dB low, and the interpolator must recover
it to within 0.1 dB. That tests the property true-peak metering exists for,
independent of any coefficient table. Deferred to the meter build's second
commit; peak/RMS ship first.

## Rejected

- Matched-Z as primary (berndporr's choice): fixes the band the tolerance is
  loosest in, worsens the band it is tightest in.
- Long FIR matched to Table 3: arbitrary accuracy nobody needs at latency and
  coefficient cost; phase is unconstrained by the standard.
- Transcribing BS.1770 coefficients from unverifiable fragments: a bit-exact
  table copied wrongly is worse than a derived design verified by property.
