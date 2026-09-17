# A shortest round-trip decimal is not a fixed point of narrowing

*2026-09-17, lane L-API station 4 wave 1, Task F row F3.*

## What the plan asked for

> walk the parsed document; for every numeric leaf assert it is not NaN, not
> infinite, and that `static_cast<float>` of it round-trips to itself — i.e.
> the wire carries nothing a float32 consumer will silently widen or lose

Written to catch a real, measured defect in a shipping competitor: Open Sound
Meter widens float32 to double before printing (`server.cpp:386-390`,
`item.cpp:169-175`), so its wire carries seventeen digits of precision the
measurement never had.

## Why it fails against correct code

The assertion went red on six of the eight endpoints, on a serialiser that was
doing exactly what the record asked.

```
float32 25.118864f              -> exactly 25.118864059448242 as a double
shortest round-trip decimal     -> "25.118864"          (Task A emits this)
"25.118864" parsed as a double  -> 25.118864            (exactly)
(double)(float)25.118864        -> 25.118864059448242
equal?                             NO, they differ by 5.9e-8
```

`std::to_chars` with no precision argument emits **the shortest decimal that
reads back as the identical float**. That decimal is, by construction, *not*
the float's exact double value — it is the shortest string inside the float's
rounding interval. Reading it as a `double` therefore lands somewhere in that
interval that is almost never the interval's centre, and narrowing that double
back to float and widening again returns the centre. A fixed point of
float-narrowing is what the assertion demands and what the shortest-decimal
decision **refuses to emit**. The two are contradictory: satisfying F3 as
written would have meant printing `25.118864059448242`, which is precisely
OSM's mistake.

The row was wrong a second way. `effectiveAverages` and `frequencyHz` are
`double` on `Snapshot` (`Snapshot.h:55`, `:82`, `:101`), not float32 at all, so
even a correct float32 invariant must not be applied to them — and a document
cannot say what C++ type a number was born as.

## The invariant that is true and still has teeth

Narrow the parsed value to float32, re-emit the shortest decimal, and require
it to be **byte-identical to the token on the wire** — applied only to keys
whose source really is a `float`, listed by name in the test.

A widened double fails it immediately: `25.118864059448242` narrows to
`25.118864f`, re-emits as `"25.118864"`, and does not match the token. Proven
by mutation — widening float32 to double before printing turns that case red
and nothing else.

## The general shape

**An assertion written to catch a known defect can contradict a decision made
two tasks earlier, and the contradiction only shows up when both are built.**
The plan's Task A decided shortest-round-trip; the plan's Task F asked for
exact-double-equality; nobody reading either in isolation saw that they cannot
both hold. A round-trip property is always a property of a *specific pair* of
operations — here `to_chars`/`from_chars` at `float` — and re-stating it across
a different pair (`double` -> `float` -> `double`) is a different claim, usually
a false one.

When a plan's assertion goes red on code you believe is correct, the first
question is which of the two documents is wrong, not how to make the code
agree. `CLAUDE.md`: *if a measurement disagrees with this plan, the plan is
wrong and the orchestrator hears about it.*

## See also

- `memory/float32-fft-precision.md` — what these numbers are worth in the first
  place, and why a 1e-9 tolerance on an FFT-derived value fails against correct
  code. Same family of error: a tolerance or an identity transplanted across a
  precision boundary it was never measured at.
- `memory/a-shared-kernels-numeric-factor-is-not-transferable.md` — a numeric
  claim that held in one context and was carried into another without
  re-measuring.
