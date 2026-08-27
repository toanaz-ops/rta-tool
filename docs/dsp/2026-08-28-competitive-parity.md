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
| G2 | MTW and fixed-FFT engines running CONCURRENTLY on one TF | Smaart v9 | **P3** (scope amendment: the MTW engine sits beside, not instead of, the fixed engine — the P2 interface already anticipated swappable engines; now it must allow two at once) |
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
- **Beyond-the-floor candidates seen at REW/SysTune** (auto-EQ generation,
  stepped-sine THD sweeps at 96 pts/octave, room-mode tools, SysTune's TFC
  continuous windowing as an alternative to MTW, bidirectional DSP plug-in
  API): worth weighing once the floor is met — the spec's superiority table
  already claims "better", and these are where "better" would live.
