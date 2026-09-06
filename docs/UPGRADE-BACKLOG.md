# Upgrade backlog — amendments the owner deferred to a later version

*Each item here is a feature the owner has explicitly chosen NOT to build in the
current version, with the reason and the trigger for revisiting it. This is not
`HUMAN-QA-QUEUE.md` (open questions) and not a lane in the master plan (scoped
work): it is decided-but-deferred. When an item is picked up, it becomes a lane
or a record amendment and its line here is struck with the commit that does so.*

---

## From lane L6b (owner rulings 2026-09-06)

- [ ] **SysTune-style soft excursion down-weighting of a capture block.**
  Instead of a hard numeric "capture is bad below X% trusted bandwidth" refusal
  (which no source publishes and which would be an invented grid floor — memory
  `a-threshold-read-off-a-grid-is-that-grids-floor`), soft-*down-weight* a
  capture block that departs from the running average, the one documented soft
  rejection (AFMG SysTune Manual §5.3). Owner ruled 2026-09-06: **ship v1 with
  report-only** (the two hard refusals — overload, gate-not-cleared — stay), and
  **build this in the next version**. It changes how the spatial average is
  weighted, so it is a `core/` amendment with its own before/after measurement,
  not a UI toggle. Analysis: `docs/dsp/2026-09-06-multichannel-l6b.md` §8.
  Trigger to build: real measurement sessions showing noisy captures polluting
  the average.

- [ ] **Remote API: write access to routing.** Owner ruled 2026-09-06: **ship
  the remote API read-only first** (it exposes `SnapshotSource` — measurements
  and traces — and cannot change routing or config), and **add a write surface
  in a later version**. A remote write to routing during a live show is a
  near-irreversible action and needs authentication (see the bind decision
  below) and a deliberate design before it exists. Analysis:
  `docs/dsp/2026-09-06-multichannel-l6b.md` §10. Settled at the same time and
  NOT deferred: the API binds **localhost by default** (owner ruled 2026-09-06,
  REW model), with a LAN-bind-plus-password opt-in itself a later feature.
  Trigger to build: the remote-tablet operator workflow is actually scheduled.
