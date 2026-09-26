# An index from one table is not valid in another

**2026-09-25/26, lane L6a (SPL-pro), Wave 3/W2-E2b (calibration).** Found by
an independent verifier; fixed in `50e53a8`.

`MainComponent::kCalibrationRouteIndex = 0` names a **route position** — a
slot in the routing plan, chosen once as "route 0's measurement channel" in
the task brief. Three channel-indexed calls in the calibration pipeline
(`AnalysisThread::armLocateCapture`, and two calls that read or wrote
per-channel calibration state) used `kCalibrationRouteIndex` directly as a
**channel number**. A route position and a channel number are both plain
`int`s, both usually small, and the routing plan's default assignment (route 0
→ channel 0) makes them numerically identical in the configuration every
manual test runs under.

That is exactly what let it ship unnoticed: every session in this lane
calibrated with the default routing, so route 0 and channel 0 were the same
number every time, and no test disagreed with itself. One operator click —
reassigning the Measurement role to a different channel, which is an ordinary
thing to do before a show — makes the route index and the channel number
diverge, and the calibration verdict is silently computed for (or never
reaches) the wrong channel. Nothing crashes and nothing goes red; the drift
check just answers about a channel the operator was not calibrating.

## Why it is easy to write and easy to miss

The two spaces share a type (`int`), a small numeric range, and — in the
default configuration every developer runs first — the same values. A
reviewer reading `kCalibrationRouteIndex` used where a channel index is
expected sees a variable with a plausible name doing a plausible-looking
thing, because nothing about the call site marks which space the argument is
supposed to be in. The bug is invisible until a test deliberately uses a
non-identity mapping between the two spaces.

## The fix, and the shape to copy

The route index is resolved to a channel number **exactly once**, at the point
where a capture actually completes, via `rta::measure::
calibrationMeasurementChannel(plan, kCalibrationRouteIndex)` — against the
*routing plan the capture that just completed actually used*, not re-derived
later from a possibly-stale plan. The resolved channel is then the only value
carried forward into every channel-indexed call. `-1` (no channel resolved, or
the completed capture's route had none — the calibrator-only, no-reference-
channel case) is treated the same way every other channel-indexed call in
`AnalysisThread` already treats it: a safe no-op, not a crash and not a
silent wrong-channel write.

## The check this costs

**When an integer crosses from one indexing space into another, convert it
through the table that defines the mapping, name the variable after its own
space, and test with a mapping that is not the identity.** A variable named
`...RouteIndex` should never be passed where a parameter expects a channel
number without going through the function that performs that specific
conversion — and a fixture where route N and channel N are always equal can
never catch this, no matter how many such fixtures exist, because the defect
lives entirely in the space *between* two numbers a same-identity fixture
never lets diverge.

## Related

- `memory/a-cap-checked-on-the-drain-path-is-unchecked-on-the-publish-path.md`
  — another case where a fixture's default configuration hid a distinction
  that only shows up off the default.
- `memory/a-config-field-with-one-value-in-every-fixture.md` — the general
  form of "the fixture never varies the thing that would have caught this."
