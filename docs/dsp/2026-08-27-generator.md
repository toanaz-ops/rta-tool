# Signal generator — decision record

*2026-08-27. Station-2 analysis of the generator research pass.*

## Per-signal choices

| Signal | Choice | Rejected, and why |
|---|---|---|
| Pink | **Kellet refined 7-term IIR** (±0.05 dB over the audio band per the published bound; 7 floats state; real-time trivial). The coefficients are empirically fitted — the code comment must say so and cite the source; the citation is what satisfies the check-against-a-reference rule. Kasdin's binomial-series cascade is the recorded escalation: derivable with a published error recursion, if the fitted table ever draws a justified objection | Voss-McCartney (OSM's choice): accuracy is an empirical function of row count — makes the ±0.2 dB slope acceptance a tuning exercise instead of a designed property. FFT-shaped: exact but needs an IFFT, violating the no-FFT-in-callback rule; pre-rendering breaks the non-repeating property |
| White | uniform via **PCG32** (public spec, tiny, deterministic). The golden generator reimplements PCG32 in Python (~10 lines) so C++ and NumPy share bit-exact input sequences | hand-rolled LCGs (OSM/Friture): unverifiable statistical quality for zero savings. Gaussian white: +3 dB crest for no benefit here; documented future option |
| Sine / dual | double-precision phase accumulator + std::sin per sample | recursive oscillators: amplitude drift needs renormalisation — an extra failure mode to test, for CPU nobody is short of at 1-2 tones |
| Sweep | Farina exponential: phase(t) = K(e^(t/L)−1), L = T/ln(f2/f1), K = 2π·f1·L, evaluated per sample from the sample counter (one exp + one sin). Inverse filter = time-reversed sweep + **+6 dB/oct envelope** (reversal keeps the −3 dB/oct magnitude; +6 nets the required +3), built off-thread on parameter change. Mandatory raised-cosine fades — an unfaded start at f1 is a broadband click that pollutes the exact band the sweep exists to measure | linear sweep: harmonic products do not collapse to the constant pre-arrival offset Δt = L·ln(n) that lets one fixed window separate distortion orders |
| MLS | Galois-form LFSR, orders 15-18 from the standard tap table, default 17; sequence pinned bit-exact against scipy.signal.max_len_seq | treating MLS as the default IR tool: nonlinearity smears across the whole recovered IR with no sweep-style separation; kept for periodic-averaging workflows |

## Level conventions (recorded because the surveyed tools disagree silently)

- **Noise is RMS-referenced dBFS** (REW's documented behaviour; SMPTE ST 2095-1
  fixes calibration pink at 11.5-12 dB crest). Default cap −12 dBFS RMS so pink
  crest never clips. OSM/Friture appear to apply gain as a bare linear factor —
  a peak-ish control that reads like a calibrated level but is not.
- **Deterministic signals (sine, dual, sweep, MLS) are peak-referenced dBFS** —
  their crest factors are closed forms, so peak hides nothing.
- Same shape as Window.h's ACF/ECF precedent: two conventions, named, never
  unified for tidiness.

## Real-time and multichannel rules

- Click-free start/stop: 5-20 ms raised-cosine ramp run by a per-sample state
  machine in the callback; UI sets a target atomically, audio thread ramps.
- Decorrelated multichannel: per-channel PRNG state, seeds derived from one
  master seed via SplitMix64 — reproducible from a single number, channels
  statistically independent. Correlated (duplicated) noise stays available as an
  explicit option for mono-compatibility checks.

## Test tiers

- Closed form: sine phase progression; sweep instantaneous frequency
  f(t) = f1·e^(t/L); K and L from (f1, f2, T); MLS period 2^n − 1 and the exact
  bit sequence; dual-sine IM product locations.
- Golden: pink filter output for a fixed PCG32 input sequence (catches
  coefficient transcription — a slope test cannot); MLS vs max_len_seq; sweep →
  synthetic IR → deconvolution SNR > 60 dB.
- Statistical: −3.01 dB/oct ±0.2; 1/3-octave flatness ±0.5 dB; crest ranges;
  cross-channel correlation ≈ 0.
