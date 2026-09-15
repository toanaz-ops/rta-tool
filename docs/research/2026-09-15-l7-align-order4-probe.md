# The order-4 sign contradiction, settled by an independent grid cell (L7-ALIGN §13.1)

*2026-09-15. Probe session, working from `main` at `23b7ea0` in an isolated
worktree. No `core/` or `app/` code was changed. Scripts:
`tools/probe_align_order4.py`. Test added: `core/tests/test_align_order4_identity.cpp`.*

---

## 0. The question, and what this settles

`docs/HUMAN-QA-QUEUE.md` carried one open item blocking Wave 3 of lane L7:

> **Order-4 mâu thuẫn (ALIGN §13.1)** — identity `N·90°` dự đoán ĐÚNG dấu ở BW4,
> L4a ĐO sai dấu ở bậc 2 VÀ 4. Cần trí nhớ chủ nhân về fixture L4a hoặc một ô
> grid độc lập.

The closed form in `docs/dsp/2026-09-06-l7-alignment-wizard.md` §3 says a
Butterworth-N high-pass leads its matched low-pass by `N·90°` at **every**
frequency, so at N = 4 the two are in phase and a correctly wired pair reads
the **correct** sign. `docs/dsp/2026-08-30-sweep-ir-l4a.md` ("What G21 promises
an operator about a subwoofer", amendment 2) records the opposite:

> a correctly wired sub/main pair reads the opposite sign at crossover orders
> 2 and 4, and the sign flips again at orders 1 and 8.

Orders 1, 2 and 8 are consistent with the identity. Order 4 is not. The handoff
asked for either the owner's memory of the L4a fixture, or an independent grid
cell. **This is the independent grid cell, and the owner does not need to be
asked.**

**Ruling.** The `N·90°` identity holds — exactly, analog and digital,
Butterworth and Linkwitz-Riley, at every order 1–8 and every frequency — and it
holds under this repo's own dual-FFT convention with no sign to get wrong. The
L4a order-4 reading is not a defect in the identity and **cannot** be a sign
convention: conjugating the transfer function negates the offset, and −0° = 0°
and −180° = 180° modulo 360, so no convention flip in this engine can move an
**even**-order crossover's reading from right to wrong. What reproduces the L4a
report exactly — right at 1, wrong at 2, wrong at 4, right at 8 — is a pair of
**band-pass boxes** correlated against each other rather than a matched-cutoff
complementary crossover pair. That is the only box model this repo has ever
committed (`tools/probe_polarity_bandwidth.py:132-152`, `band_sos`), and §3 of
the ALIGN record named it as one of the two candidates before this probe ran.

**And the rule matters as much as the fixture.** What reproduces L4a is
decision 6b's **un-whitened** `ρ = |peak| / √(E₁E₂)` peak sign
(`docs/dsp/2026-08-30-sweep-ir-l4a.md:1195`) — an estimator this repo does not
ship; `relativePolarity()` exists in no file. The correlator it *does* ship,
`findDelayPhat`, is **PHAT-whitened** and on the identical pair reads the mirror
image: right at order 4, wrong at order 8 (§4). Two correlators, one unchanged
pair of loudspeakers, opposite polarity verdicts at the two orders this question
turns on. **The wizard must keep §3's table unchanged and must not read a
topology sign off any correlation peak — PHAT or plain.**

---

## 1. Method, and why the L4a finding could not simply be re-read

The L4a sub/main cross-correlation fixture **is not in the repo**. Searched:
`tools/probe_polarity_bandwidth.py`, `probe_polarity_margin.py`,
`probe_polarity_edges.py`, `verify_l4a_measurement.py`, `l4a_sweep.py`, and
`git log --diff-filter=D -- 'tools/*'` (empty — nothing was deleted). Every
committed polarity probe measures the **absolute** first-arrival sign of a
**single** band-pass box; none of them correlates two systems against each
other.

**Which rule L4a was applying, stated before anything is attributed to it.**
L4a decision 6b names it at `docs/dsp/2026-08-30-sweep-ir-l4a.md:1195`: a
**normalised, un-whitened** cross-correlation `ρ = |peak| / √(E₁E₂)`, bounded in
[0, 1] by Cauchy–Schwarz, with the sign read at that peak. **That estimator is
not built.** `relativePolarity()` does not exist anywhere in `core/` or `app/`
(grepped); ALIGN §8 is the plan that would build it, and until its two-grid pass
is done it is to return ρ and the sign and no verdict. The only correlator this
repo ships is `findDelayPhat` — `DelayFinder.cpp:34`, `result.inverted =
peak.height < 0.0` — and its peak comes off a **PHAT-whitened** cross-spectrum
(`DelayFinder.cpp:9`, "whiten-and-weight"), which is a different estimator with
a different peak. §4 below measures both, and they disagree at exactly the
orders this question turns on. Its only test negates the *same* pink-noise
signal (`core/tests/test_delay_finder.cpp:50-58`), so nothing in CI would have
noticed.

So the finding could not be re-read from a committed artefact, and the question
had to be rebuilt from scratch. `tools/probe_align_order4.py` does that in five
sections: the identity itself (analog and digital), the summed magnitude, the
L4a peak-sign rule across five filter **geometries**, the repo's own transfer
convention, and a forensic block on the geometry that reproduces.

Numbers below are pasted from the script. Environment: numpy 2.5.2, scipy
1.18.1, Python 3.14.6, `fs` 48000 Hz, the main checkout's `.venv`.

```
.venv/Scripts/python.exe tools/probe_align_order4.py --section all
```

---

## 2. The identity is exact — analog and digital, every order

Analog: normalised Butterworth zeros/poles/gain evaluated directly on `s = jω`
at 101 points over 0.01–100 rad/s. No bilinear transform, no filter design
choices.

```
     topology   N  predicted   measured      max dev
           BW   1     90.00d     90.00d    0.00e+00d
           BW   2    180.00d    180.00d    0.00e+00d
           BW   3    -90.00d    -90.00d    0.00e+00d
           BW   4     -0.00d      0.00d    0.00e+00d
           BW   5     90.00d     90.00d    0.00e+00d
           BW   6    180.00d   -180.00d    0.00e+00d
           BW   7    -90.00d    -90.00d    0.00e+00d
           BW   8     -0.00d      0.00d    0.00e+00d
           LR   2    180.00d    180.00d    0.00e+00d
           LR   4     -0.00d     -0.00d    0.00e+00d
           LR   8     -0.00d      0.00d    0.00e+00d

  worst deviation anywhere in the analog grid: 0.000e+00 degrees
```

Digital, second-order sections at 48 kHz, 200 points 20 Hz – 20 kHz. This had
to be checked separately: the bilinear transform is not *obliged* to preserve a
ratio of two filters. It does here, because a matched-cutoff pair shares its
denominator exactly and the numerators are `(1+z⁻¹)^N` and `(1−z⁻¹)^N`, whose
ratio on the unit circle is `(j·tan(ω/2))^N` — still `N·90°` for every
`0 < ω < π`.

```
   crossover fc = 100 Hz
       topology   N  predicted   measured      max dev
             BW   1     90.00d     90.00d    1.96e-12d
             BW   2    180.00d   -180.00d    4.09e-12d
             BW   3    -90.00d    -90.00d    3.64e-12d
             BW   4     -0.00d      0.00d    4.89e-12d
             BW   5     90.00d     90.00d    6.82e-12d
             BW   6    180.00d   -180.00d    8.19e-12d
             BW   7    -90.00d    -90.00d    8.98e-12d
             BW   8     -0.00d      0.00d    1.03e-11d
             LR   2    180.00d    180.00d    3.87e-12d
             LR   4     -0.00d      0.00d    8.19e-12d
             LR   8     -0.00d      0.00d    9.78e-12d
   crossover fc = 1000 Hz
       topology   N  predicted   measured      max dev
             BW   1     90.00d     90.00d    2.22e-12d
             BW   2    180.00d   -180.00d    4.01e-12d
             BW   3    -90.00d    -90.00d    3.41e-12d
             BW   4     -0.00d      0.00d    5.80e-12d
             BW   5     90.00d     90.00d    7.39e-12d
             BW   6    180.00d   -180.00d    9.66e-12d
             BW   7    -90.00d    -90.00d    1.02e-11d
             BW   8     -0.00d      0.00d    1.05e-11d
             LR   2    180.00d   -180.00d    4.43e-12d
             LR   4     -0.00d      0.00d    8.07e-12d
             LR   8     -0.00d      0.00d    1.16e-11d
```

**The `1e-11` figure is not a tolerance argued from a grid** — it is the
double-precision floor of `atan2` on a ratio of two products of N second-order
sections, and it grows with N exactly as accumulating rounding does.
§3 of the ALIGN record stands unchanged.

---

## 3. The BW2 sum: ALIGN §1 correction 1 is right, research D1/D6 are backwards

`docs/research/2026-09-06-l7-alignment-wizard-station1-research.md` D1 (line 87)
and D6 (line 33) both say the **un-inverted** BW2 sum peaks +3 dB. ALIGN §1
correction 1 says that is backwards. It is.

```
   crossover fc = 100 Hz, 48000 Hz, second-order sections
     topology   N    HP-LP    sum as-is    sum flipped
           BW   1    90.0d        0.00dB          0.00dB
           BW   2   180.0d     -261.18dB          3.01dB
           BW   3   -90.0d        0.00dB          0.00dB
           BW   4    -0.0d        3.01dB       -248.01dB
           BW   5    90.0d        0.00dB          0.00dB
           BW   6   180.0d     -243.98dB          3.01dB
           BW   7   -90.0d        0.00dB          0.00dB
           BW   8    -0.0d        3.01dB       -242.56dB
           LR   2   180.0d     -284.75dB          0.00dB
           LR   4    -0.0d       -0.00dB       -258.16dB
           LR   8    -0.0d        0.00dB       -245.00dB
```

The `-2xx dB` readings are the double-precision floor of an exact cancellation,
not a small residual. Every row of the ALIGN §3 table reads back verbatim:
BW2 null/+3.01, BW4 +3.01, LR2 null un-inverted and flat inverted, LR4/LR8 flat
as-is, odd orders all-pass in **both** polarities (which is why an odd-order
crossover's polarity cannot be read off a magnitude sum at all).

---

## 4. The L4a rule, across five geometries — and the one that reproduces

The L4a reading is the sign of the **un-whitened** cross-correlation peak
between two impulse responses, both driven with the same sign — decision 6b's
`ρ = |peak| / √(E₁E₂)` (`docs/dsp/2026-08-30-sweep-ir-l4a.md:1195`). A negative
peak is the "wrong sign". Five geometries, orders 1/2/3/4/6/8, 1 s impulse
responses at 48 kHz.

The `PHAT sign` column is the **shipped** `findDelayPhat` rule
(`DelayFinder.cpp:34`) run on the same pair, and it is printed because the two
estimators must not be confused for each other. Read it: it is not a footnote,
it is half the finding.

```
   G1 matched crossover 100 Hz
       N  peak sign     lag     rho  r[0] sign  PHAT sign   verdict
       1         -1       1  0.0804         -1         -1   WRONG
       2         -1       0  0.0676         -1         -1   WRONG
       3          1      74  0.0273          1         -1   RIGHT
       4          1       0  0.0271          1          1   RIGHT
       6         -1       0  0.0174         -1         -1   WRONG
       8          1       0  0.0129          1          1   RIGHT

   G2 matched crossover 1 kHz
       N  peak sign     lag     rho  r[0] sign  PHAT sign   verdict
       1          1      -1  0.2403         -1          1   RIGHT
       2         -1       0  0.2013         -1         -1   WRONG
       3         -1      -7  0.0878         -1         -1   WRONG
       4          1       0  0.0869          1          1   RIGHT
       6         -1       0  0.0560         -1         -1   WRONG
       8          1       0  0.0415          1          1   RIGHT

   G3 boxes, overlapping     (sub 30-120 Hz, main 100-8000 Hz, both band-pass)
       N  peak sign     lag     rho  r[0] sign  PHAT sign   verdict
       1          1      -1  0.1402          1          1   RIGHT
       2         -1       2  0.1124         -1         -1   WRONG
       3         -1     -56  0.0723         -1         -1   WRONG
       4         -1    -148  0.0571          1          1   WRONG
       6          1    -126  0.0466         -1         -1   RIGHT
       8          1    -304  0.0400         -1         -1   RIGHT

   G4 boxes, edges touching  (sub 30-100 Hz, main 100-8000 Hz, both band-pass)
       N  peak sign     lag     rho  r[0] sign  PHAT sign   verdict
       1          1      -1  0.1112          1          1   RIGHT
       2         -1       1  0.0909         -1         -1   WRONG
       3         -1     -91  0.0486         -1         -1   WRONG
       4         -1    -218  0.0354          1          1   WRONG
       6          1    -225  0.0254         -1         -1   RIGHT
       8          1    -470  0.0191          1         -1   RIGHT

   G5 sub LP + main band-pass (sub low-pass 100 Hz, main 100-8000 Hz band-pass)
       N  peak sign     lag     rho  r[0] sign  PHAT sign   verdict
       1          1      -1  0.1316          1          1   RIGHT
       2         -1       1  0.1149         -1         -1   WRONG
       3          1      75  0.0476         -1         -1   RIGHT
       4          1       2  0.0468          1          1   RIGHT
       6         -1       1  0.0301         -1         -1   WRONG
       8          1       1  0.0223          1          1   RIGHT
```

**Read the table this way.**

- **G1/G2 — the matched-cutoff complementary pair, i.e. the topology §3
  describes — obey the identity exactly.** Wrong at 2 and 6 (180°, correctly a
  negative peak); right at 4 and 8 (0°); scattered at 1 and 3, where the pair is
  in quadrature, `r[0]` is numerically zero, and the peak lands a quarter-cycle
  to whichever side wins. G1 and G2 disagree with each other at orders 1 and 3 —
  which is the clearest possible demonstration that an odd-order sign is not a
  property of the topology.
- **G3 and G4 reproduce the L4a report exactly — under the UN-WHITENED rule
  only: right at 1, WRONG at 2, WRONG at 4, right at 8.** Both are pairs of
  band-pass boxes — the box model `tools/probe_polarity_bandwidth.py:132-152`
  builds and the only one this repo has committed.
- **The shipped PHAT rule does NOT reproduce it, and the disagreement is at
  exactly the two load-bearing orders.** On G3 and G4 the `PHAT sign` column
  reads `+1` at order 4 where the plain peak reads `−1`, and `−1` at order 8
  where the plain peak reads `+1`. So `findDelayPhat` on that band-pass pair
  agrees with the identity at order 4 and **contradicts it at order 8** — the
  mirror image of the L4a pattern, not a reproduction of it. Whitening flattens
  the magnitude before the transform, which reweights the overlap band and moves
  the peak onto a different lobe; it does not fix anything, it relocates the
  failure. On the matched pairs G1/G2 the two rules agree everywhere except G1
  order 3, which is quadrature and has no defined sign to agree about.
- G5 (one complementary low-pass against a band-passed main) does **not**
  reproduce it: it reads right at 4 under both rules. So it is the **sub** being
  band-passed below, not the main being band-passed above, that does the damage
  — which matters, because a real subwoofer is a band-pass and the wizard will
  meet one.

---

## 5. Root cause, named at file:line

The mechanism is in the forensic block. For each geometry: the spread of the
HP−LP offset across the band where the two overlap within 10 dB, and whether
the correlation peak sits at lag 0 at all.

```
   order 4
                         geometry       overlap band  offset spread  sign@0  sign@pk    pk lag  |pk|/|r0|
      G1 matched crossover 100 Hz     75.4-  133.3Hz           0.0d       1        1     0.00ms          1
       G2 matched crossover 1 kHz    750.7- 1331.5Hz           0.0d       1        1     0.00ms          1
       G3 boxes, overlapping          76.9-  143.6Hz          62.9d       1       -1    -3.08ms       1.45
       G4 boxes, edges touching       76.9-  119.4Hz          62.9d       1       -1    -4.54ms       1.02
       G5 sub LP + main band-pass     75.4-  132.6Hz           2.2d       1        1     0.04ms          1
```

Two things go wrong at once, and both are properties of the **fixture**, not of
the estimator:

1. **The offset stops being constant.** A matched pair reads `0.0°` of spread —
   the identity. Two band-pass boxes read **62.9°** of spread across the same
   overlap band, because each box carries a second skirt of its own (the sub's
   30 Hz high-pass, the main's 8 kHz low-pass) whose phase does not cancel in
   the ratio. The "one delay plus one constant" model the whole ALIGN band fit
   is built on (§4) is simply false for that pair.
2. **The correlation peak leaves lag 0.** The identity constrains only the value
   **at lag 0**, and at order 4 that value is still POSITIVE in every geometry
   (`sign@0` is `+1` in all five rows above — the identity survives even in
   G3/G4). But the narrowed, phase-smeared overlap stretches the correlation
   envelope until a neighbouring lobe wins: at G3 order 4 the peak sits at
   **−3.08 ms** and is **1.45×** larger than the value at lag 0, and adjacent
   lobes of a narrowband correlation alternate in sign. The rule reports that
   lobe's sign. At order 8 the peak moves further still (−6.33 ms on G3) and
   happens to land on a positive lobe again, which is why L4a saw the sign "flip
   back" — and by then the spread has grown to **86.3°**, far enough that on G3
   even the lag-0 value has gone negative (`sign@0 = −1` in that row, the one
   place in this probe where it does). So the order-8 "right" reading is not the
   identity surviving; it is two errors landing on the same answer. The scope of
   the "lag 0 stays positive" claim is **order 4**, where all five geometries
   read `+1`, and nowhere wider.

**Which lobe wins is a property of the ESTIMATOR, not only of the pair — and
that is the sharpest thing in this section.** Run the identical G3 pair through
the shipped PHAT rule instead and the peak lands somewhere else again: `+1` at
order 4, `−1` at order 8, the mirror of the un-whitened reading at both. Neither
rule is reading the identity. Whitening moved the failure from order 4 to order
8; it did not remove it, and nothing in either rule reports that it has stopped
tracking lag 0. **Two correlators over one unchanged pair of loudspeakers,
disagreeing about polarity at two of six orders, is the whole argument against
reading a topology sign off any correlation peak.**

**So the L4a order-4 reading is a fixture artefact of the un-whitened
correlation-peak-sign rule applied across two different passbands, not a
counter-example to the identity.** L4a's
own text already says the right thing about it, at
`docs/dsp/2026-08-30-sweep-ir-l4a.md:1166-1172`:

> the parenthetical below — that cross-correlation against a stored trace "is
> sound at any bandwidth" — is **true only for the same system measured twice**.

The precise attribution, in file:line terms:

| What | Where |
|---|---|
| The claim ("opposite sign at orders 2 and 4") | `docs/dsp/2026-08-30-sweep-ir-l4a.md:1169-1170` |
| The rule that produces it | `docs/dsp/2026-08-30-sweep-ir-l4a.md:1195` — decision 6b's **un-whitened** `ρ = \|peak\| / √(E₁E₂)` and the sign at that peak. **Not shipped code**: `relativePolarity()` exists nowhere in `core/` or `app/` |
| The rule that does NOT produce it | `core/src/dsp/DelayFinder.cpp:34` — `result.inverted = peak.height < 0.0`, off a **PHAT-whitened** cross-spectrum. On the same band-pass pair it reads the identity's sign at order 4 and contradicts it at order 8: the mirror of the L4a pattern (§4) |
| The box model that reproduces it | `tools/probe_polarity_bandwidth.py:132-152` (`band_sos`, band-pass both sides) |
| The identity it appears to contradict | `docs/dsp/2026-09-06-l7-alignment-wizard.md` §3 |
| Why no convention can explain it | `core/src/dsp/DualFftEngine.cpp:252`, `core/src/dsp/TransferEstimator.cpp:108` — see §6 below |

**NOT VERIFIED:** that the L4a session's fixture was *these exact* band edges.
The fixture is not committed and nobody's memory was consulted. What is verified
is that a band-pass pair of the kind L4a's own surveys build reproduces the
reported pattern at all four orders it names, and that no matched-cutoff pair
does at any of them. That is enough to stop the finding being read as a
counter-example to §3, which is the only thing Wave 3 needed.

---

## 6. No convention in this engine can reach the order-4 sign

This is the part that makes the attribution safe rather than merely plausible,
and it is the reason `memory/dual-fft-conventions.md` turned out **not** to be
the culprit this smelled like.

```
   crossover fc = 100 Hz, offset read over 25 .. 400 Hz
     N  predicted   repo conv.   conjugated   reachable by a flip?
     1     90.00d       90.00d      -90.00d    yes -- sign differs
     2    180.00d     -180.00d      180.00d NO -- both read the same
     3    -90.00d      -90.00d       90.00d    yes -- sign differs
     4     -0.00d       -0.00d        0.00d NO -- both read the same
     6    180.00d     -180.00d      180.00d NO -- both read the same
     8     -0.00d        0.00d       -0.00d NO -- both read the same
```

`Sxy = conj(X)·Y` with X the reference (`DualFftEngine.cpp:252`) and
`phase = atan2(Im h, Re h)` (`TransferEstimator.cpp:108`) give
`arg H = arg Y − arg X`; with X the common electrical drive the measured offset
**is** `arg H_HP − arg H_LP`, with no intervening sign. Flipping item 1 of
`memory/dual-fft-conventions.md` — `Sxy = X·conj(Y)` — conjugates the whole
trace and negates the offset. Modulo 360, `−0° = 0°` and `−180° = 180°`. The
same holds for the delay sign (item 2), which only adds a term linear in `f` and
cannot change a frequency-independent constant's residue mod 180°.

**Only the odd orders — the ±90° rows — are reachable by a convention flip, and
those are exactly the orders whose correlation peak has no defined sign anyway.**
An even-order sign discrepancy is proof that the two systems are not a matched
complementary pair. That is a diagnostic the wizard can use, not just a
negative result.

---

## 7. What CI now locks

`core/tests/test_align_order4_identity.cpp` (new file; `core/tests/CMakeLists.txt`
gains one line to register it; **no `core/` or `app/` code changed**). Five
Catch2 cases, all closed-form under CLAUDE.md's verification standard
preference 1 — the analog Butterworth prototype is built in the fixture because
`rta::dsp::ButterworthDesign` offers only `bandPass` (`ButterworthDesign.h:41-52`)
and adding a low-pass/high-pass design is Wave 3's job, not a probe's.

1. a normalised Butterworth's pole product is exactly 1
2. Butterworth HP leads LP by N·90° at EVERY frequency (N = 1..8, 64 points)
3. Linkwitz-Riley inherits the identity with the SAME N (LR2/LR4/LR8)
4. the order-4 crossover sums IN PHASE and the order-2 one nulls
5. only an ODD-order crossover's reading can be flipped by a convention

This is the first half of ALIGN §10 item 2, built now rather than in Wave 3, so
the question cannot be reopened from the code side. Wave 3's test file should
fold it in rather than re-derive it.

**Made red once, deliberately** (`memory/mutation-testing-needs-the-exe-deleted-first.md`
— the exe was deleted before the rebuild). Changing case 4's order-4 row to
assert the L4a claim (`Row{4, false}`, i.e. 180° at order 4):

```
test_align_order4_identity.cpp(177): FAILED:
  CHECK( std::abs(lp + hp) < 1e-15 )
with expansion:
  1.41421356237309537 < 0.000000000000001
with message:
  order 4

test_align_order4_identity.cpp(178): FAILED:
  CHECK_THAT( 20.0 * std::log10(std::abs(lp - hp)), WithinAbs(kThreeDb, 1e-12) )
with expansion:
  -306.84271675369200238 is within 0.000000000001 of 3.01029995663981209
with message:
  order 4

test cases:  1 |  0 passed | 1 failed
assertions: 24 | 22 passed | 2 failed
```

`1.41421356237309537` is `√2`, i.e. exactly +3.0103 dB: the order-4 pair sums in
phase.

**Case 3 was strengthened after a verifier found it blind.** As first written,
the Linkwitz-Riley case asserted only the *squared* offset, and squaring maps
`x → 2x`: a mutation reversing the half-order Butterworth's sign takes ±90° to
∓90°, and `2·(±90°)` is `±180°`, the same number modulo 360. Every LR row read
identically and the case stayed green. It now checks the **un-squared**
half-order offset alongside. Mutating `highPass` to `(−s)^N/D` and rebuilding
with the exe deleted first:

```
LR2 at w = 100
test cases:   1 |   0 passed |  1 failed
assertions: 192 | 160 passed | 32 failed
```

32 of 192 — exactly LR2's half-order checks, the only odd half-order in the set
and therefore the only one the mutation can reach. Reverted.

Restored, rebuilt from the current tree with the exe deleted first:

```
cmake -S . -B build-probe -G "Visual Studio 18 2026" -A x64 -DRTA_BUILD_APP=OFF
cmake --build build-probe --config Release --parallel
ctest --test-dir build-probe -C Release --output-on-failure

100% tests passed, 0 tests failed out of 556
Total Test time (real) =  21.29 sec
```

551 before this file, 556 after — measured, not counted by hand:
`ctest -N -E "<the five new names>"` returns `Total Tests: 551`.

---

## 8. The ruling the ALIGN builder can rely on

**The `N·90°` topology identity holds under this repo's own convention, exactly,
at every order and every frequency, analog and digital, Butterworth and
Linkwitz-Riley — so §3's table ships unchanged and a row for a new *order* stays
arithmetic.** The L4a fixture measured the wrong sign at order 4 because it
applied decision 6b's **un-whitened** correlation-peak-sign rule
(`ρ = |peak| / √(E₁E₂)`, `docs/dsp/2026-08-30-sweep-ir-l4a.md:1195` — an
estimator this repo does not ship) across two systems with different passbands:
a band-pass sub against a band-pass main spreads the HP−LP offset by 62.9°
across the overlap instead of holding it constant, which stretches the
correlation envelope until a neighbouring, oppositely-signed lobe outgrows the
value at lag 0 (peak at −3.08 ms, 1.45× the lag-0 value) — while the order-4
value *at lag 0* stays positive in every geometry, exactly as the identity says.
The correlator this repo **does** ship, PHAT-whitened `findDelayPhat`, does not
reproduce the L4a pattern; on the identical pair it reads the mirror of it,
right at order 4 and wrong at order 8, because whitening reweights the overlap
and relocates the winning lobe. No sign convention can produce either reading:
conjugating the transfer function negates the offset, and −0° = 0° and
−180° = 180°, so an even-order pair reads identically either way. **The wizard
must therefore (a) keep §3's table, (b) never read a topology sign off ANY
correlation peak, whitened or not — the §4 complex band fit with its bounded
agreement `R` is the estimator, and `R` collapsing is precisely how it reports
"these two are not a matched pair", and (c) treat an even-order sign discrepancy
as evidence about the *system* (a band-passed source, a processor whose
high-pass is `(−s)^N/D`, or a wiring inversion the operator must be asked about
under question (c)), never as evidence about a convention.**

---

## 9. What is still open, and what is not

**Settled by this probe, no owner input required:**

- §13.1's order-4 attribution. The identity is confirmed; the L4a reading is
  attributed to the fixture's geometry **and** to decision 6b's un-whitened
  correlation-peak-sign rule, which is not shipped code. The shipped PHAT
  correlator fails differently on the same pair, which is why the ruling bans
  both rather than preferring one.
- ALIGN §1 correction 1 (BW2 sum) — confirmed against research D1/D6.
- Whether a dual-FFT sign convention could be responsible — it cannot, at any
  even order.

**Still open, and this probe does not touch it:**

- §13.2 (a real sub/main pair with a rack and a microphone), §13.3 (question
  (c)'s "unknown" branch), §13.4 (the ρ ruling wording). Unchanged.
- The ρ threshold two-grid pass of §8. Step 4 of that plan ("cross-system cells
  are in the grid as expected refusals") now has a **known answer to check
  against** rather than an open discrepancy to settle: G3/G4 above are exactly
  those cells, and the ρ they read (0.019–0.14) is the figure the two grids must
  reproduce.
- **NOT VERIFIED:** the exact band edges of the original L4a fixture. See §5.

**Wave 3 (L7-ALIGN) is unblocked.**

---

## Sources

This repo: `docs/dsp/2026-09-06-l7-alignment-wizard.md` §1, §3, §4, §8, §10,
§13; `docs/dsp/2026-08-30-sweep-ir-l4a.md` decisions 6/6b and "What G21
promises an operator about a subwoofer" amendment 2 (`:1166-1180`);
`docs/research/2026-09-06-l7-alignment-wizard-station1-research.md` D1, D6;
`core/src/dsp/DualFftEngine.cpp:252`, `core/src/dsp/TransferEstimator.cpp:108`,
`core/src/dsp/DelayFinder.cpp:34`, `core/include/rta/dsp/ButterworthDesign.h:41-52`,
`core/include/rta/ir/Polarity.h`, `tools/probe_polarity_bandwidth.py`;
`memory/dual-fft-conventions.md`, `memory/a-gen-script-runs-the-moment-you-invoke-it.md`,
`memory/mutation-testing-needs-the-exe-deleted-first.md`,
`memory/a-threshold-read-off-a-grid-is-that-grids-floor.md`.
External: Linkwitz, S., "Active Crossover Networks for Noncoincident Drivers",
JAES 24(1), 1976; Rane Note 160 (Bohn), "Linkwitz-Riley Crossovers: A Primer".
The identity in §2 and the closed forms in §3 were evaluated by this probe, not
taken on citation.
