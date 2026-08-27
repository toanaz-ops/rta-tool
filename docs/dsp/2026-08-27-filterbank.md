# IEC 61260 filter bank — decision record

*2026-08-27. Station-2 analysis. Every number below was reproduced by the
research pass against this repo's own .venv (scipy 1.18.1), not quoted.*

## Decision: single-rate SOS bank. No decimation cascade.

This overturns the earlier assumption in 2026-08-26-banding-and-averaging.md
that the filter bank "needs a decimation cascade so the low bands are not
computed at 48 kHz". Measured: the full 1/3-octave bank (≈30 bands, 6 sections
each) at 48 kHz costs ~43 M multiply-adds/s — irrelevant on the analysis thread
of a desktop CPU. Friture and phonometry decimate for CPU budgets this project
does not have, and both pay with exactly the per-band group-delay bookkeeping
our own doc flagged as a Phase-4 trap. Single-rate removes that trap instead of
managing it. Decimation returns only with a profiler result in hand.

## Filter: Butterworth, 6 second-order sections per band (12-pole band-pass)

**Order convention, pinned because the libraries disagree:** `order` here means
scipy's N = the analog low-pass prototype order = the SOS-section count = HALF
the true band-pass pole count. phonometry's "order 6" means this; python-
acoustics' "order 8" means the true order. Reading one library's number into
another's parameter silently builds a different filter.

Verified independently at 1 kHz / 48 kHz: N=6 passes every ANSI S1.11 Table B1
Class 1 breakpoint with margin (3.011 dB against the 5.0 dB ceiling at
f/fm=1.122; 92 dB against the 17.5 dB floor at 1.882). Chebyshev II reaches
class 1 only at 72 dB attenuation; Chebyshev I, elliptic and Bessel fail the
mask outright. Friture's per-band elliptic order-2 is a CPU choice, not a
conformance choice, and is not precedent here.

## The representation rule: ZPK → SOS, never a transfer-function polynomial

Reproduced, not cited: for the 20 Hz third-octave band at 48 kHz the direct-form
(ba) polynomial DIVERGES TO NaN in double precision, while the identical filter
as SOS stays bounded even in float32. The roots of the ba denominator come back
with |root| > 1 — the coefficient vector no longer represents the designed
filter at all. Poles sit ~7e-5 from the unit circle there, and that is the
normal operating point of the bottom third of the band table, not an edge case.

Hence: coefficients and states live as SOS in double, always. And no debugging
or golden-vector script may call scipy with output='ba' "just to check" — for
the low bands that output is garbage that will masquerade as a project bug.

## Design chain (closed-form, no scipy at runtime)

buttap poles p_k = -exp(j*pi*(2k-N-1)/(2N)) → prewarp both edges with
2*fs*tan(pi*f/fs) → LP-to-BP substitution (doubles the order; N zeros to the
origin, N to infinity) → bilinear in ZPK form → nearest-neighbour pole/zero
pairing into sections ordered by pole radius. Runtime is Direct Form II
Transposed per section. Full step list with formulas is in the research
transcript and goes verbatim into the implementation plan.

## Verification tiers

1. **Closed form / standard clause (CI, no fixtures):** every band's response
   evaluated at the Table B1 breakpoint ratios scaled by its own centre must sit
   inside the Class 1 window; every pole radius < 1. ANSI S1.11-2004 is publicly
   readable and nationally identical to IEC 61260-1.
2. **Golden vectors (scipy):** SOS coefficients for lowest/mid/highest bands
   (catches pairing-order bugs), sosfilt impulse responses (catches DF2T state
   bugs), band powers on white noise and on-centre tones.
3. A point in this path's favour, stated for the record: band power here is the
   mean square of a filtered time signal — there is NO windowing/ENBW correction
   to get wrong, unlike the FFT path.
