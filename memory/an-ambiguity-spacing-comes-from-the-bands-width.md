# An ambiguity spacing comes from the band's WIDTH, never from its mean

*Learned in lane L7-ALIGN Wave 3a, 2026-09-16. The decision record and the
implementation plan both carried the same wrong sentence, and the two candidate
explanations were 8.26 ms and 10.0 ms apart — close enough that the fixture
which would have settled it was not the obvious one.*

## The claim that was wrong

Record §4 and plan C5 both say a complex band fit's competing delays sit at
`tau* + n/f_bar` — one over the band's MEAN frequency. For a 40–160 Hz window
that is 10.0 ms.

They do not. The fit maximises

    |S(tau)| = | sum_k w_k R_k e^{-j 2 pi f_k tau} |

Write `f_k = f_bar + delta_k`. The mean factors out as `e^{-j 2 pi f_bar tau}`,
a pure rotation of modulus 1, which `|.|` discards. **`|S(tau)|` is a function
of the spread `delta_k` alone.** `f_bar` cannot appear in the answer.

What is left is the Fourier transform of the weight function, so for a flat
N-bin window the envelope is the Dirichlet kernel and its local maxima sit near
`(m + 1/2)/(N*delta)` — 8.26 ms spacing for 121 bins of 1 Hz.

## Why it was hard to catch

8.26 ms and 10.0 ms differ by 21%. On the record's own fixture a test asserting
either one, with any honest tolerance, would pass or fail for reasons that look
like fixture noise. **Two explanations that agree to 21% on one fixture are not
distinguished by a tighter tolerance on that fixture.**

## The experiment that settles it

**Move the thing one explanation depends on and hold the thing the other
depends on.** Two windows of the same WIDTH at different CENTRES:

```
40-160 Hz  : 121 bins of 1 Hz, f_bar = 100 Hz, 1/f_bar = 10.00 ms
240-360 Hz : 121 bins of 1 Hz, f_bar = 300 Hz, 1/f_bar =  3.33 ms

first competitor offset, measured:  0.0118209 s  and  0.0118209 s
```

Identical to seven figures while `1/f_bar` changes by a factor of three. No
tolerance argument is needed and the result cannot be read two ways.

## Where the wrong sentence came from

The `1/f_c` ambiguity is real — for a **time-domain correlation of a narrowband
signal**, where a whole cycle of the carrier can slip unnoticed. The record
imported that intuition into a **complex band fit over a contiguous band**,
which is a different estimator with a different failure. Both are "the delay is
ambiguous"; they are ambiguous by different amounts, for different reasons.

## The portable form

> When a derivation names a scale, check whether the estimator can even see the
> quantity that scale is built from. A magnitude discards a global phase, so
> anything that enters only as a global phase is not in the answer.

And when two candidate scales are numerically close on the fixture you have,
**build the fixture that separates them** rather than sharpening the tolerance
on the one that does not.
