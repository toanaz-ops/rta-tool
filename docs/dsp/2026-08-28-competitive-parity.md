# Competitive parity ledger — Smaart v9 Suite ∪ RiTA

*2026-08-28. Station-2 analysis of the competitive research pass. This file is
the checklist the spec's "feature floor" is measured against. Every claim in
the underlying research carries a URL; see the research transcript. RiTA is
RiTA by G Audio Solutions (gaudiosolutions.com), confirmed by the owner's link,
with the free real-time companion Little RiTA.*

## Already covered by the existing roadmap (no action)

RTA 1/1..1/48 + A/C/Z, SPL Fast/Slow/Leq/Ln, generator suite, calibration
(P1) · TF magnitude/phase/coherence, delay finder + auto-tracking, live IR
(P2) · MTW (P3) · swept-sine measurement + IR analysis, RT60/EDT, C50/C80/D50
(P4) · spectrograph, target curves, traces (P5) · multichannel routing, remote
API (P6).

## Gaps adopted into the roadmap (owner's floor directive, 2026-08-28)

| # | Gap | Source | Lands in |
|---|---|---|---|
| G1 | FIFO averaging (2..16 ring) alongside Welch/exponential | Smaart RT | **P2** (small, same engine surface) |
| G2 | MTW and fixed-FFT engines running CONCURRENTLY on one TF | Smaart v9 | **P3** (scope amendment: the MTW engine sits beside, not instead of, the fixed engine — the P2 interface already anticipated swappable engines; now it must allow two at once); landed in L3, 2026-09-06 |
| G3 | THD readout on the RTA | Smaart Suite | **P4b** (needs the sweep/stepped-sine machinery; grouped with distortion) |
| G4 | STI / STIPA speech intelligibility from IR | Smaart Suite | **P4** (extends the IR-analysis list; IEC 60268-16 is paywalled — same acquisition problem as the Class-1 table, tracked in the same open-items list) |
| G5 | Cinema X-curve named target presets | Smaart | **P5** (data, not code) |
| G6 | Multi-plot workspace layouts, saved | Smaart | **P5** |
| G7 | SPL logging: history plots, alarms/traffic-light, PDF reports, remote web viewing | Smaart SPL | **P6** (persistence + report + web surface over P1's meters) |
| G8 | Noise dose / exposure (IEC 61252, OSHA/NIOSH presets) | Smaart SPL | **P6** (an integrator over P1's Leq engine) |
| G9 | Bode-style paired magnitude+phase plot as a named layout | Little RiTA | **P5** |
| G10 | FIR / filter export (.txt/.csv/.wav) for third-party DSP | RiTA | **P7** |
| G11 | Virtual processor: predict multi-source summation (delay, polarity, PEQ) from captured measurements BEFORE playing a test signal | RiTA | **P7** — the flagship of the parity work; see the non-goal amendment below |
| G12 | Reference-free TF (SyncSource-style) | Smaart v9 | **P8** (research-heavy: needs its own station-1 pass before any promise about method) |
| G13 | AES-75 measurement procedure support | Smaart RT | **P8** (standard acquisition + procedure automation) |

## Round 2 — market sweep beyond the floor (2026-08-28)

Products surveyed: SysTune (deep), SATlive, ARTA/STEPS/LIMP, WaveCapture
Live-Capture Pro, Meyer SIM3/Galaxy, L-Acoustics M1, CrossLite+
(F.MonteiroScience), Studio Six AudioTools, HOLMImpulse, CLIO, Klippel dB-Lab,
REW Pro. URLs in the research transcript. Thirteen further gaps adopted:

| # | Gap | Seen at | Lands in |
|---|---|---|---|
| G14 | Spatial multi-mic averaging with per-mic weighting and SPL alignment | REW Pro, M1 | **P6** — **BUILT 2026-09-06 (L6b)**, `docs/dsp/2026-09-06-multichannel-l6b.md` |
| G15 | Coherence-weighted blending of multi-mic captures (down-weight a noisy position) | ~~REW Pro + SysTune SSA concept~~ **Smaart only** — corrected by L6b station 1: REW has no coherence weighting, and SysTune "SSA" is *Spectrally Selective Accumulation*, a temporal bad-data filter (a G20 precedent) | **P6** — **BUILT 2026-09-06 (L6b)** |
| G16 | Environment compensation: temperature/humidity → speed of sound, delay-drift warning | CrossLite+, AudioTools | **P2** (delay finder gains an optional environment input) |
| G17 | Sub/main alignment wizard — guided or one-click delay/polarity proposal | M1 Autoalign, SATlive Delay-Suggestion | **P7** (UI front end over G11's summation prediction) |
| G18 | Crossover design surface (LR/Butterworth/Bessel 12-48 dB/oct + FIR) with phase-alignment cursor | CrossLite+ | **P7** (UI mode over G10/G11 math, no new DSP) |
| G19 | Network-audio input at protocol level (Dante/AVB/Milan) | M1 (Milan via P1) | **P8** — own research pass; scoped to protocol input, never vendor-hardware coupling |
| G20 | Measurement sequencing with auto solo/mute and automatic bad-capture discard | M1 (only "Autosolo" and auto-grouping are attested; **no reachable M1 document defines a discard criterion** — L6b station 1) | **P6** — **PARTLY BUILT 2026-09-06 (L6b)**: sequencer + refusal on overload / gate-not-cleared; **auto solo/mute needs a generator output path that does not exist** — own record, see `HUMAN-QA-QUEUE` |
| G21 | One-click polarity checker (sign of first arrival) | AudioTools, SysTune delay module | **P4** (cheap IR-toolkit add) |
| G22 | Offline dual-FFT against a reference WAV with latency compensation | WaveCapture, REW | **P4** |
| G23 | Cepstrum and wavelet / cycle-wavelet views | WaveCapture, CLIO | **P5** |
| G24 | Minimum-phase / excess-phase decomposition | REW | **P4** — feeds G11's "EQ can fix this / EQ cannot" verdicts |
| G25 | Drag-adjustable IR gating for quasi-anechoic response — table stakes in every surveyed tool | ARTA, HOLMImpulse, REW, CLIO | **P4** (ships regardless of the MTW decision) |
| G26 | Open bidirectional DSP plug-in API + SDK | SysTune (30+ vendors) | **P8** — promoted from the earlier gesture; the SDK, not the 30 partnerships |

## Rejected in round 2, with reasons (so nobody rediscovers them)

- **Vendor-hardware coupling** (M1's P1-only Milan node, SIM3's Galaxy line
  switcher): business models that sell hardware through the analyzer. G19 stays
  protocol-level.
- **Lab/production-line QC** (CLIO 3-D balloons, Klippel rub-and-buzz/3DL,
  Thiele-Small/LCR): answers "is this box built right", not "is this system
  tuned right". Different product; REW/ARTA/CLIO already serve it.
- **Predictive room/driver simulation with no measurement in it** (REW's
  8-sub room simulator): outside the amended non-goal — G11's carve-out is for
  simulation over CAPTURED measurements only.
- **Klippel MTON multi-tone distortion**: needs near-anechoic conditions and
  calibrated excitation; G3's THD readout is the right-sized live version.
- **Tonal-balance match scoring**: found in NO surveyed product's official
  docs — not parity. Logged instead as a potential DIFFERENTIATOR candidate
  for the beyond-the-floor list.
## Non-goal amendment

The design doc's non-goal said "not an EQ processor... it measures and
displays". The owner's floor directive pulls RiTA's virtual processor into
scope, so the line moves: **simulating** processing on captured measurements
(delay, polarity, PEQ, summation) and **exporting** the result to external DSP
is IN. Applying processing to the live signal path remains OUT — this tool
still never sits in the signal chain.

## Flagged to the owner, not promised

- **10EaZy / certified Class 1-2 hardware integration** (Smaart SPL): requires
  a commercial partnership, not code. Recorded, not scheduled.
- Auto-EQ was listed here as beyond-floor on 2026-08-28 morning; the owner
  mandated it the same day — it now lives in the tuning-visuals design record
  (P7) together with auto-delay. Remaining beyond-the-floor candidates:
- **Seen at REW/SysTune** (
  stepped-sine THD sweeps at 96 pts/octave, room-mode tools, SysTune's TFC
  continuous windowing as an alternative to MTW, bidirectional DSP plug-in
  API): worth weighing once the floor is met — the spec's superiority table
  already claims "better", and these are where "better" would live.
