# A scalar gate calibrated on a filter grid is always beaten by a steeper filter

*Learned in lane L4a, 2026-08-30, at the cost of four gate variables and most of
a session. Portable well beyond polarity.*

## The pattern

You need a threshold that separates "this measurement is trustworthy" from "this
one is not". You survey a grid of synthetic systems, find the value that
separates them cleanly, and ship it with the honest label **observed, not
derived**.

Then someone widens the grid, and the threshold is wrong.

It happened three times in one lane, each time with the honest label attached:

| variable | threshold found | what broke it |
|---|---|---|
| arrival bandwidth in octaves | 2.5 oct | `butter(8)` measured 4.18 oct and still answered backwards |
| ambiguity margin | 0.537, then 0.5 | the wrong-answer floor fell 0.708 → 0.300 from order 2 to 16 |
| ambiguity margin, again | 0.30 | `cheby1` order 9 broke it; the reviewing session's grid had Chebyshev, ours did not |

Each threshold was the **floor of whatever grid the measurer happened to build**.
Three sessions each found a different one and each believed it, because each had
surveyed honestly and labelled it honestly. Honesty about provenance does not
make a number a boundary.

## The test to apply before adopting any threshold

> **What stops the surveyed quantity from moving?**

Not "what value did the survey produce". If the answer is "nothing in
particular, the grid just ran out", the number is provisional and will be
overturned by the next person with a steeper filter, a higher order, or a family
you did not think of.

Three kinds of answer are worth having, in descending order of strength:

1. **A mathematical bound.** Normalised cross-correlation ρ = |peak| / √(E₁E₂)
   is in [0, 1] by Cauchy–Schwarz. The variable cannot run away, even if the
   threshold inside that range still needs surveying.
2. **A statement in the vocabulary of the thing being measured.** "Filter slopes
   up to 48 dB/oct" is checkable by an operator against their own processor.
   "2.5 octaves" is not a fact about any device anyone owns.
3. **A physical proposition that could be false.** The gate that survived L4a is
   a pair of band edges, and behind it is: *the sign of a first arrival needs a
   low end to give the wavefront a direction and a high end to give it a front.*
   That can be attacked, and was, at its corner. A number read off a grid cannot
   be attacked, only re-measured.

## The related failure, since it comes from the same root

The first three variables were each measured on **one grid by one measurer**. The
fourth survived because three independent grids were built and compared. When a
threshold matters, the cheapest real defence is a second person building their
own grid without reading yours — not a wider grid built by the same hands.

See also [[a-fixture-can-be-too-well-behaved-to-fail]] for the mirror error: a
fixture so benign that the measurement could not have failed.
