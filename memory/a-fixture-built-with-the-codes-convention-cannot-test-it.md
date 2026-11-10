---
name: a-fixture-built-with-the-codes-convention-cannot-test-it
description: H12 bext prior sign was inverted in BOTH the code and the fixture that wrote the test files; the 14 s "found only through the prior" case passed against physics-reversed files
metadata:
  type: feedback
---

H12 (two-file WAV) took the bext TimeReference prior as `T_meas - T_ref`. The
mapping's offset is the measurement index of reference sample 0, so the correct
prior is `T_ref - T_meas`.

The JUCE test that was meant to prove the prior is wired wrote the measurement
file's TimeReference with the same inverted sign. The fixture's own clock model
gave the measurement pre-roll (it started EARLIER), and the test still wrote it
a LATER TimeReference. Code and fixture agreed with each other, and disagreed
with EBU 3285.

Below ~5 s the ±10 s search window still straddled the truth, so only the 14 s
case could have caught it. That case was built the same wrong way.

The Fable phase review found it by deriving the sign from the standard's
definition of the field, not from the code.

**Why:** a test whose expected value is produced with the implementation's own
convention is a regression lock on that convention, not a correctness test
(CLAUDE.md verification standard). This is the same family as
[[a-convention-cannot-be-blamed-for-a-value-it-maps-to-itself]].

**How to apply:**

- For any sign or offset convention that crosses a file format or standard
  (bext, timecode, delay sign), write the fixture from the STANDARD's sentence:
  "file B started 14 s later -> B's TimeReference is larger by 14 s".
- Then derive what the code must output.
- Test BOTH signs (started earlier and started later), and use a magnitude
  larger than any search window that could hide the error.
