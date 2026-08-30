# A fixed defect comes back through the branch where the fix does not run

*Lane L4a, 2026-08-30. Found by a line-by-line comparison between two
implementations, not by any test — and it was present in BOTH of them.*

## What happened

A band-edge estimator read a 50–71 Hz subwoofer as **46.9 Hz – 18270 Hz**, wrong
by about 90 dB, because a fixed 50 ms window holds only 2.5 cycles at 50 Hz and
the truncation leakage swamped the spectrum. That reading placed a subwoofer
*inside* a gate built to refuse it.

The fix was to size the window from the low edge just measured — ten cycles of it
— and measure again. Verified: the same cell then read 51.5 – 72 Hz. Both the C++
and its NumPy transcription were fixed, and both were checked against each other.

Then a reviewer asked the question neither implementation had asked:

> **What happens when the re-measurement cannot run?**

The window needs 0.21 s. The capture length rule sizes the gap at 1.5 × RT60, so
a dry room at RT60 = 0.1 s leaves 0.15 s. Both implementations then fell through
to the first-pass reading — which is the original fiction, returned silently, on
the exact class of system the fix existed to protect. Measured:

```
capture 0.15 s -> (40.67, 18272)     <- the fiction, and the gate admits it
capture 0.21 s -> (40.67, 18272)
capture 0.30 s -> (51.55, 71.51)     <- correct
```

## The general shape

A fix usually adds a branch: *if the better path is available, take it.* The
`else` gets written as "keep what we had", because what we had is right there and
returning it feels safe. But what we had is the defect. The fallback quietly
restores it under conditions nobody enumerated, and no test fires because every
fixture was long enough.

**When you fix something by adding a better path, the other branch must refuse,
not degrade.** Refusing is visible; degrading is not. Here it became a distinct
refusal reason naming the real cause — the capture, not the loudspeaker.

## How to catch it

- After adding any conditional improvement, write down the condition and then
  ask what the code does when it is false. If the answer is "the old behaviour",
  ask whether the old behaviour was the bug.
- Fixtures hide this: they are built generous. The test that caught it truncates
  a *real* measurement to a length the project's own capture rule would produce.
- Neither of two independent implementations found it, because they shared the
  omission rather than differing on it. Cross-checking two builds catches drift
  between them; it does not catch a question both failed to ask. A reader coming
  fresh to the logic does.

See also [[a-scalar-gate-calibrated-on-a-filter-grid-always-breaks]] and
[[a-fixture-can-be-too-well-behaved-to-fail]].
