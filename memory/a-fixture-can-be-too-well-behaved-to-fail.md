# A fixture can be too well-behaved to fail

Lane L4a, 2026-08-30. Record decision 9 says a capture must outlast the sweep by
1.5 × RT60, and the C++ test written to prove it went green reading **0.0026 dB**
of error from truncating at *half* the RT60 — a figure that says truncation is
harmless, which is the opposite of the rule being tested.

The code was right. The fixture could not detect the thing it existed to detect,
and it took three separate fixes to make it able to fail:

1. **The assertion was a tautology.** It checked that a truncated capture
   *differs* in the time domain. Every working convolution satisfies that; no
   wrong implementation makes it red.
2. **The synthetic room was sparse** — one tap every 97 samples, chosen so a
   direct convolution stayed cheap. Its reverberant tail therefore carried about
   a ninety-seventh of the energy it should have, so truncation removed almost
   nothing. (Density was free all along: deconvolution *is* convolution, so the
   existing FFT path convolves the excitation with the room in O(N log N).)
3. **The scan sampled 31 round frequencies, 250 Hz apart.** A reverberant tail
   puts spectral nulls a couple of hertz apart. The scan walked past every bin
   where truncation does its damage and reported the reassuring 0.005 dB.
   Scanning *every* bin in the band moved it to **1.76 dB**, against 0.0023 dB
   at 1.5 × RT60 — a factor of 750, and the rule finally had a test.

## The rule

**For every fixture, name the wrong implementation it would catch.** Not the
right one it agrees with. If no answer comes, the fixture is a demonstration,
not a test — and a green demonstration reads exactly like a green test in the
suite output.

Two specific traps this produced, both worth checking by name:

- **A worst-bin claim sampled on a coarse grid is not a worst-bin claim.** If
  the quantity has structure finer than the sample spacing, the sampling *is*
  the answer. Scan the bins, or say in the comment why the grid resolves what
  matters.
- **Numerical stability of a fixture can mean insensitivity of the test.** A
  reviewing session advised keeping the room's direct arrival dominant "so the
  spectrum has no deep nulls and the worst-bin figure stays numerically
  stable". That advice is what removed the signal: a smooth spectrum is a
  spectrum on which truncation leaves no mark, and the deep nulls of a dense
  reverberant tail *are* the evidence. Optimising a fixture for well-behaved
  numbers optimises it toward measuring nothing.

## Why this is not an argument for fragile tests

The point is not that fixtures should be numerically nasty. It is that
"well-behaved" and "sensitive" are different properties, and a fixture chosen
for the first without checking the second will pass regardless of the code.
Prove sensitivity the cheap way: run the assertion against a deliberately wrong
configuration and watch it go red. In this lane that was done twice — a five
sample lead-in instead of the derived one (0.0073 dB → 1.10 dB), and a gap of
half the RT60 instead of one and a half (0.0023 dB → 1.76 dB). Both were reached
through the public API, so neither needed the source edited to demonstrate it.

## Related

- `docs/HANDOFF.md`'s "green but proving nothing" table collects seven earlier
  instances from lane L5c. This is the same failure, found in fixture design
  rather than in a guard or a build system, and the question that unifies all of
  them is still: *what wrong implementation would make this red?*
- `docs/dsp/2026-08-30-sweep-ir-l4a.md` decision 9 carries the capture-length
  rule itself and the measured table behind it.
