---
name: a-convention-cannot-be-blamed-for-a-value-it-maps-to-itself
description: before hunting a sign convention for a sign discrepancy, check the convention can even reach the observed value — conjugation negates a phase, and negation fixes 0 and 180 degrees
metadata:
  type: reference
---

An open question sat in `docs/HUMAN-QA-QUEUE.md` for nine days: a closed-form
identity said a Butterworth-4 crossover's two halves are **in phase**, lane L4a
had **measured** the opposite sign there, and the obvious suspect was one of the
four sign conventions in [[dual-fft-conventions]]. The brief that opened the
probe said so out loud: "this contradiction smells like exactly one of them."

It was not one of them, and **five minutes of arithmetic could have said so
before any grid was built.** The only thing flipping `Sxy = conj(X)·Y` does to a
transfer function is conjugate it, which **negates** the phase. The disputed
quantity was a frequency-independent offset of `N·90°`. Negation is the identity
on `0°` and on `180°` modulo 360. So:

> **No convention flip in this engine can change an EVEN-order crossover's
> reading. Only ±90° — the odd orders — move at all.**

The measurement was at order 4. The suspect was incapable of the crime.

## The move, stated so it transfers

Before you go looking for a convention, a sign, or a flipped `conj`, ask: **what
set of values can that flip actually produce, and is the observed value in it?**
A transformation with fixed points cannot be blamed for a discrepancy sitting on
one of them. This costs one line of algebra and it either eliminates a whole
class of causes or promotes it to the prime suspect — either outcome is worth
more than the first grid cell you would otherwise have built.

The same shape appears wherever a suspected cause is an *involution* or has a
small orbit: a polarity flip cannot explain a magnitude error; a `±1` sample
delay cannot explain a frequency-independent offset; a `conj` cannot explain a
change in `|H|`. Rule it in or out by its reachable set first.

## Where the sign actually came from, for the record

A fixture artefact. L4a applied a **cross-correlation peak-sign** rule across two
systems with *different passbands* (a band-pass sub against a band-pass main,
not a matched-cutoff complementary pair). The identity constrains the
correlation **at lag 0** and nothing else; with the two passbands mismatched the
offset stopped being constant (62.9° of spread across the overlap instead of 0°),
the correlation envelope stretched, and an oppositely-signed neighbouring lobe
outgrew lag 0 by 1.45×. The reported sign was that lobe's. **The value at lag 0
stayed positive the whole time, exactly as the identity says.**

Second lesson, smaller but with teeth: **a rule that reads an argmax is not
reading the quantity your identity constrains.** `argmax |r(τ)|` and `r(0)` agree
only while the envelope is peaked enough to keep the winner at the origin, and
nothing in the rule tells you when it stopped being.

Probe and data: `tools/probe_align_order4.py`,
`docs/research/2026-09-15-l7-align-order4-probe.md`. CI lock:
`core/tests/test_align_order4_identity.cpp`, whose last case asserts the
reachable-set argument itself — `reachable == (n % 2 == 1)` — so the reasoning is
in the build, not only in a document.
