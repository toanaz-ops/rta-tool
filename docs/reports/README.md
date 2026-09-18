# Reports — one per delivered part

Each part of the build gets a numbered report here when it lands. A report is
written by the session that did the work, at the time of the work — not
reconstructed later from git, which holds the what but never the why.

## The pipeline (set by the owner, 2026-08-27)

Every feature or algorithm moves through five stations:

```
1. RESEARCH   sub-agent (Explore type, no write tools) reads papers, standards,
              competing products, open-source repos. Returns findings + where
              sources disagree.
2. ANALYSIS   Fable weighs the findings, argues against the obvious option,
              writes the decision into docs/dsp/ or the relevant spec.
3. IMPL PLAN  Opus sub-agent turns the decision into an implementation plan:
              exact files, exact tests, exact acceptance numbers.
4. BUILD      Sonnet sub-agents implement to that plan. TDD: the test exists
              and fails before the code exists.
5. REVIEW     a verifier agent (no write tools) tries to refute the claim that
              the work is done. Findings go back to station 4.
```

A report documents: what was built, what the research changed about the plan,
what the verifier refuted, and the exact commands + output that prove the state.

## Index

| # | Part | Status |
|---|---|---|
| 001 | Phase 0 foundations + FFT/banding core (pre-pipeline) | see docs/features/FEAT-phase0-foundations.md |
| 002 | Phase 1: the audio chain end to end | [002-phase1-audio-chain.md](002-phase1-audio-chain.md) |
| 003 | Lane L2: the dual-FFT transfer-function engine | [003-dual-fft-engine.md](003-dual-fft-engine.md) |
| 004 | Lane L5c: the display layer — Bode layout, workspaces, the app's first call into the dual-FFT engine | [004-display-layer-l5c.md](004-display-layer-l5c.md) |
| 005 | Lane L3: the multi-time-window engine | [005-mtw-engine.md](005-mtw-engine.md) |
| 006 | Lane L6b: multichannel workflows — spatial average, coherence weighting, routing, sequencing, presets | [006-multichannel.md](006-multichannel.md) |
| 007 | Lane L7: solvers — output path, FIR export, auto-delay, auto-EQ, the alignment wizard and the crossover surface | [007-solvers.md](007-solvers.md) |
| 008 | Lane L-API: the read-only remote API — HTTP/JSON on loopback, eight endpoints, the `Host` allowlist, and the transport L6a's SPL web viewer rides | [008-remote-api.md](008-remote-api.md) |
