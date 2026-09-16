# A tolerance inherited from a plan is a property of that plan's fixture

*Learned in lane L7-ALIGN Wave 3a, 2026-09-16, three times in one session, at
the cost of one red acceptance row and two near-misses. The sibling of
`a-threshold-read-off-a-grid-is-that-grids-floor.md`, one layer down: that file
is about a THRESHOLD read off a survey; this one is about a TOLERANCE read off a
fixture.*

## The pattern

A decision record measures a tolerance on the fixture it happened to have. A
plan copies the number. A builder writes it into a new test with a different
fixture, and it is wrong — sometimes too loose, sometimes unreachable, and the
only way to tell which is to derive it.

Three instances, all in one lane:

| what | inherited number | what the new fixture could actually do |
|---|---|---|
| `20log10\|H\| == -attenuationDb` | 1e-12, from a Butterworth-SOS filterbank | **1.23e-12** on a -9 dB Q=4 peaking evaluated at its own corner |
| a dB-crossing interpolation | 1e-9 Hz, with constants 0.7 and 12.4 | **3.75e-6 Hz** — neither constant is representable in `float`, which is what the input array IS |
| a band fit's phase intercept | 1e-9 rad, flat | a residual delay `dtau` leaves exactly `2*pi*f_bar*dtau` there, so the bound is **a function of the delay tolerance** |

The first was red. The second and third passed on MSVC and would have been
luck on another libm.

## Why it happens

A tolerance is never a property of the formula. It is a property of the
formula **evaluated on particular numbers**:

- **Conditioning.** `1 + a1 z^-1 + a2 z^-2` is a sum of terms of magnitude ~2
  that has to produce `|D(w)|`. Near a section's own corner, or far below it,
  that cancels, and the relative accuracy of the result is `eps` times
  `(1+|a1|+|a2|)/|D|`. A filterbank fixture with no deep dip never sees it; a
  high-Q cut does.
- **Storage width.** If the input array is `float`, no assertion about it can
  be tighter than `float`'s rounding of its own constants. 0.7 and 12.4 are
  not representable; 0.75 and 12.25 are exact, and the same test then passes
  at 1e-12 instead of failing at 1e-9.
- **Coupled quantities.** Two residuals of one estimate are usually one
  statement. Asserting them as two independent constants is asserting the
  second one and hoping.

## What to do instead

**Compute the bound in the fixture, from the fixture.** All three of these
became one-line helpers:

```
conditioningBoundDb(sections, omega) = (20/ln10) * eps * sum_sections(
    (1+|a1|+|a2|)/|D(w)| + (|b0|+|b1|+|b2|)/|N(w)| )
interceptBound = 2*pi*meanFrequencyHz*dtau_max
```

A derived bound is usually **tighter** than the inherited constant, not looser
— the conditioning bound above is 130x tighter than 1e-12 at `omega = 3.14`.
So this is not a way to make a test pass. It is a stronger test that also
happens to be true.

**And pick binary-exact fixture constants.** If an assertion is tighter than
the storage width's rounding of the constants it is built from, the test is
measuring `float`, not the code.

## The question to ask

> **What in this fixture sets this number, and is it still true here?**

If the answer is "the plan said 1e-12", you do not have a tolerance, you have
someone else's measurement. Derive it, print the residual beside it, and say in
the commit message which way it moved and why.
