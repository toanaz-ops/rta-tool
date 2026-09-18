---
name: a-bitwise-check-must-copy-the-expression-not-the-arithmetic
description: floating-point contraction is a property of expression SHAPE, so a bitwise test that recomputes an implementation's mathematics in one expression where the implementation used two statements fails on clang and passes on GCC and MSVC
metadata:
  type: reference
---

L6a Wave 1 shipped a bitwise assertion that recomputed what the library computes:

```cpp
// core/src/meter/Leq.cpp -- the implementation, TWO statements
const double db = 10.0 * std::log10(meanSq);
return db + referenceOffsetDb_;

// core/tests/test_window_energy.cpp -- the test, ONE expression
const double fromEnergy =
    10.0 * std::log10(leq.sumSquares() / count) + s.offset;
CHECK(leq.leqDb() == fromEnergy);
```

Same mathematics. Green on MSVC for days. The first three-OS CI run this wave ever
got said:

```
ubuntu-latest  (GCC)    72/818 B1 ... Passed
windows-latest (MSVC)   72/818 B1 ... Passed
macos-latest   (clang)  72/818 B1 ... ***Failed   test_window_energy.cpp:113
```

**Apple clang defaults to `-ffp-contract=on`, and on arm64 FMA is in the baseline
ISA.** Within a single expression it may contract `a * b + c` into `fma(a, b, c)` —
one rounding instead of two. Across statement boundaries it may not, because the
named intermediate is an observable value. So the implementation got two roundings
and the test got one, the results differed in the last bit, and a bitwise
comparison is exactly the kind of check that notices.

## Read this beside [[a-bitwise-identity-can-belong-to-the-isa-not-the-arithmetic]]

The `ci/macos-fixes` lane (PR #22, `20f3c65`) hit the same contraction on the same
day, in `app/src/measure/Levels.h`, and wrote the other half of the lesson. **Read
both; they are complementary, not competing.**

- **That file** is about the *claim*: `levelDbFs(0.5) == 0.0` was never an identity,
  it was a coincidence of two roundings, so the bitwise assertion was retired for a
  derived `1e-15` bound. Its rule: an exactness that exists only because a product
  was rounded before an add is not exactness.
- **This file** is about the *fixture*: where the invariant IS real, the test has to
  reproduce the implementation's expression shape to see it.

**And PR #22 set `-ffp-contract=off` repo-wide** (`CMakeLists.txt:91`), so
contraction no longer happens on any of the three platforms. That does not retire
either lesson, and it is important to understand why: the flag makes the
*one-expression* form pass again, and it still must not be written that way,
because — in that file's words — *an assertion whose truth is a compiler flag
records the flag, not the arithmetic.* The structural form below is correct with the
flag, without it, and on a toolchain that has not been thought about yet. Treat the
flag as a belt beside the braces, never as the reason.

## The rule

**A bitwise assertion against an implementation must copy the implementation's
EXPRESSION STRUCTURE, not merely its arithmetic.** Every product or transcendental
whose result the implementation stores in a named variable before using it must be
stored in a named variable in the test too:

```cpp
const double meanSquare = leq.sumSquares() / static_cast<double>(leq.sampleCount());
const double db = 10.0 * std::log10(meanSquare);   // named, as the .cpp does
const double fromEnergy = db + s.offset;
CHECK(leq.leqDb() == fromEnergy);
```

The same applies to an accumulation loop. `sum += x * x` is one expression and a
contraction candidate; the implementation's `const double sq = x * x; sum += sq;`
is not. Two loops that are algebraically identical are not bitwise identical.

## How to spot the hazard before CI does

Look for a `CHECK(a == b)` where `b` is recomputed in the test, and then look for a
**multiply-then-add** or **divide-then-add** anywhere in that recomputation. That
shape, in one expression, is the whole risk. `a * b` alone, `a / b` alone, and
`a + b` alone cannot contract; it takes a product feeding a sum.

Note which of this wave's other bitwise checks were safe, and why — the pattern is
worth internalising rather than re-deriving:

| check | shape | safe because |
|---|---|---|
| `sumSquares() == independent` | sum of named products | named intermediate, no mul-add in one expression |
| B2's 3600-step window equality | `10*log10(s/n)` vs same, offset **0.0** | `fma(10, log10, 0.0)` rounds once and equals the plain product |
| `percent() == 100.0` | mul then div, compared to a literal | no addition to contract into |
| `exchangeDenominator(3) == 3.0/log10(2)` | division only | no product |
| `10^(Q/q) == 2.0` | `pow` of a quotient | no product |
| A6's merge equality | integer counts | no floating-point arithmetic at all |

One of them, B2, is safe for a reason worth spelling out because it looks unsafe:
its comparison is `10*log10(s/n)` against `10*log10(s/n) + referenceOffsetDb` where
the offset is `0.0`. Even fully contracted, `fma(10, log10(x), 0.0)` rounds the
product exactly once and equals the plain product, so the two agree bitwise either
way. Change that fixture's offset to a non-zero value and it acquires this bug.

## What NOT to do about it

**Do not widen the tolerance.** The property was real and it holds on all three
toolchains once the test stops asking a differently-shaped question; a tolerance
would have discarded a true bitwise invariant to paper over a fixture defect. This
is the same discipline as
[[a-threshold-read-off-a-grid-is-that-grids-floor]]: fix the measurement, not the
bound.

And do not conclude "clang is wrong". Contraction is standard-permitted and
`FP_CONTRACT` is on by default in C++ for a reason. The test was wrong.

## The wider point about single-toolchain verification

This defect sat green through a full build, an adversarial verification round that
ran seven mutations, and a merge — because the only machine available was MSVC. It
was found within minutes of CI coming back. A bitwise claim measured on one
toolchain is a claim about one toolchain, and this project's own selling point is
three-OS provability; see `docs/GIT-WORKFLOW.md` rule 3 on why a green matrix is
the merge gate and not a formality.
