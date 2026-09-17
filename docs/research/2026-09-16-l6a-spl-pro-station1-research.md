# Lane L6a — station-1 research and the decisions it produced

*2026-09-16. Four read-only research agents ran in parallel on HEAD `6d9a53d`:
IEC 61672-1 / ISO 1996 (Part A1–A2), IEC 61252 and the public occupational
regulations (Part A3–A4), the source code of open-source analysers (Part B1),
and commercial practice plus the report/web-viewer/alarm question (Part B2).
Part C is this session's own read of the repository. Their findings are
preserved here because the decision record
(`docs/dsp/2026-09-16-spl-pro-l6a.md`) cites them and a citation is not the
finding. Part E maps every decision to its evidence and to the alternative it
rejected.*

Scope under research: SPL logging with history plots, alarms/traffic-light, PDF
reports and remote web viewing (**G7**), and noise dose / exposure with IEC
61252 and OSHA/NIOSH presets (**G8**) — the L6a row of
`docs/plans/MASTER-EXECUTION-PLAN.md:140`.

**Every external URL below was read on 2026-09-16.** Each claim is tagged
**VERIFIED** (read on a primary or authoritative source that names the clause or
the line) or **UNVERIFIED** (secondary, inferred, or arithmetically confirmed
rather than read). The two are never blurred: several of the most-quoted figures
in this field are published only as images in their official sources and could
be confirmed against text tables but not read.

---

## Part 0 — Seven corrections to the brief this lane was opened with

Seven premises were wrong. Three would have reached code.

1. **The Meters dependency is satisfied in `core/` only.** Nothing outside
   `core/` has ever called `rta::meter` or `rta::dsp::Weighting`. See §C1.

2. **IEC 61672-1:2013's Table 2 is not the weighting tolerance table.**
   Table 2 is *"Acceptance limits for deviations of directional response from
   the design goal"*; the weighting tolerances are in **Table 3, "Frequency
   weightings and acceptance limits"**. The repo names Table 2 in three places.
   See §A1.8.

3. **Clause numbers moved between IEC 61672-1 Ed 1 (2002) and Ed 2 (2013).**
   A citation without an edition year is a coin flip. See §A1.1.

4. **IEC 61252 has a new edition.** **Edition 2.0 was published 2025-10** and
   cancels and replaces 1993 + A1:2000 + A2:2017. It is a technical revision:
   different ranges, C-weighted peak now mandatory, F/S time weightings
   specified, and non-equal-energy exchange rates promoted from "permitted as a
   display conversion" to first-class specified quantities. There was no "2017
   confirmation" — 2017 was Amendment 2. See §A3.

5. **IEC 61252 specifies no criterion level and no criterion duration.** The
   brief assumed "85 dB(A) / 8 h from IEC". It is not: 61252 defines the
   criterion *parametrically* and says in its own Note that the value "is
   typically chosen to be a legal limit. Such limits vary between jurisdictions
   and are subject to change." 85 dB(A)/8 h is NIOSH's REL and the EU's upper
   action value, not IEC's. See §A3.3.

6. **The exchange-rate arithmetic is not what the brief (or this session's own
   first draft) assumed.** "3 dB exchange" in the standards means
   `q = 10 exactly`, not `10·log10(2) = 3.0103` and not `2^(ΔL/3)`. Both IEC
   61252 Ed 2 Formula (7) and ANSI S1.25-1991 clause 4.7 fix it at 10 by fiat,
   and NIOSH's own published `TWA = 10.0·log(D/100) + 85` confirms it. The
   `2^(ΔL/3)` reading comes from IEC's *other* formula, the non-equal-energy
   one, evaluated at Q = 3. See §A3.4 — this is the single most consequential
   correction in the pass.

7. **REW's API is not read-only.** L6b §10 took the *bind* default from REW,
   correctly. But REW's own API page says "PUT and POST are also supported by
   default for most endpoints" and warns "there are no delete confirmations".
   Read-only-plus-localhost is therefore **more** conservative than the cited
   precedent, not equal to it. See §D1.

**And one thing that changed while this pass was running.** The remote-API lane
this research treated as unwritten was written the same day:
`docs/dsp/2026-09-16-remote-api.md` — read at head `5b62218` and **merged to
`main` as PR #11 at `a39a02e` on 2026-09-17**. Its §12 rules directly on G7's
web half and
its station-1 pass reached the same conclusion about the meters from the other
side. §D3 records what that means for this lane.

---

# Part A — the standards

## A1. IEC 61672-1:2013 (sound level meters)

Primary source: the IEC official preview PDF of Ed 2.0 2013-09 (cover, complete
Contents with every clause and table title, Foreword, Introduction, Scope,
Normative references, and **the whole of clause 3 from 3.1 to 3.31 including
Equations (1)–(6) in the standard's own typesetting**), served by authorised
distributor iteh.ai:
`https://cdn.standards.iteh.ai/samples/17900/df52d949fc904f329404e965b6268258/IEC-61672-1-2013.pdf`.
Ed 1 (2002) preview for the renumbering comparison:
`https://cdn.standards.iteh.ai/samples/10129/775315f9a8db4fb282eb00a978dbba8e/IEC-61672-1-2002.pdf`.
Parts 2 and 3 previews (clauses 4–7.1 and 1–12.2 respectively, in full):
`.../17901/...IEC-61672-2-2013.pdf`, `.../17902/...IEC-61672-3-2013.pdf`.
ISO OBP and Electropedia both returned HTTP 403 and were not used.

### A1.1 The Ed 1 → Ed 2 renumbering — VERIFIED

| Topic | 2002 Ed 1 | **2013 Ed 2** |
|---|---|---|
| Level linearity | 5.5 | **5.6** |
| Self-generated noise | 5.6 | **5.7** |
| Time-weightings F and S | 5.7 | **5.8** |
| Toneburst response | 5.8 | **5.9** |
| Overload indication | 5.10 | **5.11** |
| Under-range indication | 5.11 | **5.12** |
| C-weighted peak sound level | 5.12 | **5.13** |
| Display | 5.15 | **5.18** |
| Timing facilities | 5.17 | **5.20** |

Cross-checked against Norsonic's NorCal manual, which maps Ed 1.0→Ed 2.0
explicitly (`https://manual.norsonic.com/NorCal/iec-61672-part-3.html`) —
consistent. **Caution:** `https://sonavyx.com/en/insights/iec-61672-1-time-weighting`
cites "clause 5.6" for time weighting, which is Level linearity. It ranks well
and reads as generated; do not use it.

### A1.2 Definitions this lane depends on — VERIFIED, clause 3

- **3.4 time weighting** — an exponential function of time, of a specified time
  constant, weighting the **square** of the sound-pressure signal.
- **3.6 time-weighted sound level**, Equation (1), with Figure 1 giving the
  chain: square → **one real pole at −1/τ** → log₁₀ → dB re (20 µPa)². Ed 1's
  clause 3.5 wrote the same quantity as `20 lg[(1/τ)∫p²e^(…)dξ]^(1/2)/p₀`;
  Ed 2 writes `10 lg[(1/τ)∫p²e^(…)dξ]/p₀²`. **Algebraically identical** — the
  rewording is not a spec change.
- **3.7 maximum time-weighted sound level**, with example symbols `L_AFmax`,
  `L_ASmax`, `L_CFmax`, `L_CSmax`.
- **3.8 peak sound pressure** — greatest sound pressure, **positive or
  negative** (Note 2 is explicit), during a stated interval.
- **3.9 peak sound level** — `10 lg(frequency-weighted peak p² / p₀²)`, defined
  generically for any weighting.
- **3.10 time-averaged sound level / equivalent continuous sound level**,
  Equation (2): `L_Aeq,T = 10 lg[(1/T)∫_{t−T}^{t} p_A²(ξ)dξ / p₀²] dB`.
  **Note 3 to entry 3.10 states that time weighting is in principle not
  involved in determining time-averaged sound level.** This is the clause to
  cite against any proposal to compute Leq from an F-weighted stream.
- **3.11 sound exposure**, Equation (3): `E_A,T = ∫ p_A²(t)dt`, unit Pa²·s.
- **3.12 sound exposure level**, Equation (4), with
  `E₀ = p₀²T₀ = (20 µPa)² × (1 s) = 400×10⁻¹² Pa²s` and *"T₀ is the reference
  value of 1 s for sound exposure level"*. Equation (4) also gives
  `L_AE,T = L_Aeq,T + 10 lg(T/T₀)`; Equations (5) and (6) invert it.

### A1.3 Time constants — UNVERIFIED

Clause **5.8** exists (ToC, p. 24) but its body is past the preview cut. The
values τ_F = 125 ms and τ_S = 1 s come from vendor pages only
(`https://www.nti-audio.com/en/support/know-how/fast-slow-impulse-time-weighting-what-do-they-mean`),
which also attribute I (35 ms attack / 1500 ms decay) to the **superseded
IEC 651**. Nobody disputes the values; they were simply not readable on a
primary page.

### A1.4 Time-weighting I is gone from Ed 2 — VERIFIED by absence

Ed 1 had *Annex C (informative) Specifications for time-weighting I (impulse)*
and *Annex B (informative) AU weighting*. Ed 2's annexes are A (tolerance /
acceptance interval), B (normative, maximum permitted uncertainties), C (example
conformance assessments), D (normative, fractional-octave frequencies) and
E (normative, analytical expressions for C, A and Z). **No I annex, no AU
annex.** Several secondary sources still assert otherwise; that was true of
Ed 1. The repo's `Detector.h` already labels Impulse "outside the current IEC
normative scope", which this confirms exactly.

### A1.5 Percentile levels are not in IEC 61672-1 — VERIFIED

Clause 3 was read continuously from 3.1 to 3.31 and contains no `L_N`; the
complete clause-5 ToC (5.1–5.23) contains no percentile clause; no annex covers
it. *Caveat:* clause 3 runs to standard p. 14 and the preview ends at p. 13, so
entries after 3.31 were not seen — but with no clause-5 specification to support
them, a percentile definition there is implausible. **Ln lives in ISO 1996-1**
(§A2).

### A1.6 Clause 5 structure, and the validity predicate — VERIFIED

Full ToC: 5.1 General · 5.2 Adjustments at the calibration check frequency ·
5.3 Corrections to indicated levels · 5.4 Directional response · 5.5 Frequency
weightings · 5.6 Level linearity · 5.7 Self-generated noise · **5.8
Time-weightings F and S** · 5.9 Toneburst response · 5.10 Response to repeated
tonebursts · **5.11 Overload indication** · **5.12 Under-range indication** ·
**5.13 C-weighted peak sound level** · 5.14 Stability during continuous
operation · 5.15 High-level stability · 5.16 Reset · 5.17 Thresholds ·
**5.18 Display** · 5.19 Analogue or digital output · 5.20 Timing facilities ·
5.21 RF emissions · 5.22 Crosstalk · 5.23 Power supply. **5.14 and 5.15 are new
in Ed 2** — relevant to a logger that runs for days.

The range definitions, all clause 3 and all VERIFIED:

- **3.22 level range** — nominal sound levels for one setting of the controls.
- **3.24 reference level range** — the range specified for testing.
- **3.26 level linearity deviation** — indicated minus anticipated, at a stated
  frequency.
- **3.27 linear operating range** — *on any level range and at a stated
  frequency*, the range over which level linearity deviations do not exceed the
  acceptance limits. Two qualifiers: **per level range and per frequency**, not
  one number for the instrument.
- **3.28 total range** — A-weighted, sinusoidal, from the smallest level on the
  most sensitive range to the greatest on the least sensitive, measurable
  **without indication of overload or under-range and without exceeding the
  level-linearity acceptance limits**.

**Clause 3.28 is the validity predicate a logging meter needs**, and it is
written into the definition itself: a datum is in-spec only if it is inside the
linear operating range for that level range at that frequency and neither
overload nor under-range is indicated.

Overload and under-range indication are **mandatory facilities** — VERIFIED
from IEC 61672-2:2013 clauses 6.2–6.4 (time-weighting SLM must display L_AF and
indicate both; integrating-averaging must display L_Aeq and indicate both;
integrating must display L_AE and indicate both), and **6.11**: lacking any
mandatory facility, the instrument does not conform and pattern-evaluation tests
are not performed. **6.9** requires level-range overlap to conform.

The *bodies* of 5.11 and 5.12 are UNVERIFIED. From the NPL guide to the Ed 1-era
part 2 (`http://resource.npl.co.uk/acoustics/techguides/soundpower/slm_tg_61672_2.pdf`,
Ed 1 numbering, directional only): the overload indicator **latches on**
(Ed 1 5.10.5); linearity must hold up to the first overload indication and down
to the first under-range indication; the overload test drives positive and
negative half-cycles in 0.1 dB steps and bounds the polarity asymmetry. The
latching behaviour and the polarity asymmetry are the two things worth designing
for.

### A1.7 Display and reporting — mostly UNVERIFIED, and one clean negative

- **Clause 5.18 Display** exists (p. 29); its body was not readable on any
  legitimate free source and is not guessed here.
- A display resolution and minimum display range **are** mandated — VERIFIED
  indirectly from IEC 61672-2:2013 clause 6.5: display devices "shall be
  verified to be able to display sound levels or sound exposure levels with the
  resolution required by IEC 61672-1. The range of the display shall be at least
  the minimum specified in IEC 61672-1." The requirement provably exists; the
  values do not appear in any free text found.
- **0.1 dB is strongly implied but not confirmed as a mandate.** IEC
  61672-2:2013 clause 6.21 and IEC 61672-3:2013 clause 4.2 both list an
  uncertainty component "associated with the resolution of the display
  device… For digital display devices that indicate signal levels with a
  resolution of **0,1 dB**, the uncertainty component should be taken as a
  rectangular distribution with semi-range of **0,05 dB**." That is the test
  standards *assuming* 0.1 dB, not clause 5.18 requiring it. Note that
  **0.05 dB semi-range is exactly the bound the histogram decision derives
  independently** (§E, decision 5).
- **Display update rate: nothing found. No clause, no value.**
- **There is no measurement-report clause in IEC 61672-1** — VERIFIED from the
  ToC. Its documentation clauses are **8 Marking** and **9 Instruction Manual**
  (9.1 General; 9.2 Information for operation 9.2.1–9.2.8; 9.3 Information for
  testing), which bind the *manufacturer*, not the measurement. The reporting
  clause is ISO 1996-2 clause 13 (§A2.2).

### A1.8 Class 1 vs class 2, and which table holds what — VERIFIED

From the Scope, in the standard's own words: class 1 and class 2 "have the same
design goals and differ mainly in the **acceptance limits** and the **range of
operational temperature**. Acceptance limits for class 2 are greater than, or
equal to, those for class 1."

So the *design goals* — the A/C/Z curves, the F/S time constants, the Leq and
SEL definitions — are **identical**. For a software meter this matters: one
algorithm, and class is a statement about the realised chain plus the tolerance
it holds.

- **Table 2** = acceptance limits for **directional response**.
- **Table 3** = **"Frequency weightings and acceptance limits"** — design goal
  *and* tolerances in one table. **The repo's three references to a "Table 2
  tolerance envelope" name the wrong table.**
- Table 4 = toneburst; Table 5 = C-weighted peak; Table B.1 = maximum permitted
  uncertainties.
- **Annex E (normative) "Analytical expressions for frequency-weightings C, A
  and Z"** (p. 49) is the annex `core/src/dsp/Weighting.cpp` implements from.
- **Annex D (normative)** gives the fractional-octave frequency grid (Tables
  D.1/D.2/D.3), which is the grid Table 3's limits are tabulated on — the exact
  point the weighting record already makes about nominal vs exact frequencies.
- **C-weighting is mandatory for class 1 and optional for class 2** — VERIFIED
  from IEC 61672-2:2013 clause 6.7. A genuine non-tolerance difference.
- Class 1 temperature range −10 °C to +50 °C — VERIFIED, but from *another*
  standard's normative text: ISO 1996-2:2017 clause 5.1 NOTE 1. Class 2's
  0 °C to +40 °C is UNVERIFIED (manufacturer pages only).
- **Table 3's actual numbers, and therefore the class 1 / class 2 frequency
  limits, remain UNVERIFIED.** Secondary sources disagree on the *shape* of the
  answer: PTB (Kling et al. 2021,
  `https://oar.ptb.de/files/download/681c533d53218de84f08e859`) says the
  specifications are "limited to the standard frequency range… from 10 Hz to
  20 kHz", with class 2 limits going to **−∞** outside roughly 20 Hz–8 kHz;
  Cirrus Research
  (`https://cirrusresearch.com/whats-the-difference-between-a-class-1-and-class-2-sound-level-meter/`)
  gives sample values consistent with that (1 kHz ±1.1 / ±1.4 dB; 20 Hz ±2.5 /
  ±3.5; 16 Hz +2.5/−4.5 vs +5.5/−∞; 10 kHz +2.6/−3.6 vs +5.6/−∞), while other
  pages state flat ranges ("class 1: 16 Hz–16 kHz"). The −∞ framing is the one
  consistent with a tolerance table; the flat-range framing is a
  simplification. **All specific dB figures above are UNVERIFIED.**

## A2. ISO 1996-1:2016 and ISO 1996-2:2017 (environmental noise)

Primary sources, both IEC/ISO-typeset previews via iteh.ai:
`.../59765/b0c065255b7a45658425773086323f0e/ISO-1996-1-2016.pdf` (ToC +
clause 3.1–3.3 complete) and
`.../59766/dbeb6253754b42f193ff53bd64e835b1/ISO-1996-2-2017.pdf` (ToC +
clauses 3, 4, 5 and 6 complete).

### A2.1 ISO 1996-1:2016 — where Ln actually lives — VERIFIED

Its single normative reference is **IEC 61672-1**; it does not redefine
weightings or detectors (clause 3.1.1 Note 4: the standard frequency weightings
are A and C and the standard time weightings are F and S, "as specified in
IEC 61672-1").

- **3.1.2 maximum time-weighted and frequency-weighted sound pressure level** —
  `L_AFmax` is standardised here.
- **3.1.3 N percentage exceedance level** — the time-weighted and
  frequency-weighted SPL exceeded for N % of the time interval considered. The
  standard's own worked example is **`L_AF95,1h`**. Note what the notation
  carries: **frequency weighting, time weighting, N, and the interval**. A bare
  "L90" names neither the detector that produced it nor the window it covers,
  and two instruments printing "L90" are not necessarily measuring the same
  thing.
- **3.1.4 peak sound pressure level**, Note 3: peak should be determined with a
  detector as defined in IEC 61672-1, and **IEC 61672-1 specifies the accuracy
  of a peak detector only for C-weighting**. So `L_Zpeak` and `L_Apeak` are
  computable but unspecified.
- **3.1.5 sound exposure level** with `E₀ = 400 µPa²s`; **3.1.6 equivalent
  continuous sound pressure level**.
- A NOTE under 3.1 makes it **mandatory to state the frequency weighting (or
  bandwidth) and, where applicable, the time weighting** for every level in
  3.1.1–3.1.6.
- Scope: ISO 1996-1 **does not specify limits**.
- Reporting: clause **8.2 Test report** (body not visible).

### A2.2 ISO 1996-2:2017 — the clauses a logger must be built around

**Clause 5.2 Calibration — VERIFIED, body read in full.** Check the whole system
with a **class 1 IEC 60942** calibrator **at the beginning and at the end of
every measurement**; the difference between two consecutive checks with no
adjustment in between shall be **≤ 0,5 dB**; **if exceeded, discard all results
since the previous satisfactory check.** For monitoring over several days,
ISO 20906:2009/Amd 1:2013 applies. *This is the only published numeric
criterion for calibration drift found anywhere in the pass, and it comes from a
normative clause.*

**Clause 5.1 General — VERIFIED.** The instrument chain *including microphone,
cables, windscreen, recording devices and accessories* shall meet **class 1**
per IEC 61672-1; filters shall meet class 1 per IEC 61260; **a windscreen shall
always be used outdoors**.

**Clause 5.3 Verification — VERIFIED.** Valid certificate against IEC 61672-3 /
IEC 61260 / IEC 60942 from an ISO/IEC 17025 lab; recommended interval 1 year,
maximum 2 years.

**Clause 4 and Table 1 — VERIFIED.** Uncertainty per GUM, coverage 95 % with
k = 2. Table 1 footnote a: `u(L′) = 0,5 dB` for a class 1 SLM and **1,5 dB for a
class 2 SLM**. That 1.0 dB gap is the concrete cost of class 2 in an ISO 1996-2
uncertainty budget.

**Clause 10 Evaluation of the measurement results — ToC VERIFIED, bodies
UNVERIFIED.** 10.1 General; **10.2 Determination of L_E,T, L_eq,T and L_N,T**
(10.2.1 L_E,T and L_eq,T; **10.2.2 L_N,T**); **10.3 Treatment of incomplete or
corrupted data** (10.3.1 General, 10.3.2 Wind sound); 10.4 residual-sound
correction; 10.5 standard uncertainty; 10.6 L_den; **10.7 Maximum level L_max**.

**Clause 13 "Information to be recorded and reported" — existence VERIFIED,
body UNVERIFIED.** This is the reporting clause for a logging meter, and it is
the one document in the whole pass worth buying if the export format is to be
specification-backed rather than invented.

## A3. IEC 61252 (personal sound exposure meters)

Primary sources, IEC official preview PDFs via authorised distributor iteh.ai —
**both contain the complete normative Clause 3 including Formulas (1)–(9)**:

- Ed 2.0:2025 —
  `https://cdn.standards.iteh.ai/samples/iec/iec-61252-2025/1753e8defd9c4adf9ec075085935a623/iec-61252-2025.pdf`
- Ed 1.2:2017 consolidated (1993 + A1:2000 + A2:2017), redline —
  `https://cdn.standards.iteh.ai/samples/7588/365e1a9c4ff6496290994d96e03f6712/IEC-61252-1993.pdf`
- Catalogue: `https://webstore.iec.ch/en/publication/68929` (2025),
  `https://webstore.iec.ch/en/publication/5054` (1993)

So IEC 61252 turned out to be **much less paywalled than the brief assumed**:
almost everything below is VERIFIED from primary normative text.

### A3.1 Quantities and equations — VERIFIED

**Sound exposure**, Ed 2 clause 3.1 (sourced from IEC 61672-1:2013 clause 3.11),
Formula (1): `E_A,T = ∫ p_A²(t) dt`, unit Pa²s, with Note 4 making Pa²h the
convenient workplace unit.

**Normalized 8 h-average sound level**, Ed 2 clause 3.2, Formulas (2)–(5):

```
L_Aeq,8hn = 10 lg[ E / (p0² · Tn) ]                    (2)
E         = (p0² · Tn) · 10^(0,1 · L_Aeq,8hn)          (3)
L_Aeq,8hn = 10 lg[ E × 10⁹ / 3,2 ]     (E in Pa²h)     (4)
L_Aeq,8hn = L_Aeq,T + 10 lg(T / Tn)                    (5)
```

**Note 6 to 3.2 states that Formula (2) is identical to the "daily noise
exposure level" (L_EX,8h) of Directive 2003/10/EC Article 2**; Note 7 ties it to
ISO 1999's L_EX,8h. Anchor values, Note 5: `E = 1 Pa²h ≈ 85 dB`,
`E = 3,20 Pa²h = 90 dB` — check: `10·lg(10⁹/3,2) = 84,95` and
`10·lg(3,2×10⁹/3,2) = 90,00`.

**Percentage dose**, Ed 2 clause **3.9 "percentage criterion sound exposure"**,
Formula (7) — the equal-energy dose:

```
D = (100 / T_cr) ∫ 10^( 0,1 · (L_A − L_cr) ) dt                       (7)
```

**Non-equal-energy dose**, Ed 2 clause **3.10**, Formula (8):

```
D_ASQ = (100 / T_cr) ∫ 10^( (L_AS − L_cr) · lg(2) / Q ) dt            (8)
```

**Inverse (average level from dose)**, Ed 2 clause **3.11**, Formula (9):

```
L_neeAS,T = L_cr + (Q / lg 2) · lg( D_ASQ · T_cr / (100 · T) )         (9)
```

Note 4 to 3.11 says this quantity *is equivalent to average sound level as
described in ANSI S1.25-1991 clause 4.7*.

**Critical caveat, VERIFIED: clauses 3.5–3.11 are new in Edition 2.0:2025.**
Ed 1.2 has no "dose" or "criterion" definition at all. It handled dose only by
permission, in the Introduction: *"An indication of sound exposure with a unit
other than pascal-squared hours is permitted provided the manufacturer specifies
a procedure for converting the indication to pascal-squared hours, for example,
a display of 'dose' as a fraction or a percentage of a specified sound
exposure."*

### A3.2 Exchange rate — VERIFIED, and the answer is edition-dependent

**Ed 1 (1993/2000/2017): 3 dB only.** Introduction: *"It is the 'equal-energy
exchange rate' whereby a doubling (or halving) of the integration time of a
constant sound level yields a two-fold increase (or decrease) of sound
exposure."* Non-3 dB dose was tolerated only as a display conversion.

**Ed 2 (2025): 3 dB is the base, but non-equal-energy rates are first-class.**
Foreword, significant technical change (c): *"specifications for physical
quantities that do not follow the principle of equal-energy exchange rate have
been added."* Clause 3.3 defines *exchange rate* generically, 3.4 defines
non-equal-energy sound exposure with Q free, and clauses 4.16 and 5.16 specify
and test the response. Introduction: *"some jurisdictions specify quantities
that are not based on the principle of equal-energy exchange."*

So the "does it permit others" answer **flipped from no to yes in 2025**. Any
secondary source answering this without naming an edition is unreliable.

### A3.3 Criterion level and duration — VERIFIED, and the brief's premise is wrong

IEC 61252 **states no numeric criterion level or duration**. Ed 2:

- **3.5 criterion duration** — "specified duration of time integration". Note 1:
  "typically chosen to be the maximum duration of exposure for the application,
  for example the duration of a working day."
- **3.6 criterion sound level** — "specified level of a sound". Note 1: a level
  which "if maintained for longer than the criterion duration or if exceeded,
  could have adverse effects on human health."
- **3.7 criterion sound exposure** — Note 1: **"The criterion sound exposure is
  typically chosen to be a legal limit. Such limits vary between jurisdictions
  and are subject to change."**

The Introduction says the same: *"The physical quantity and the value of the
limit vary between jurisdictions."* **The only 85 dB in IEC 61252 is the
informative note that 1 Pa²h ≈ 85 dB.** Criterion level, criterion duration,
exchange rate and threshold must therefore all be settings; there is no
defensible standard default.

### A3.4 The q factor — VERIFIED, with the correction that matters most

For a constant level `L` held for `T`:

```
D% = 100 × (T / T_c) × 10^((L − L_c)/q)
```

and **the exact constant at the 3 dB rate is q = 10, not 3/log10(2) = 9,9658.**
Both IEC 61252 Ed 2 Formula (7) (exponent 0,1) and ANSI S1.25-1991 clause 4.7
fix it at 10 by fiat. ANSI's symbol list is explicit and is where 16,61 actually
comes from — it is an *expression*, not a tabulated constant:

```
q = 10        for a 3 dB exchange rate
q = 5/log 2   for a 5 dB exchange rate   = 16,6096404  → "16,61"
q = 4/log 2   for a 4 dB exchange rate   = 13,2877124  → "13,29"  (US DoD)
```

ANSI clause 4.5 states the same as an integrator exponent: *"The exponent
depends on the exchange rate, being 0,6 for the 5 dB rate, 0.75 for the 4 dB
rate, and 1.0 for the 3 dB rate"* — i.e. `0,6 = 10/16,6096`,
`0,75 = 10/13,2877`. Independently cross-checked inside the standard at clause
7.2.2, which requires time ratios 104,7× / 33,1× / 16,45× for a 20,2 dB change:
these are `10^(20,2/10)`, `10^(20,2/13,2877)`, `10^(20,2/16,6096)`.

Source: ANSI S1.25-1991, OCR text at
`https://archive.org/stream/gov.law.ansi.s1.25.1991/ansi.s1.25.1991_djvu.txt`
(public by incorporation-by-reference; the law.resource.org mirror is image-only).
**VERIFIED as to the q values; the equation glyphs are reconstructed from a
garbled 1991 scan and are UNVERIFIED as literal text**, though the structure is
unambiguous from the accompanying symbol list.

ANSI clause 4.7 Eq. (1) — matching IEC Ed 2 Formula (8) in form — implements the
**threshold as `L = −∞` below `L_t`**, not as a separate gate term.

**Where IEC disagrees with itself.** IEC Formula (7) at the 3 dB rate is
`10^(ΔL/10)`; Formula (8) at `Q = 3` is `10^(ΔL·lg2/3) = 10^(ΔL/9,9658)`. Note 3
to 3.10 says the two "would be equivalent" at Q = 3 dB. They are not: the
denominators differ by `10/9,9658 = 1,00343`, i.e. **0,34 %**, and the *dose*
differs by `10^(ΔL × 3,4322e-4)` — **+2,40 % at ΔL = 30 dB**. Small, and exactly
the size of error that survives review because it looks like rounding.

### A3.5 What Ed 2 changed about the instrument — VERIFIED

| | Ed 1.2 (1993+A1+A2) | **Ed 2.0 (2025)** |
|---|---|---|
| Mandatory quantity | **Sound exposure** (Pa²h) | **Time-averaged sound level + peak sound level**; sound exposure now optional |
| Frequency weighting | A | A (cl. 4.10), applied to relative **diffuse-field** response (new) |
| Time weighting | **None — pure integration** | **F and S specified** (cl. 4.14) |
| Level range | **80 dB to 130 dB** (cl. 1.3) | **at least 67 dB to 137 dB** |
| Frequency range | **63 Hz to 8 kHz** | **20 Hz to 8 kHz** |
| Accuracy grade | Type 2 of **IEC 60804** | **class 2 of IEC 61672-1:2013** |
| Peak | **no requirement** | **C-weighted peak MANDATORY** — cl. 4.18, tested 5.18 / 6.9 |
| Directional response | explicitly **not** specified | **specified** — cl. 4.9, Table 1 |

### A3.6 "Projected dose" is a vendor convention — VERIFIED NEGATIVE

- **ANSI S1.25-1991:** the word "projected" does not occur anywhere. Definitions
  3.1–3.15, Sections 1, 2, 4, 5, 6 and 7.0–7.6 and the complete Contents were
  read. The nearest relative is Eq. (3), which normalises a partial measurement
  to an *average level*.
- **IEC 61252 Ed 2.0:2025:** absent. Clause 3 runs 3.1–3.11 and was read in
  full; the ToC for clauses 4–6 and Annexes A–D contains no projection clause.
- **IEC 61252 Ed 1.2:** absent.

Three mutually independent vendors define it identically and none cites a
clause — Castle Group (`https://www.castlegroup.co.uk/projected-dose/`, "present
accumulated dose over the logged time duration is projected forward to give the
predicted 8 hour dose"), Larson Davis
(`https://www.larsondavis.com/learn/industrial-hygiene/noise-dosimetry-terminology`)
and Faber Acoustical
(`https://www.faberacoustical.com/help_x/soundmeter_x/help_x/tools/dosimeter.html`).
All describe `D_projected = D_measured × (T_c / T_elapsed)`. **UNVERIFIED as
normative; ship it labelled as an extrapolation.**

## A4. The public occupational regulations

### A4.1 OSHA 29 CFR 1910.95 — VERIFIED (primary, public domain)

eCFR 302-redirects automated fetches to `unblock.federalregister.gov` and
osha.gov returns 403; read instead from GPO's official CFR print, 7-1-24
edition:
`https://www.govinfo.gov/content/pkg/CFR-2024-title29-vol5/pdf/CFR-2024-title29-vol5-sec1910-95.pdf`

**Paragraph (a):** *"Protection against the effects of noise exposure shall be
provided when the sound levels exceed those shown in Table G–16 when measured on
the A scale of a standard sound level meter at slow response."* → **A-weighting
and SLOW, mandatory.**

**Table G-16 — Permissible Noise Exposures**, complete: 8 h/90 · 6 h/92 ·
4 h/95 · 3 h/97 · 2 h/100 · 1½ h/102 · 1 h/105 · ½ h/110 · ¼ h or less/115 dBA.
**The 5 dB exchange rate is implicit in this table and is never stated as a
number in the regulation's body** — 8→4 h is 90→95, 4→2 h is 95→100.

**Table G-16 footnote 1** (mixed exposure): *"If the sum of the following
fractions: C1/T1 + C2/T2 … Cn/Tn exceeds unity, then, the mixed exposure should
be considered to exceed the limit value."*

**Impulse limit**, in the unnumbered text after Table G-16: *"Exposure to
impulsive or impact noise should not exceed 140 dB peak sound pressure level."*
Cite it as "29 CFR 1910.95, Table G-16 note" — it is not a lettered paragraph.

**Paragraph (c)(1)/(c)(2) — the action level:** *"an 8-hour time-weighted
average sound level (TWA) of 85 decibels measured on the A scale (slow
response) or, equivalently, a dose of fifty percent"*, "shall also be referred
to as the action level."

**Appendix A, marked "This appendix is Mandatory":**
- **I(1)(i):** `D = 100 C/T`, T from Table G-16a or its footnote formula.
- **I(1)(ii):** `D = 100(C1/T1 + C2/T2 + … + Cn/Tn)`.
- **I(2):** **`TWA = 16.61 log10(D/100) + 90`**, and "a dose of 50 percent
  corresponds to a TWA of 85 dB."

**`T = 8 / 2^((L−90)/5)` is PARTIALLY VERIFIED.** The CFR prints the formula as
a **typeset image** with no text layer, so it could not be read verbatim. It was
confirmed numerically against Table G-16a, which *is* text: L = 80 → T = 32
(`8/2^−2 = 32` ✓); L = 81 → 27.9 (`8·2^1,8 = 27,86` ✓); L = 130 → 0.031
(`8/2^8 = 0,03125` ✓). Citation: **Appendix A, footnote to Table G-16a.**

**Table G-16a extends down to 80 dBA (T = 32 h)** — the basis of OSHA's 80 dBA
dosimeter threshold for *hearing-conservation* dose, which is lower than the
90 dBA threshold used for *PEL-compliance* dose. **Two different doses from one
regulation**; an instrument offering only one cannot serve it.

Self-consistency: `D = 100·(C/8)·2^((L−90)/5) = 100·(C/8)·10^((L−90)/16,6096)`,
inverted at C = 8 h, is exactly `TWA = 16,61·log10(D/100) + 90`. The 16,61 in
Appendix A and the 5 dB exchange rate are the same statement.

**Two things to get right when citing this, because a decision record got both
wrong on the first pass.** First, **the dose is computed against Table G-16a,
not Table G-16.** Appendix A I(1)(i) says `D = 100 C/T` with "T … as given in
Table G-16a *or by the formula shown as a footnote to that table*". Table G-16
in the regulation's body is the nine-row permissible-exposure table that
paragraph (a) points at; **Table G-16a in the mandatory appendix is the
reference-duration table the dose arithmetic actually uses**, and it runs in
1 dB steps from 80 dBA. Second, **Table G-16a is itself rounded**, exactly as
NIOSH's Table 1-1 is: the exact `8/2^((L−90)/5)` at 81 dBA is **27.857618 h**
and the table prints **27.9**. So there is no asymmetry between the two
regulators here — both print an exact footnote formula and a table rounded off
it, and any acceptance test that demands `D = 100 %` on a printed table row
will fail on both.

### A4.2 NIOSH REL — publication 98-126

Citation VERIFIED: NIOSH [1998], *Criteria for a Recommended Standard:
Occupational Noise Exposure, Revised Criteria 1998*, DHHS (NIOSH) Publication
No. 98-126, June 1998, DOI **10.26616/NIOSHPUB98126**.

**Correction 2026-09-17: an earlier revision of this section declined to read
the primary, and its reason was wrong.** It recorded that
`cdc.gov/niosh/docs/98-126/pdfs/98-126.pdf` 404s (true) and that the surviving
copy at `https://stacks.cdc.gov/view/cdc/6376/cdc_6376_DS1.pdf` "is a scanned
image with no text layer, so the criteria document's own formulas cannot be
machine-read" (true of *that* copy) — and then argued from the CDC 2016
bulletin and a third-party HTML mirror instead. **A born-digital copy with a
real text layer is archived**, and the decision record's §7 was wrong in two
places as a direct result. Read and transcribed 2026-09-17 from:

`https://web.archive.org/web/2020/https://www.cdc.gov/niosh/docs/98-126/pdfs/98-126.pdf`
— 126 pages, PDF 1.4, `/Author NIOSH`, `/Creator Adobe InDesign CC 2014
(Windows)`, `/Producer Adobe PDF Library 11.0`. Text extraction returns
characters, not OCR guesses. **Printed page *N* = PDF page *N* + 18.**

All of the following are **VERIFIED from that text layer**, with printed page
numbers:

- **§1.1 REL, p.1:** *"The REL is 85 decibels, A-weighted, as an 8-hr
  time-weighted average (85 dBA as an 8-hr TWA). Exposures at and above this
  level are considered hazardous."*
- **§1.1.1, p.1** — the exposure formula, printed as a displayed fraction with
  the exponent set as a superscript on the 2: **`T (min) = 480 / 2^((L−85)/3)`**,
  followed by *"where 3 = the exchange rate."*
- **§1.1.2, p.1:** *"the REL for an 8-hr work shift is a TWA of 85 dBA using a
  3-decibel (dB) exchange rate."*
- **§1.1.3, p.2** — the dose formula `D = [C₁/T₁ + C₂/T₂ + Cₙ/Tₙ] × 100` and
  the conversion **`TWA = 10.0 × Log(D/100) + 85`** (capital `Log`, `×`).
- **Table 1-1, p.2** — "Combinations of noise exposure levels and durations that
  no worker exposure shall equal or exceed". **51 rows**, Hours / Minutes /
  Seconds columns with an en dash for an unused unit, 1 dB steps from 80 dBA
  (`25 hr 24 min`) through 129 dBA (`1 sec`) to a final `130–140 → <1` sec.
  85 → `8 hr`, 95 → `47 min 37 sec`, 100 → `15 min`, 120 → `9 sec`.
  **It has no footnote and the document nowhere says the values are rounded.**
- **Table 1-2, p.3** — "Daily noise dose as an 8-hr TWA", **121 rows** from
  `20 % → 78.0 dBA` to **`32,500,000 % → 140.1 dBA`**, with the printed footnote
  **`*TWA = 10 × Log(D/100) + 85`**.
- **§1.1.4 Ceiling Limit, p.4**, complete and verbatim — this **resolves** the
  conflict an earlier revision left open: *"Exposure to continuous, varying,
  intermittent, or impulsive noise shall not exceed 140 dBA."* One sentence, no
  footnote. It is **A-weighted and it covers impulsive noise**. §3.2 (p.19)
  explains the wording: *"Because NIOSH is recommending a 3-dB exchange rate
  with an 85-dBA REL, a ceiling limit for continuous-type noise is
  unnecessary."*
- **§1.3.3, p.4:** *"all continuous, varying, intermittent, and impulsive sound
  levels from 80 to 140 dBA shall be integrated into the noise measurements."*
  This is the source of the 80 dB(A) threshold, and it is why Table 1-1 starts
  at 80 rather than at the 85 dBA REL.

**Three errata, found by recomputing every row against the document's own
formulas** (printed values VERIFIED by rendering the cells; the "correct"
values are derived here):

| table | row | printed | from the document's formula |
|---|---|---|---|
| 1-1 | 99 dB(A) | 18 min **59** sec | 18 min **53.93** sec |
| 1-2 | 50,000 % | **102.0** dBA | **111.99** — a transposition; 45,000 → 111.5 and 60,000 → 112.8 bracket it, so 102.0 breaks monotonicity |
| 1-2 | 26,000,000 % | **139.0** dBA | **139.15**, where every other row lands within 0.05 |

**Rounding convention — derived, never stated by the document:** Table 1-1 is
round-to-nearest in its smallest printed unit, except 124 dB(A) (exact 3.516 s,
printed 3) and 127 dB(A) (exact 1.758 s, printed 1), which are truncated. So it
is not even internally consistent, which is why the decision record's fixture
bounds each row by its own printed resolution instead of asserting equality.

**What the CDC 2016 bulletin actually is** (`https://www.cdc.gov/niosh/bulletin/2016/noise.html`,
read 2026-09-16): a **six-row summary** — 8 h/85, 4 h/88, 2 h/91, 60 min/94,
30 min/97, 15 min/100 — of rows that happen to sit on the 3 dB grid. It is a
correct secondary source for the REL and the exchange rate and a **misleading
one for the table**, because its endpoint at 100 dBA is 40 dB short of the
document's.
- **Threshold — VERIFIED** at
  `https://www.cdc.gov/niosh/noise/prevent/understand.html`, "NIOSH Recommended
  Dosimeter Settings": *Exchange Rate 3-dB; Frequency Weighting A; Response
  **Slow**; **Threshold Level 80 dBA**; Measurement Range 80–140 dBA.* §1.3.3
  (MIRROR) agrees: *"all continuous, varying, intermittent, and impulsive sound
  levels from 80 to 140 dBA shall be integrated."*
- **Currency:** 98-126's NRR-derating recommendation was superseded in January
  2025 by DHHS (NIOSH) Publication No. **2025-104** (fit-testing in lieu of
  derating), DOI 10.26616/NIOSHPUB2025104 — VERIFIED, read in full. **The REL,
  exchange rate and dose formulas are untouched.**

### A4.3 EU Directive 2003/10/EC — VERIFIED (primary, eur-lex)

`https://eur-lex.europa.eu/legal-content/EN/TXT/HTML/?uri=CELEX:32003L0010`

**Article 2:** *peak sound pressure (p_peak)* = "maximum value of the
'C'-frequency weighted instantaneous noise pressure"; *daily noise exposure
level (L_EX,8h)* = time-weighted average over a nominal eight-hour working day
"as defined by international standard **ISO 1999:1990, point 3.6**"; *weekly
noise exposure level* over a nominal week of five eight-hour days.

**Article 3:**

| | L_EX,8h | p_peak |
|---|---|---|
| Exposure **limit** value | **87 dB(A)** | 200 Pa = **140 dB(C)** |
| **Upper** exposure action value | **85 dB(A)** | 140 Pa = **137 dB(C)** |
| **Lower** exposure action value | **80 dB(A)** | 112 Pa = **135 dB(C)** |

**Article 3(2)** — the asymmetry that matters: applying the **limit** values
takes account of hearing-protector attenuation; the **action** values *"shall
not take account of the effect of any such protectors."*

**On exchange rate: the directive never states "3 dB".** It inherits it by
defining L_EX,8h via ISO 1999:1990 §3.6, a pure energy average. **Cite Article 2
→ ISO 1999, never an article number for "3 dB".**

### A4.4 Peak is always a separate criterion — VERIFIED in all four

| framework | peak/ceiling rule | weighting |
|---|---|---|
| OSHA | *"Exposure to **impulsive or impact noise** should not exceed 140 dB peak sound pressure level"* (Table G-16 note) | **no weighting is stated**, and the limit is scoped to impulsive/impact noise only. "States none" is not "states Z" |
| EU 2003/10/EC | p_peak in its own column at each of three tiers; either quantity trips that tier | **C** |
| NIOSH | §1.1.4 "Ceiling Limit", p.4, verbatim and complete: *"Exposure to continuous, varying, intermittent, or impulsive noise shall not exceed 140 dBA."* | **A**, and it is a *level* ceiling, not a peak |
| IEC 61252 Ed 2 | C-weighted peak **mandatory** (cl. 4.18) while dose stays optional and parametric | **C** |

**There are three different 140s.** OSHA 140 dB peak *unweighted*; EU 140 dB(C)
*peak*; NIOSH 140 *dBA* ceiling. They share a number and measure three different
things. A single "140 dB peak" readout cannot serve all three, and conflating
them is the most likely correctness bug in a dose feature.

*(This was carried as unresolved in the first revision — "140 dBA" on a mirror
against "140 dB peak SPL for impulsive noise" in a machine summary of Chapter 3.
**Reading the born-digital primary on 2026-09-17 settles it**: p.4 reads
"140 dBA", the section is one sentence long, and there is no footnote. The
summary was wrong.)*

## A5. IEC 60942 calibrators, and what the repo would need

ISO 1996-2 clause 5.2 (§A2.2) is the operative requirement: a **class 1**
IEC 60942 calibrator, checked at the start and end of every measurement, with a
**≤ 0,5 dB** drift criterion and a mandatory discard above it. The nominal
levels in field use are **94,0 dB and 114,0 dB at 1 kHz** (confirmed by both
Smaart's and 10EaZy's own procedures, §B2). A widely repeated identical 0,5 dB
rule attributed to **ISO 9612** could not be confirmed against the ISO text
(`https://svantek.com/academy/calibration/` is the source of the common
paraphrase) and is **UNVERIFIED**.

---

# Part B — how other projects actually did it

## B1. Open-source code, read at a pinned head

| project | permalink base | head SHA | head date |
|---|---|---|---|
| python-acoustics | `github.com/python-acoustics/python-acoustics` | `99d79206159b822ea2f4e9d27c8b2fbfeb704d38` | 2023-08-20 |
| Friture | `github.com/tlecomte/friture` | `0c6a787a2cc02efd7b6656c74cd2b5e4a5b6e81a` | 2026-08-11 |
| Open Sound Meter | `github.com/psmokotnin/osm` | `1e08de2322f5e8849e1ba39e1d3993187a7cc75c` | 2025-09-13 |
| NoiseCapture | `github.com/Universite-Gustave-Eiffel/NoiseCapture` | `509fb083d8a26a6a38d117c22667267c0ccf9cc3` | 2026-09-04 |
| VSLM | `github.com/muehleisen/VSLM` | `87249988d507a2b36e5f7656d477496305287666` | — |
| esp32-i2s-slm | `github.com/ikostoski/esp32-i2s-slm` | `b643efa1beae51b84d4ad9b2bfa35d439c81e9b6` | 2019-11-22 |
| soundmeter | `github.com/shichao-an/soundmeter` | `89222cd45e6ac24da32a1197d6b4be891d63267d` | — |
| SlmInterface (Larson Davis SDK headers) | `github.com/parsley72/SlmInterface` | `d3efedd7b10c4625cf58f9420ee5a57444661058` | — |
| noise-dosimeter | `github.com/pranav-pjd/noise-dosimeter` | `08ca5026853d27c90c269d004ccbbdf3a969ba61` | — |

### B1.1 python-acoustics — **cannot be used as a reference implementation**

- **Dose: does not exist.** `grep -rniE "\bdose|dosim|61252|exchange"` over the
  repo → 2 hits, both the string `Ambisonic_data_exchange_formats`.
  **Percentiles: do not exist** — `percentile|quantile|histogram` over `*.py`
  → 0 hits. `acoustics/aio.py:17-19` says occupational-noise CSVs are out of
  scope by design.
- Level series: `Signal.levels(time=0.125, method='average')` →
  `iec_61672_1_2013.average()` reshapes `p²` into **non-overlapping rectangular
  blocks of `floor(τ·fs)` samples**, arithmetic mean, `10log10`. The tail is
  dropped and the docstring itself warns of sample drift over an hour.
- **The `method='weighting'` (Fast/Slow) path is broken twice over.**
  `integrate()` calls `zpk2tf([1.0], [1.0, integration_time], [1.0])`, which on
  scipy 1.18.1 / numpy 2.5.2 raises `TypeError: Field elements must be 2- or
  3-tuples, got '1.0'` — `k` must be a scalar, so **time weighting does not run
  at all on current scipy**. Patched, the analog prototype is `b=[1,−1]`,
  `a=[1,−1.125,0.125]`, i.e. `H(s) = 1/(s − τ)` — a **right-half-plane pole**,
  not the "one real pole at −1/τ" the docstring claims; bilinear at 48 kHz puts
  both digital poles outside the unit circle. Because the code also re-runs
  `lfilter` **from zero state on every block** and keeps only the last sample,
  the result degenerates into a rectangular block mean-square with a
  **+0.034 dB** bias (measured: `1.00784789` per block on a unit-mean-square
  1 kHz sine, against a true exponential detector's `0.632, 0.865, 0.950,
  0.982, 0.993` at the same instants).
- **`leq()` carries a dimensional error that a unit test locks in.**
  `descriptors.py:67` computes `10log10(Σ10^(L/10) / time)` with
  `time = N·int_time`, where the textbook form divides by `N`. Measured on the
  package's own fixture: `leq(int_time=1.0) = 79.80698916592375` (right, because
  dividing by 1 is a no-op) but `leq(int_time=1/8) = 88.83788903584319` —
  **exactly +10·log10(8) = +9.03 dB high** — and
  `tests/test_descriptors.py:34-37` asserts that wrong value. `sel()` divides by
  a hardcoded `1.0` and takes no cadence argument.
- Useful anyway for one thing: its **Cirrus log reader** records what a real
  Class-1 instrument's cadences and schema are — `aio.py:100-113` infers the
  interval from the modal timestamp delta with a fix-up for 1/16 s, and the
  fixtures are at **10 ms, 62.5 ms, 250 ms, 500 ms, 1000 ms, 2000 ms**. The
  broadband schema is `"Date","Time","LAFMax","LASMax","LCFMax","LCSMax",
  "LCIMax","LAIMax","LAeq","LCeq","LZPeak","LCPeak","LZeq","LAeqI","LAPeak"`,
  and the third-octave log is `"Date","Time"` plus 36 band columns
  6.3 Hz–20 kHz.

**Consequence for station 3:** do not generate goldens from python-acoustics.
Its time-weighting path does not run on this machine's scipy, its filter
contradicts its own docstring, and its `leq()` is wrong for any
`int_time ≠ 1 s` with a regression lock holding the error in place. This
extends `memory/the-venv-imports-acoustics-only-through-a-shim.md`: the shim
makes the import succeed, and these are the functions that succeed wrongly.

### B1.2 Friture — no logging at all, and a non-IEC detector

- **Dose, percentiles, histograms and logging to file: all absent.** Grep for
  `QFileDialog|\.csv|writerow|QTextStream` over the app → 3 hits, all developer
  tooling. `statisticswidget.py:62-68` prints chunk number and XRUN count only.
  **Friture cannot write a level to disk.**
- `levels.py:56` — `self.response_time = 0.300  # 300ms is a common value for
  VU meters`; 25 ms / 125 ms / 1 s are commented out. **The shipped meter is
  neither Fast nor Slow.** Its coefficient (lines 60-63) is
  `alpha = 1 − (1−w)^(1/(n+1))` with `w = 0.65` — a different convention from
  IEC's `alpha = 1 − exp(−1/(τ·fs))`, which leaves `e⁻¹ = 36.8 %` outside rather
  than 35 %; effective τ ≈ 0.286 s. Smoothing is applied to `y²` then
  `10log10` — the right order. Peak is a separate 25 ms one-pole decay on `|y|`.
  Cadence: `FRAMES_PER_BUFFER = 512` at `SAMPLING_RATE = 48000`, hardcoded →
  **10.67 ms**.
- Octave-spectrum settings are the only place standard constants appear:
  `25 ms (Impulse)`, `125 ms (Fast)`, `300 ms`, `1s (Slow)`, `5s (Very Slow)`,
  **default Slow**.
- **History** (`longlevels.py`): span default 600 s (5…3600), response time
  default 20 s (1…20). The cadence is *derived, not chosen* —
  `Ndec = floor(log2(response_time·fs/100))` "so we end up with 100 points in
  the kernel", giving **one point per 8192 samples ≈ 171 ms** at the defaults.
  Smoothing is a **41-tap causal Gaussian FIR** on decimated `y²` plus a cascade
  of 11-tap Gaussian decimators, chosen over IIR because "IIR ringing produces
  negative values". Stored quantity is **dB, float64**.
- `ringbuffer.py` is worth copying: a `2 × buffer_length` numpy array written
  twice per block (offset and offset+length) so any window read is contiguous
  with no wraparound branch. But it **grows to `int(1.5 × needed)` on demand and
  never shrinks** — at the default cadence its initial 10000 slots is ≈28 min,
  and asking for the 3600 s maximum forces a reallocation to ≈31 600.
- **Alarms: none.**

### B1.3 Open Sound Meter — and its Leq is the cautionary tale

- **Dose: zero** (`dose|dosim|exchange|criterion|61252|OSHA|NIOSH` → one hit,
  `std::exchange`). **Ln: zero.** **Logging to file: does not exist** — every
  export is a frequency-domain snapshot.
- `src/math/meter.{h,cpp}` — a **rectangular sliding-window mean-square in
  `double`**, not an exponential detector: per sample, square, push into a
  circular buffer, add the new and subtract the departing sample with a
  Kahan-style compensated add (lines 52-61) guarding against the integrator
  sticking low. Window sizes `0.125·fs` and `1·fs`. Eight meters run in parallel
  (`{A,B,C,Z} × {Fast,Slow}`) plus a `{Z,Slow}` reference. Weighting is a biquad
  cascade citing **ANSI S1.43-1997**.
- **"SPL" is a constant:** `LevelObject::SPL_OFFSET = 140` added to dBFS, and
  the UI's "94 dB" button simply calls `applyAutoGain(94 − 140)`.
- **The Leq defect.** `src/math/leq.cpp:27-30` `addOneSecondValue(v)` does
  `10^(v/10)` into an `integration_tree`, fed from `MeterPlot::timeReadyRead()`
  on a **1000 ms QTimer** reading `level(curve(), Meter::Time::Fast)`. So OSM's
  Leq is the energy mean of **one instantaneous 125 ms Fast reading sampled once
  per second — roughly 87.5 % of the audio never enters it.** Two further
  readable defects: `value()` always divides by the full window size, so the Leq
  reads low until the window fills; and `Leq()` constructs
  `integration_tree(7200, 12)` while that class allocates only `2^12 = 4096`
  leaves and guards with `if (data[0].size() > m_pos)`, so at the 120 min
  setting **samples 4096…7199 are silently discarded while the divisor stays
  7200**.
- **History:** none in the model; it lives in the renderer as a
  `std::deque<timePoint>` of `{float time; float value;}` — **float dB and a
  float delta-time, no absolute timestamp** — capped at `MAX_HISTORY_SIZE = 750`
  and **cleared whenever curve, time, mode or type changes**.
- **Alarm:** one cosmetic threshold, auto-set to **99 for A/B/K and 135 for
  C/Z**, used only to paint the number red. `grep -rniE "\balarm"` → 0;
  `hysteresis` → 0. Stateless, no duration gate, no latch, nothing logged.
- **Export:** `saveCSV` = `frequency, magnitude, phase(deg), coherence`;
  `saveTXT`, `saveFRD`, `saveCal`, `saveWAV`; session JSON `.osm` autosaved
  every 30 s. **No time column exists in any export.** The only periodic level
  emission is JSON over the network at 1 Hz (`src/remote/server.cpp:91`).

### B1.4 NoiseCapture — the Ln reference

- **Cadence: exactly 1.000 s, hardcoded.** Two chains run from one
  `AudioRecord` loop: 0.125 s (`PROP_FAST_LEQ`, **display only, never stored**)
  and 1.0 s (`PROP_SLOW_LEQ`, the recorded one). `windowTime = 1.0` is a literal
  at `AudioProcess.java:162-163`; none of the 14 settings keys changes it. The
  1 s block is **not** an average of 125 ms sub-windows — the whole block goes
  through the IIR bank once. Overlap is 0 everywhere, so the energetic-merge
  path is dead code and `Window.java:80` still carries
  `@param overlap TODO Fix window overlapping`.
- **History is SQLite, not RAM.** `leq` table: one row per second carrying
  `leq_utc (long ms)`, position fields, and `laeq FLOAT` — **dB(A) as a float**.
  `leq_value` table: 23 rows per second, third-octave 100 Hz–16 kHz,
  **unweighted since v1.3 despite the column comment saying dB(A)**. There is no
  in-memory level ring at all.
- **Ln — binned, then re-expanded and exactly sorted** (`LeqStats.java`):
  sparse `TreeMap<Integer, AtomicInteger>` at **`DEFAULT_CLASS_STEP = 0.1` dB**,
  unbounded range, key `= (int)(leq / classStep)` — **C truncation, so the bin's
  representative value is its lower edge, not its centre**, giving a systematic
  downward bias of up to 0.1 dB. `computeLeqOccurrences()` then **re-expands the
  histogram into a flat `double[]` of length `rmsSumCount`** and hands it to
  Apache Commons Math 3 `Percentile` with no estimation type set (so LEGACY,
  `pos = p(N+1)/100` — library-documented, UNVERIFIED here).
  `la10 = evaluate(90)`, `la50 = evaluate(50)`, `la90 = evaluate(10)`. **Only
  L10/L50/L90 exist.** `CalibrationActivity` reuses the class at a 0.01 dB step
  and reads L50 as its stable level estimate. Dead code worth knowing:
  `AcousticIndicators.medianApprox()` is a 1001-bin histogram with **bin-centre**
  readout and zero callers.
- **Its own server disagrees with its app:** `nc_process.groovy:80` `getLA50()`
  sorts and uses the **energetic** average of the two middle values for even n.
- **Dose: does not exist** (`dosim`, `OSHA`, `NIOSH`, `61252`,
  `criterion level` → 0 each; the 257 `TWA` hits are all "sof**twa**re").
- **Alarms: none.**
- **Export is a ZIP and there is no CSV anywhere** (`grep -rni csv` over
  `app`+`sosfilter` → 0): `meta.properties` (version, build date, device
  manufacturer/product/model, uuid, record_utc, leq_mean, **gain_calibration**,
  **method_calibration**, time_length, tags, **microphone_identifier**,
  microphone_settings) plus `track.geojson` with one Point Feature **per stored
  second** carrying `leq_mean` and 23 band keys. All numerics clamped to
  [0, 150]; NaN → JSON `null`.
- Calibration is a single scalar dB gain with **provenance recorded per
  measurement** (`None, System, ManualSetting, Calibrator, Reference,
  CalibratedSmartPhone, Traffic`) — the one open-source project that records
  *how* it was calibrated, not just by how much.

### B1.5 VSLM — the dose reference, and the design this lane converges on

`Matlab_Source/vslm.m`. The only open-source project found that does **both**
Ln and dose.

- `handles.leq.mindt = 0.1` — statistics are **always** computed from 100 ms
  rectangular non-overlapping mean-square blocks; weighting is applied in the
  time domain per block; `leqdata = 10log10(msq) + 94`. The user's chosen Leq
  length is produced by **averaging the 100 ms mean-squares in the energy
  domain, then converting** — the right order, and exactly the two-rate design
  this record adopts.
- **Ln: exact sort with linear interpolation**, `sx = sort(x); idx = y*N/100;`
  then `interp1`, 1-based. L10…L90 plus one user-settable percentile
  (default 5).
- **Dose, the energy form** (lines 692-699):
  `q = Dex/log10(2); D = sum(dt .* 10.^((L(L>Lt) − Lc)./q)) / (480*60);
  Ltwa = max(10*log10(D)+Lc, 0)`. `D` is a **fraction**, not a percent;
  reference time fixed at `480*60` s; dose refused unless the weighting is A.
  Defaults are **NIOSH 3/85/80**; the OSHA menu sets **5/90/80**.
- Two readable defects: the comment says "above the criterion" while the code
  compares against the **threshold**; and the save handler calls `fprintf(...)`
  **without `fid`** for the dose/TWA/integration-time lines, so **they go to the
  console and never reach the saved file** — only the Ln block and the time
  series are written.
- **It applies an 80 dB threshold to NIOSH as well as OSHA.** Whether that is a
  deviation is itself contested — see §B3.7.

`pranav-pjd/noise-dosimeter` gives the same dose in the **allowed-time form**
OSHA actually uses: `T_allowed = 8·2^((Lc−L)/ER)·3600`,
`dose += (1/T_allowed)·100` per 1 s sample, accumulating **percent**. Config:
1 Hz, NIOSH `{85, 28800 s, 3 dB, threshold 80}`, OSHA
`{90, 28800 s, 5 dB, threshold 80}`. Sub-threshold samples contribute nothing to
dose but still count toward elapsed exposure time and peak. (Small unknown repo;
treat as a formulation example, not authority.)

### B1.6 Larson Davis 831/LxT SDK headers — what a Class-1 instrument stores

`parsley72/SlmInterface`, `Slm/LxT831.h` and `Slm/SlmStructs.h` — the C struct
definitions of a real commercial instrument's measurement record. These settle
the histogram-versus-sort question.

- `LxT831.h:1605-1606`: *"NUM_STAT_BINS is the number of level bins of **0.1 dB
  resolution** used for Ln percentile calculations"*, `#define NUM_STAT_BINS
  2000` → a **200.0 dB span at 0.1 dB**. `NUM_SLM_LN_BINS = NUM_STAT_BINS + 2`
  — **an over bin and an under bin**.
- `SlmStructs.h:317-323` `stSlmLnInfo_t`: `int m_nLnTable[NUM_STAT_BINS+2]`,
  `m_nBaseDB`, `m_nStatus`, `m_nSamples`, the last with the comment *"NOT the
  number of ln bins…num of samples in this sample"*. **The instrument stores a
  histogram of counts, not the samples.**
- `NUM_LNS = 6`, and `SlmStructs.h:629-630` carries both
  `float m_fLnValues[NUM_LNS]` and `float m_fLnPercents[NUM_LNS]` — **six Ln
  values with user-settable percentages** — plus `m_fSpectralLn[NUM_LNS][36]`
  for per-third-octave Ln.
- **`NUM_SLM_DOSES = 2`** — **two dose accumulators running simultaneously**,
  each `stSlmDose1_t = { name, float thresholdLevelDB, BYTE exchangeRate,
  float criterionTimeHours, float criterionLevelDB }`, plus
  `float m_fTWA[NUM_SLM_DOSES]`. That is exactly what makes Smaart's
  `Exposure O` + `Exposure N` columns possible; **a single dose setting cannot
  produce that log.**

### B1.7 Two minor sources with one idea each

**esp32-i2s-slm**: `LEQ_PERIOD 1` second, accumulating `sum_sqr_weighted` in a
`double`. Notable for one thing — it **forces `±INFINITY` into the accumulator
on acoustic overload or below-noise-floor** (lines 394-398), so a corrupted
second poisons the whole Leq deliberately and visibly rather than silently
biasing it.

**soundmeter**: the only alarm state machine in the survey.
`AUDIO_SEGMENT_LENGTH = 0.5` s; `is_triggered()` counts **consecutive** blocks
past the threshold and **resets the counter to zero the instant one block falls
back**, firing at `num` consecutive hits. A minimum-duration gate with **no
hysteresis and no tolerance for a single dropout** — the crudest workable
version, and the only prior art there is.

## B2. Commercial practice (documentation-derived)

### B2.1 Smaart SPL v9.1

`https://downloads.rationalacoustics.com/documentation/smaart-spl/SmaartSPLv9UserGuide.pdf`
(Release 9.1, 40 pp), plus
`https://support.rationalacoustics.com/support/solutions/articles/150000195027-how-to-create-spl-reports-in-smaart`.

**Logging (p. 33) — VERIFIED, and this is the most portable idea in the pass:**

> "When logging is enabled, the values of all configured sound level metrics are
> written to a timestamped logfile every logging interval. **The default logging
> interval is three seconds**… For 'short term' metrics (Peak, SPL Fast and SPL
> Slow), **the highest level reached during the logging interval is recorded so
> that no maximum values are missed**. 'Long term' metrics (Leq and Exposure)
> are logged at their present value."

A logger that samples every metric instantaneously at the interval boundary
discards the loudest moment of every interval — the one moment a complaint will
be about.

**Sample log columns, verbatim from the official `SampleSPLLog.txt` (Smaart SPL
1.1.0.2, Log File Version 6), one row per second:**
`Time, Peak C Max, SPL Fast Max, SPL A Fast Max, SPL C Fast Max, SPL Slow Max,
SPL A Slow Max, SPL C Slow Max, Leq 1, LAeq 1, LCeq 1, Leq 1 C-A, Leq 15,
LAeq 15, LCeq 15, Exposure O, Exposure N, LAeq 10s, LAeq 10, Leq 1 C-A,
dB FS Max, Overloads, Leq Reset`. The integer after `Leq`/`LAeq` is the period
**in minutes**; `LAeq 10s` is the explicit seconds form. **Every SPL column is a
Max over the interval, and every column name states weighting, detector and
reduction** — Smaart never writes a bare "SPL" column. A preamble block carries
operator/company/venue/device/calibration date and per-metric session maxima.
Leq integration period is user-configurable **1 second to 24 hours**; the row
cadence is not documented but the sample file is 1 s.

**Report contents (pp. 37–38), PDF only:** user-editable **Operator, Company,
Venue, Event, Engineer, Production Company, Notes**; per configured metric over
the selected range, **Max, L10, L50, L90** ("L10 = the level that was exceeded
by 10 % of the data points"); for Exposure metrics the bar becomes **Max, Min,
Delta** in percent, Delta being "the sound exposure dose accumulated in the
selected time range"; optional history thumbnails; and per the support article,
**alarm levels, alarm violations, alarm overloads, software version and I/O
information**. **Ln window and method are stated nowhere**, and there are no Ln
columns in the sample log.

**Logfile finalisation (p. 37):** data is held in a temp text file during
logging and "column headers, statistical analysis data and other information are
added" on stop — so **an interrupted session leaves a file Smaart cannot read**.

**Dose:** two models, **Exposure O (OSHA PEL, 90 dBA / 8 h, 5 dB)** and
**Exposure N (NIOSH REL, 85 dBA / 8 h, 3 dB)**. The guide notes a 3 dB
calibration error is a **100 % error in NIOSH sound exposure**. No exchange
rate, criterion level or TWA is named on any page read.

**Calibration — NEGATIVE FINDING, VERIFIED.** A per-input **Cal Offset** exists
("a calibration offset of zero means the input is not calibrated for sound level
measurements") and the calibrator procedure is covered in depth (94/110/114 dB,
±1 dB realistic accuracy goal, IEC 60942 classes) — but **there is no
before/after calibration record in the report or the logfile.** The report
dialog's field list is exhaustive and contains no calibration entry.

**Overload:** "(three consecutive samples at 0 dBFS)", after which "all
averaging buffers will be flushed to remove the undefined values."

**Alarms (pp. 32, 34) — VERIFIED, explicitly single-threshold:**

> "Whenever a metric's logged value exceeds the corresponding alarm threshold…
> an orange lollipop will indicate that the alarm has been triggered. **The
> alarm state will persist until the metric's measured value drops below the
> alarm threshold.**"

Same number up and down. **No hysteresis.** The Alarms tab's fields are `Use`,
`Input`, `Type`, **`Level (dB)`** and **`Duration`** — and `Duration` is *not* a
debounce: it "sets the amount of time, in seconds, that the associated meter
will **flash a visual warning** when the alarm is triggered". **There is no
minimum-duration parameter in Smaart SPL at all.** The traffic light is
"customize the Red, Yellow and Green color thresholds… independently or all at
once" — **entirely user-defined, with no published default.**

**Webviewer (pp. 38–40)** — the closest precedent for G7's web half:

> "Smaart SPL allows realtime remote viewing of the SPL history timeline data,
> meters, and alarms via a web browser on a computer or mobile device that
> shares a network connection with the computer running Smaart SPL."

The app serves the page itself, no separate client (worked example
`192.168.1.117:26000`; 26000 is an *example*, not a documented default).
**Bind: LAN**, multiple adapters. **Auth: optional password**, therefore off by
default. **Gated on logging:** "Logging must be running for the API to be
enabled." Fan-out both ways — many clients to one instance, one client to many
instances "to view levels on multiple stages at a music festival". Operational
note: "We recommend using your web browser's private browsing / incognito mode,
as it disables scripts and plugins that can interfere with the API's
functioning."

### B2.2 10EaZy v2.5

`https://www.spektrum-online.net/downloads/files/10EaZy%20User%20Guide.pdf`
(the official `10eazy.com/s/10EaZy-User-Manual.pdf` v2.7 now **404s**; mirror
used).

**Log (p. 28): data is stored to file once per minute**, carrying LAeq **and**
LCeq for 1-minute periods, Leq for the user-selected period **and** a secondary
period, max **FAST dB(A)**, max **SLOW dB(A)**, max **PEAK dB(C)**,
**Leq C − Leq A**, an audience-noise estimate explicitly flagged ("These two
values are not IEC standards and are only included for statistical reasons"),
and the user information from the start-up screen. Spreadsheet-compatible.

**Tamper evidence — the standout feature (p. 28):**

> "When a measurement is stopped, 10EaZy writes a checksum to the bottom of the
> log file… an additional application called LFV, short hand for 'Log File
> Validator'… In case someone altered the data, maybe because they didn't like
> the results showing they where too loud, the file will fail the check."

The checksum covers file content only, so the file can be moved or renamed. For
a compliance artefact this matters more than any extra metric.

**Limits (p. 8):** the main limit is **LAeq** over a user-set period with a
**minimum of 3 minutes**; an optional **LCeq** limit; and a **secondary Leq
period** for jurisdictions where breaching the main limit triggers a second
limit. German **DIN 15905-5** systems are locked to **30 minute / 99 dBA**.

**Leq is a sliding window of 1-minute blocks (p. 19):** "Every green dot
represents a 1-minute average value… If the Leq period is set to 30 minutes…
there will be 30 dots on the graph." Before the window fills it degrades
gracefully: "From program start until a full Leq period has passed, the Leq
value will be a running average based on the time expired since program start."

**Calibration (p. 30) — this is where the before/after question is answered:**

> "In order to comply with some standards the system needs to be calibrated at
> every program run. **Additionally some standards require that the calibration
> needs to be checked again after a measurement.**"

and

> "10EaZy will recalibrate and store the new calibration value **+ the date of
> calibration**… Calibration will fail if input is not a 1 kHz tone or if the
> calibration value differs **more than ± 1.5 dB from the factory
> calibration**."

Calibrator: IEC 60942 class 1 or 2, 1 kHz at 94 or 114 dB, ≥30 s settle.
**±1.5 dB against factory is a published hard number** — and it is the shape of
check only a *flow* can perform, not a typed offset.

**Overload:** "According to IEC standards the program needs to be reset if an
input peak occurs." Measurement continues but strict IEC 61672 compliance
requires a restart; an entry goes to a separate **event log** file
`<logname>_eventlog`.

**MAM traffic light (p. 21) — mechanism published, margins not:**

> "The MAM takes a short term average, **approx. 30 seconds**, and compares it
> to the average stated in the dB limit. If the short-term average is louder
> than the limit, the **red** lights will illuminate, if it is below the limit
> average, the **green** light will illuminate, **indicating how far the current
> average is from the limit average**."

So the green/red boundary sits at **zero margin** and the lights are a *signed
deviation meter*, not graded bands. The amber stage is separate (p. 23): a
**prediction algorithm** issues a warning when "the average level is approaching
the dB limit" — **no dB margin, no time constant and no description of the
algorithm is published anywhere**, only the qualitative trigger and an
availability constraint (warnings need one full Leq period; "the MAM provides
reliable results 10 seconds after program start").

**WebViewer / MobileViewer (pp. 25–26):** two **static pages served by the app**
at fixed filenames `http://<local IP>/10EaZy_webviewer.html` and
`…/10EaZy_mobileviewer.html`; **port 80 by default** ("the same port that normal
web traffic uses"), changeable only by hand-editing `niwebserver.conf`;
**no auth documented anywhere**; and the manual **walks the user through port
forwarding to the public internet** so "an environmental officer in a remote
location" can watch. **Values update every 5 seconds "to save on bandwidth."**
Implementation is a bundled National Instruments webserver plus `cwdss.exe`.
This is the security anti-pattern, and it is the thing to point at when
justifying localhost-by-default.

### B2.3 REW

`https://www.roomeqwizard.com/help/help_en-GB/html/splmeter.html` (the URL in
the brief 404s; there is no separate SPL-logger page) and
`https://www.roomeqwizard.com/help/help_en-GB/html/api.html`.

- **Cadence is not user-settable and is not a round number.** The worked
  example's own header reads `Log interval: 0.18575963718820862 seconds` — a
  consequence of the audio block size — with `Log length: 7061 entries over
  1311.6s`. Files grow ~18 kB/min. *This is precisely the failure mode a
  sample-count clock prevents.*
- **Log columns, verbatim:** `Time[s], LZS, LZSMin, LZSMax, LZpeak, LZeq, LZE,
  LZeq1m, LZeq10m` plus a trailing time-of-day field (`Z` = whichever weighting
  is selected). **Rolling 1-minute and 10-minute Leq are logged alongside the
  running one.**
- **Ln: not mentioned anywhere. Dose: not mentioned.**
- **Thresholds:** "Meter limits" with independent limits for SPL, Leq and SEL
  and a green/amber/red background. **No hysteresis, no minimum duration.**
- Export: plain **text**, delimiter set from REW's File menu; optional auto-log
  to `SPL-YYYY-MMM-dd.txt` with a new file at midnight. **No CSV-specific or PDF
  export described.**
- Weightings A/C/Z; **Fast or Slow only, no Impulse**; an ~8 Hz high-pass
  toggle. Peak = largest absolute sample in each audio block.
- **API:** port **4735** default, `-port` overridable (minimum 1024). *"REW has
  an API accessible over http at localhost (127.0.0.1)"* and *"It cannot be
  accessed outside the machine REW is running on."* **No auth documented.**
  Browser UI is **swagger-ui only** — the API's own doc explorer, with the
  OpenAPI spec at `/doc.json` — **not an application UI**. And *"All API GET
  methods are available whenever the API is running… **PUT and POST are also
  supported by default for most endpoints**"*, with *"N.B. There are no delete
  confirmations, use with care!"*. Push is available via subscriptions.

### B2.4 Regulations that are free to read

**Flanders / VLAREM II art. 5.32.2.2bis — VERIFIED (primary legal text)**,
`https://www.kruisem.be/sites/default/files/2021-02/geluid_5.32_0.pdf`:

- **Cat. 2** (>85 to ≤95 dB(A) LAeq,15min): must **continuously measure** either
  LAeq,15min or LAmax,slow. **Measurement only — registration not required.**
- **Cat. 3** (>95 dB(A) LAeq,15min): limit **LAeq,60min ≤ 100 dB(A)**; must
  **continuously measure AND register LAeq,60min**; the level must be
  "continu zichtbaar voor en wordt continu bewaakt door de exploitant".
- **The deemed-compliance rule:** *"Als het maximale geluidsniveau, gemeten als
  **LAeq,15min 102 dB(A)** niet overschreden wordt, wordt geacht hieraan te zijn
  voldaan."* — **+2 dB for a 4× shorter integration, legally binding.**
- **Retention: "ten minste een maand"** (at least one month).
- **>100 dB(A) LAeq,60min is forbidden** (§3). The obligation is waived if a
  conforming **geluidsbegrenzer** (limiter) is used.
- Equipment class NBN EN 60651 class 2 — UNVERIFIED (the bijlage text was not
  in the PDF read).
- **No calibration-record requirement in the article text** — NEGATIVE FINDING,
  VERIFIED for this article.

**UK Noise Council 1995 "Pop Code" — VERIFIED (full text read)**,
`https://www.telford.gov.uk/media/qdhbupnh/noise_council_code_of_practice_on_environmental_noise.pdf`:

- **MNL = LAeq of the music over a 15-minute period**, 1 m from the façade of
  noise-sensitive premises, 0900–2300.
- Table 1 (cl. 3.2): 1–3 concert days/yr — **75 dB(A) over 15 min** at urban
  stadia/arenas, **65 dB(A) over 15 min** elsewhere; 4–12 days — **background
  + 15 dB(A) over 15 min**.
- Equipment (cl. 3.8): "integrating-averaging sound level meter complying with
  **type 2 or better of BS6698**"; **time weighting F**.
- **Clause 4.12 — the early-warning rule:** *"Although the limit value… would be
  in terms of 15 minute LAeq, useful control can be exercised by monitoring the
  LAeq over one minute periods. This enables an **early warning** to be obtained
  of possible breaches in the 15 minute limit. It is sometimes appropriate to
  set an additional control limit in terms of the one minute LAeq (**typically
  some 2-3 dB(A) above the 15 minute value**)…"* Note the hedge — "sometimes
  appropriate", "typically". Guidance, not specification.
- Appendix III model condition 7.0: the noise consultant "shall **continually
  monitor** noise levels at the sound mixer position… The Licensing Authority
  shall have **access to the results of the noise monitoring at any time**."
- **No calibration record required — NEGATIVE FINDING, VERIFIED.** The only use
  of "calibrat" in the document is cl. 4.8, where a pre-event sound test
  "effectively **calibrates the system**" — i.e. establishes the mixer-position
  control limit, not an acoustic calibrator check.

**Switzerland V-NISSG — UNVERIFIED.** The federal enforcement aid
(`bag.admin.ch/.../slv-vollzugshilfe.pdf`) returned HTTP 502 and fedlex served
only a language shell. From cantonal summaries
(`https://www.zh.ch/de/umwelt-tiere/laerm-schall/schall-laser.html`,
`https://www.aarau.ch/public/upload/assets/1888/2017-10-Zusammenfassung_SLV.pdf`):
limits **93 / 96 / 100 dB LAeq,1h** with short-term peaks never exceeding
**125 dB LAF,max**; category D requires the level to be **electronically
recorded, LAeq,5min at least every five minutes**, kept **six months**. And one
genuinely useful reporting input: **the level difference between the measurement
position and the loudest audience position must be determined and documented
before the event using a broadband signal (usually pink noise)** — which is
exactly what 10EaZy's "Compensation Setup" (p. 10, VERIFIED) does.

**Cirrus Research AuditStore — VERIFIED**,
`https://cirrusresearch.com/auditstore-data-verification-on-the-optimus-sound-level-meters/`:
each measurement writes a block into separate secure memory holding "the time,
date and duration, the LAeq, Peak(C) and LAFmax, LA10 & LA90 (where available)
and the overload indication", and **"In addition to the noise measurement data,
information about the last calibration is also stored."** NoiseTools then
verifies the database against that secure memory. Whether the calibration record
reaches the *printed report* is not documented — UNVERIFIED.

**NTi XL2 — UNVERIFIED.** `https://www.nti-audio.com/en/support/know-how/did-you-know-that-the-xl2-provides-report-configuration-after-your-measurement`
confirms only that the report is re-definable post-measurement and can carry
**LAeq, LAFmax, LAFmin, LCPKmax**. The 240-page manual was not read.

## B3. Where they disagree — this is where the decisions are

### B3.1 What time-weighting feeds the history, and whether it is even a detector

| | detector shape | τ | history cadence |
|---|---|---|---|
| python-acoustics | rectangular, **non-overlapping** | 0.125 default | = τ |
| Friture (meter) | exponential, `w = 0.65` convention | **0.300 s** | 10.67 ms |
| Friture (long history) | **41-tap Gaussian FIR** on decimated `p²` | ~20 s | ~171 ms |
| Open Sound Meter | rectangular, **sliding** (per sample) | 0.125 / 1.0 | 80 ms RMS, 1 s Leq |
| NoiseCapture | rectangular, whole 1 s block, one IIR pass | 1.0 | 1 s |
| VSLM | rectangular, non-overlapping | 0.100 | 100 ms → user-combined |
| REW | true Fast/Slow | 0.125 / 1.0 | ~0.186 s (block-driven) |
| Smaart | Fast/Slow, logged as **Max over interval** | 0.125 / 1.0 | ~1 s |
| **this repo** | **exponential one-pole on the mean square, closed-form tested** | 0.125 / 1.0 | (none yet) |

**Not one of the six code-read projects ships a correct IEC 61672 exponential
detector.** python-acoustics has one in its docstring and a right-half-plane
pole in its code; Friture ships one at 300 ms with a non-IEC alpha convention;
OSM, NoiseCapture and VSLM all deliberately use rectangular windows. **This repo
already has the thing nobody else has**, pinned by closed forms in
`core/tests/test_detector.cpp`. There is also no reference implementation to
generate a golden from — the closed forms are the reference.

Second-order split: **sliding vs block**. OSM slides per sample (no transient
aliasing, at 48 000 buffer writes/s/meter × 8 meters); everyone else advances a
block at a time (cheap, but a transient across a boundary is split between two
readings).

### B3.2 dB or energy in the store

**Every single project stores dB** — Friture float64 dB, OSM float dB
(already `+140` in SPL mode), NoiseCapture `laeq FLOAT` in SQLite, VSLM a dB
array, and every text log. **Nobody stores mean-square.** Yet every one of them
converts **back** to energy to compute anything (OSM in `addOneSecondValue`,
NoiseCapture in `rmsSum`, VSLM in the dose sum). That log→store→un-log round
trip is pure loss, and the reason nobody avoids it appears to be that the file
format *is* the store.

Related: OSM stores a **float delta-time** per history point and therefore
cannot survive a pause or a clock change; NoiseCapture stores absolute
`leq_utc` in ms; REW stores both elapsed seconds and time-of-day.

### B3.3 Ln — three methods and four index conventions

- **NoiseCapture**: 0.1 dB sparse histogram keyed on the **floor**, then
  **re-expanded to a flat array** and sorted. O(N) at read time, 0.1 dB
  downward bias.
- **Larson Davis 831/LxT** (real Class-1 hardware): **2000 fixed 0.1 dB bins,
  counts only, plus over and under bins**, six user-settable percentages, and
  per-third-octave Ln. Constant memory, no re-expansion.
- **VSLM**: exact sort of the full 100 ms series.
- **OSM, Friture, python-acoustics**: no Ln at all.
- **NoiseCapture's own server disagrees with its app**: exact sort, and the
  **energetic** mean of the two middle values for even n.

And four different index conventions for the same nominal quantity:

| convention | rule | who |
|---|---|---|
| numpy `linear` (0-based) | `idx = (N−1)·p/100` | **this repo's existing convention** |
| 1-based proportional | `idx = p·N/100` | VSLM |
| Commons Math LEGACY | `pos = p(N+1)/100` | NoiseCapture (by default, UNVERIFIED) |
| MATLAB `prctile` | `p·N/100 + 0.5` | (VSLM deliberately does not use it) |

At N = 1000 these differ by a fraction of a sample; over a 30 s window they
differ by a whole order statistic. **"L90" is not a well-defined number until
the rule is named** — which is exactly why ISO 1996-1 clause 3.1.3's own
notation carries the weighting, the detector, N and the interval.

Third split: **what feeds the percentile.** NoiseCapture percentiles 1-second
LAeq; VSLM percentiles 100 ms Leq; Larson Davis histograms the running
Fast/Slow SPL — **which is what this repo's `Leq` already does.** The same room
gives materially different L90s under the three, because short quiet gaps
survive a 100 ms average and not a 1 s one.

### B3.4 Logging cadence

Fixed and hardcoded (NoiseCapture 1 s, esp32 1 s), derived from something else
(Friture's ~171 ms falls out of a decimation target; **REW's 0.18575963718820862 s
falls out of the audio block size**), or configurable (Cirrus offers
10/62.5/250/500/1000/2000 ms; Smaart's Leq period is 1 s–24 h with a 1 s row
cadence; VSLM always computes statistics at 100 ms underneath).

**VSLM's split is the one to steal**: compute statistics at a fine fixed cadence
and derive the user's logging cadence by energy-averaging up from it. That keeps
Ln resolution independent of the log interval, which none of the others manage.

### B3.5 Leq — what actually enters the average

OSM: one 125 ms Fast sample per second (**87.5 % of the audio discarded**).
NoiseCapture: every sample, one IIR pass per 1 s block. VSLM: every sample, via
100 ms mean-squares. python-acoustics: every sample, with a `−10log10(int_time)`
scale error for any `int_time ≠ 1`. Smaart: **Max over the interval** for the
SPL columns and a true Leq for the Leq columns — a distinction its column names
make explicit and nobody else's do.

### B3.6 Alarms — three shapes, none with hysteresis

- OSM and REW: **stateless recolour**, nothing latched, nothing logged.
- soundmeter: **N consecutive blocks**, counter reset by one block below.
- Smaart: documented single-threshold, `Duration` = post-fire flash time, and a
  user-defined traffic light with no published default.
- 10EaZy: a **signed deviation meter** at zero margin, plus an undocumented
  prediction algorithm for amber.
- Friture, python-acoustics, NoiseCapture: nothing at all.

**Not one project in this survey implements hysteresis, and no source publishes
a debounce or an amber margin.** The field's actual practice is to make the
compared quantity long enough that flicker disappears — Smaart compares a
logged, integrated value at a 3 s interval; 10EaZy compares an Leq over at least
3 minutes. **Integration is doing the work hysteresis would otherwise do.**

The two published numbers that look like amber margins are not: Pop Code cl. 4.12
("typically some 2-3 dB(A) above the 15 minute value") and VLAREM's
`LAeq,15min ≤ 102` against `LAeq,60min ≤ 100`. Both are limits on a
**shorter-integration proxy quantity**, not warning colours. Two independent
jurisdictions converging on +2..3 dB is the strongest external evidence in the
pass — and reusing it as "amber comes on 2 dB below the limit" would be exactly
the category error `memory/a-threshold-read-off-a-grid-is-that-grids-floor.md`
exists to prevent.

### B3.7 Dose — two formulations, and a contested threshold

**Energy form** (VSLM, IEC Formula (7)):
`D = Σ dt·10^((L−L_c)/q) / T_ref`.
**Allowed-time form** (noise-dosimeter, and how OSHA writes it):
`D = 100 × Σ Cᵢ/Tᵢ` with `Tᵢ = 8·2^((L_c−L)/ER)`.
Algebraically identical; they differ in units (fraction vs percent) and in how
naturally they extend to a projected dose.

**Larson Davis runs two accumulators at once** with independent threshold /
exchange rate / criterion time / criterion level — which is what makes
`Exposure O` + `Exposure N` possible, and what a single dose setting cannot do.

**The threshold is the least standardised parameter in the whole feature**, and
the sources genuinely disagree:

- **NIOSH**: cdc.gov's "Recommended Dosimeter Settings" page states **Threshold
  Level 80 dBA** and §1.3.3 says levels "from 80 to 140 dBA shall be
  integrated" — so 80 dBA is VERIFIED as NIOSH's own recommendation. (The
  contrary claim that "NIOSH specifies no threshold" appears in the code survey
  and is not supported by the CDC page.)
- **OSHA**: 80 dBA for hearing-conservation dose, **90 dBA for PEL-compliance
  dose** — two thresholds in one regulation.
- **ANSI S1.25 cl. 5.1** only *"strongly recommends"* the threshold be ≥5 dB
  below the criterion level, and cl. 3.15 permits declared values of 90, 80 or
  Variable.
- **IEC 61252 Ed 2 defines no threshold at all.**

So the threshold must be a **per-preset field**, as Larson Davis structures it,
never a constant.

### B3.8 Export — CSV is the minority format

Tab-or-configurable-delimited **`.txt`**: REW, Smaart, VSLM. **CSV**: only OSM
(frequency-domain snapshots with no time column) and Cirrus as a source format.
**GeoJSON in a ZIP**: NoiseCapture, with no CSV anywhere. **PDF**: Smaart only.
**Nothing at all**: Friture, python-acoustics.

Every text-log format is **one header block plus one wide row per interval** —
never a long/tidy `timestamp,metric,value` shape. Cirrus, REW and Smaart arrived
at that independently, so it is what users' existing tooling expects.

---

# Part C — what this repo already gives L6a, and where the seams are

*Read on HEAD `6d9a53d`, 2026-09-16. Every claim names the file and was read,
not remembered; the arithmetic is shown where it is arithmetic.*

## C1. The dependency is satisfied in `core/` and is unreached everywhere else

The master plan's L6a row says the lane "waits on the Meters track", and report
002 lists `Weighting A/C/Z`, `Detector`, `Leq/SEL/Lpeak/Ln` as landed. Both are
true. What neither says is that **nothing outside `core/` has ever called any of
them.** Measured:

```
grep -rn "rta::meter\|rta/meter" --include=*.cpp --include=*.h --include=*.txt --include=*.cmake .
```

returns **16 lines, every one of them under `core/`**: the two headers, the two
sources, one guard script and the two core test files. `WeightingType` appears
in exactly three files, all `core/`. There is no hit in `app/`, `platform/` or
`ui/`.

So the L6a situation is the one L5c found and absorbed on the owner's ruling —
"`app/` had never called L2's dual-FFT engine" — repeated for the meters. The
lane is not "logging on top of a working SPL meter". It is **building the SPL
meter into the application, and then logging it.** Any plan that budgets only
for history/alarms/export is short by the whole wiring wave.

Evidence that the app carries no SPL today: `app/src/measure/Snapshot.h` has
`bands`, `spectrumDb`, `transfer`, `mtw`, `referenceBands`, `peakBandLevelDb`,
`framesAnalysed`, `droppedSamples`, `average`, `positions` — and no broadband
level, no Leq, no weighting selector, no detector state.

## C2. Two dB conventions meet at that seam, 3.0103 dB apart

`app/src/measure/Levels.h` defines the app's single meaning of "dB":

```
levelDbFs(power) = 10*log10(power) + kFullScaleSineOffsetDb,  kFullScaleSineOffsetDb = 3.0102999566398120
```

so a full-scale **sine** reads exactly `0.0 dBFS`. Its own comment calls that
"the ONE definition of dB the rest of the app reads through".

`core/include/rta/meter/Leq.h` defines Leq as `10*log10(mean(p^2)) +
referenceOffsetDb` — the IEC definition, mean-square referenced, no sine offset.
A full-scale sine therefore reads `10*log10(0.5) = -3.0103` there.

Both are correct; they answer different questions (an RMS level versus a
peak-equivalent reference). But put the broadband Leq on screen beside the RTA
bands with no conversion and a 1 kHz tone reads **3.0103 dB lower on one readout
than the other**, which an operator reads as a fault. This is the same class of
trap as the PS-vs-PSD 1.76 dB one report 002 records as already pinned, and the
same class of fix as `Snapshot.h`'s phase note ("degrees, not radians … the
conversion happens exactly once, at that one seam, so no file downstream has to
know which unit it is holding").

Note what does *not* break: once a calibration offset is derived through the
`Leq` path itself, calibrated dB SPL is right either way, because the offset
absorbs whatever constant the path carries. The mismatch bites only the
**uncalibrated dBFS** readouts, which is exactly what an operator sees before
they calibrate.

## C3. `Leq`'s history is unbounded, and its percentile sorts the world twice

`core/src/meter/Leq.cpp`, read in full:

- `Leq::process()` is `noexcept` and calls `history_.push_back(...)` once every
  `historyIntervalSamples()` samples. `historyIntervalSamples_ =
  lround(0.1 * sampleRate)` — 4800 at 48 kHz, so 10 entries per second. The
  vector has no cap and `reset()` is the only thing that clears it. An
  allocation failure inside a `noexcept` function is `std::terminate`.
- 8 h of show = 28 800 s × 10 = **288 000 doubles = 2 304 000 B = 2.1973 MiB**,
  growing, plus the transient double-size during reallocation. A permanently
  installed monitor grows without bound.
- `Leq::percentileDb(n)` copies the history into a `std::vector<double>`, sorts
  it, and then calls `percentileLevelDb(sorted, n)` — **which copies and sorts
  it again**. Two allocations and two `std::sort`s of the whole history per
  call. Reading L10, L50 and L90 once is six sorts of 288 000 doubles and
  ~13.2 MiB of allocation churn; doing that at the UI's cadence is not a
  micro-optimisation question, it is why dosimeters have used histograms since
  they were made of discrete logic (§B1.6).

None of this is a defect in what the meters track shipped — nothing was ever
asked to run for eight hours. It is the exact list of things L6a has to change,
and it is why the history/percentile decisions belong in `core/` as new types
rather than as an app-side wrapper around `Leq`.

## C4. The publish path is a UI cadence, not a metering clock

`app/src/measure/AnalysisThread.cpp:21` — `kMinPublishIntervalMs = 50`, applied
in `publishIfDue()` against `juce::Time::getMillisecondCounter()`. Snapshots are
therefore published *at most* 20 times a second, whenever a drain happened to
finish and the wall clock had moved on. The interval is a floor, not a period,
and it is wall-clock-derived.

A log sampled off that path would have jittered, wall-clock-dated rows whose
integration times do not tile the session — REW's
`Log interval: 0.18575963718820862 seconds` (§B2.3) is what that looks like when
it ships. The log must be driven by **sample counts on the drain path**, with
each block's duration reported in samples and seconds derived from it — the same
discipline `docs/dsp/2026-09-05-mtw-l3.md` adopted when it reported integration
seconds per band instead of specifying them.

There is a second hazard on the same path. `pairedHopCount()`
(`app/src/measure/PairedDrain.h`) advances the measurement ring only when the
*reference* ring can advance with it. A meter hung off the paired drain stops
logging the moment the reference channel stops — which is most of a show. The
SPL meter must sit on the **measurement channel's own drain**, so that a session
with no reference still logs.

## C5. What already exists that L6a should reuse rather than reinvent

| Need | What is already there | File |
|---|---|---|
| overload marking per block | `rta::dsp::hasOverload(span, runLength=3, threshold=1-2^-15)`, pure, stateless, with the "a run does not carry across calls" contract and the app-side latch precedent | `core/include/rta/dsp/OverloadDetector.h` |
| A/C/Z weighting on the sample stream | `rta::dsp::Weighting::process(span<const float>, span<float>)`, plus `analyticDb()` as the closed form to test against | `core/include/rta/dsp/Weighting.h` |
| F/S/I ballistics | `rta::meter::Detector`, exponential mean square, closed-form step and decay — **the thing no open-source project in §B3.1 has** | `core/include/rta/meter/Detector.h` |
| calibration offset as trace metadata | `Trace::calibrationOffsetDb`, `Trace::calibrationUnit` (`DbFs`/`DbSpl`), already round-tripped by `SessionCodec` and already refusing an unrecognised unit rather than defaulting to dBFS | `app/src/trace/Trace.h`, `app/src/trace/SessionDecode.cpp:109` |
| crash-tolerant session persistence | `SessionStore` — a session is a *folder*, index written to `.tmp` then renamed | `app/src/trace/SessionStore.h` |
| a pure formatter + a thin writer | `EqTextExport.h` / `FirExport.h` formats in a JUCE-free header, `FirTextWriter.cpp` / `FirWavWriter.cpp` touches disk | `app/src/export/` |
| a place to hang an SPL history pane | `PaneView { Rta, Transfer }` with `resolvePaneView()` already reporting `fellBack` for an unknown name | `app/src/view/PaneRegistry.h` |
| "absence, not a placeholder" | `TransferBlock::coherence` is `std::optional`, never a vector of 1.0; L6b's two named absence reasons | `app/src/measure/Snapshot.h`, `memory/a-placeholder-for-an-absent-result-erases-its-state.md` |
| a TCP listener with a bind address | `juce::StreamingSocket::createListener(port, localHostName)`, already linked through `juce_audio_utils` | pinned JUCE 9.0.1 checkout, `modules/juce_core/network/juce_Socket.h:238` |

**What does not exist:** any calibration *flow* (there is a stored offset and no
routine that derives it from a calibrator), any broadband SPL readout, any
history buffer, any alarm, any dose, any log file, any report, any network code.
**And JUCE 9.0.1 has no PDF writer** — the only `pdf` matches under
`juce_graphics` are incidental strings in vendored libraries.

## C6. Two guards L6a must plan around, one of which does not cover the risk

`core_makes_no_class_1_claim` (`core/tests/check_no_conformance_claim.cmake`)
fails if `core/include/rta/meter/*.h`, `core/src/meter/*.cpp`,
`dsp/Weighting.*` or the three named test files match
`[Cc][Ll][Aa][Ss][Ss][ \t_-]*[01]`. New `core/meter` files are picked up
automatically by that glob — good.

**But the glob stops at `core/`.** The one artefact in this whole lane that a
regulator or a venue would actually read — the generated report — is written by
`app/`, and that is precisely where the words "IEC 61672-1 Class 1" would be
most tempting and most damaging. The guard as written would not see it. Either
the guard's scope grows to cover the report templates, or the honesty rule that
the weighting record established stops being enforced exactly where it matters
most.

`measure_has_no_framework_deps` (`app/tests/CMakeLists.txt:274`) is an explicit
semicolon-separated list of ~65 files, not a directory glob, and
`memory/core-must-not-include-frameworks.md` records that a *misspelled* path in
such a list silently reduces coverage rather than failing. Every JUCE-free file
L6a adds under `app/src/measure`, `app/src/trace` or `app/src/export` has to be
added to that list by hand, and the count in its own output (`OK (N files
scanned)`) is the only proof it was.

---

# Part D — the web viewer question

## D1. The owner already ruled, and the ruling survives contact with the evidence

`docs/HUMAN-QA-QUEUE.md:68` and `docs/UPGRADE-BACKLOG.md` record the
2026-09-06 ruling: the remote API **binds localhost by default** (attributed to
the REW model) and ships **read-only**, with a write surface and a
LAN-bind-plus-password opt-in deferred to a later version. L6b §10 adds that it
lives in a new sibling of `platform/`, consumes `SnapshotSource` rather than
`Analyser`, and reuses the `key=value` line convention before any JSON
dependency.

What station 1 adds:

- **REW's API is writable** (§B2.3): "PUT and POST are also supported by default
  for most endpoints", "there are no delete confirmations". The *bind* is REW's;
  the read-only posture is this project's own and is stricter.
- **Every surveyed transport differs and none is a consensus.** REW: localhost,
  no auth, writable, swagger-ui only. Smaart SPL: LAN, optional password,
  view-only, **gated on logging running**. 10EaZy: LAN on **port 80**, no auth,
  and the manual documents **port-forwarding to the open internet**. OSM: UDP
  multicast across the subnet, no auth, and **"For remote sources you can change
  all settings but audio"** — writable by design.
- **Both live-sound precedents serve the page from the app itself**; neither
  requires a separate installed client. That is the right call for a compliance
  viewer — one that needs an app on the tour manager's phone will not get used.
- **10EaZy's 5 s update** ("to save on bandwidth") is visibly sluggish for a
  live number, and it is a consequence of re-sending the whole history every
  tick. Splitting the snapshot from the history strip removes that pressure.

## D2. The minimum payload, and the two fields a naive implementation drops

```
snapshot   { t_iso, blockIndex,
             metrics[ {id, label, weighting, detector, window, valueDb} ],
             leqBufferFill 0..1, overload }
history    per metric: [ {blockIndex, valueDb} ]  +  markers[ {blockIndex, kind, text} ]
alarms     [ {metricId, limitDb, window, state, sinceBlock, headroomDb} ]
```

- `metrics[]` is a **flat list, not a fixed LAeq/LCeq shape**. Smaart and
  10EaZy both let the user define metrics, and a hardcoded schema cannot express
  "LCeq secondary limit" or a custom Leq period.
- **`leqBufferFill` is load-bearing.** Smaart draws it as a bar that is yellow
  while the Leq buffer is partially full and green when it is full; 10EaZy will
  not issue warnings at all until a full Leq period has elapsed. A live Leq
  shown without saying its window is not yet full is a number that is quietly
  wrong.
- **`state` must be server-computed.** The page must never re-derive an alarm by
  comparing `valueDb` to `limitDb`, or the decision lives in two places and the
  page and the log will eventually disagree.
- **Markers are Smaart's taxonomy** — `alarm`, `overload`, `note`, `reset` —
  and are worth copying wholesale.
- History is a **separate endpoint** from the snapshot: cheap to fetch once and
  append to, which is what 10EaZy's 5 s cadence is paying for.

---

## D3. What PR #11 settles, and the one thing it hands back

`docs/dsp/2026-09-16-remote-api.md` §12 — read at `5b62218`, merged at
`a39a02e` — is titled "The SPL
web viewer (lane L6a, G7) rides THIS surface" and states the rule "so that a
parallel lane cannot quietly open a second listener": G7 is **a client of that
API, served from that server, on that port, behind that `Host` check, rate limit
and token**, and "must not open a second socket, a second port, a second bind
default or a second auth model."

Settled there, and not re-opened here:

- **Transport:** HTTP/1.1 + JSON over TCP, library **cpp-httplib (MIT)**;
  polling with a version token in v1, no push; one API thread reading the same
  published pointer the UI reads; GET only, versioned in the path; a
  `Host`-header allowlist that outranks the bind.
- **Precedent:** Smaart serves its SPL Web Viewer as plain HTTP on the **same
  port 26000** as its API with the same optional password — one surface, two
  representations. SysTune's answer to the same problem was to bundle a whole
  NGINX, which is the upper bound on getting this wrong.
- **Static assets ride the same server**, so the page is same-origin with the
  API it fetches, needs no CORS headers, and passes the `Host` check by
  construction.
- **Rounding is the viewer's job** and must match the desktop UI exactly —
  whole hertz, one decimal of dB, two decimals of coherence — because the wire
  carries full precision.

**It reached §C1 independently, from the other side.** Its §12.1: "`Snapshot`
carries dBFS; `rta::meter::Leq` has no `app/` caller. The `"spl"` entry in
`/api/v1/status`'s `available` list appears when the Meters track lands it, and
**L6a is blocked on that, not on this record**." Two research passes, run in
parallel and not sharing notes, found the same gap — which is about as close to
corroboration as a single-repository fact gets. It also fixes the build order:
**the SPL publish path is this lane's first wave and the web viewer is its
last.**

**The one thing handed back to L6a** (its §12.4): whether a page served *from*
`127.0.0.1` fetching `127.0.0.1` is exempt from Chrome's Local Network Access
prompt. It follows from LNA's same-address-space model and was **not found
stated verbatim** — its own station-1 UNVERIFIED ledger item 7. It is an
afternoon's test against Chrome 142+, it is a precondition for the viewer and
not for the API, and it is UNVERIFIED in this lane's ledger too.

---

# Part E — decision → evidence → rejected alternative

| # | Decision (record §) | Evidence it rests on | Alternative rejected, and its cost |
|---|---|---|---|
| 1 | Log unit is a **block** with a **sample-count clock** (§2) | `kMinPublishIntervalMs = 50` is a wall-clock floor (§C4); REW's `0.18575963718820862 s` interval is what a block-derived clock ships as (§B2.3); MTW record's precedent of reporting seconds rather than specifying them | Sampling the publish path: rows whose integration windows do not tile the session, so §3's energy sum is wrong by the jitter |
| 2 | Short-term metrics **max-held** within the block, long-term integrated (§2) | Smaart SPL v9.1 p. 33, verbatim; its column names encode it (`SPL A Fast Max`) | Instantaneous sampling of every metric: discards the loudest moment of every block — the one a complaint is about |
| 3 | Store **`sumSquares`** beside `leqDb` (§2, §10) | §B3.2: every project stores dB and every project un-logs back to energy to compute anything; python-acoustics' `leq()` scale error shows what happens when the stored quantity and the divisor drift apart | Storing dB only: the file cannot reproduce the report's own numbers, and every aggregation pays a log/exp round trip |
| 4 | Longer windows are an **energy sum over blocks, recomputed** (§3) | `test_leq.cpp`'s duty-weighted energy sum already pins the identity; IEC 61672-1 cl. 3.12 Eq. (4)/(6) for SEL; error bound `900 × 2^-52 ≈ 2.0e-13` → `8.7e-13 dB` | Running-sum-minus-departing-block: O(1) but accumulates unbounded rounding over a session, with nothing on screen saying so |
| 5 | Ln from a **fixed 0.1 dB histogram**, bin centres, over/under counters, absence rather than clamping (§5) | Larson Davis 831/LxT: **2000 bins at 0.1 dB + 2 over/under bins, counts only** (§B1.6); half-bin bound `w/2 = 0.05 dB` derived in §5; IEC 61672-2 cl. 6.21 assigns the same 0.05 dB semi-range to a 0.1 dB display; NoiseCapture's floor-keyed bins show the bias a centre avoids | Exact sort of every sample: two copies and two sorts per call in the code as it stands (§C3), no composability across sessions, and exactness about a quantity whose own uncertainty is larger |
| 6 | Ln labels carry **weighting, detector, N and interval** (§5) | ISO 1996-1 cl. 3.1.3's own `L_AF95,1h`; the mandatory NOTE under 3.1; §B3.3's four incompatible index conventions | A bare "L90": not a well-defined number, and two instruments printing it are not measuring the same thing |
| 7 | Alarm compares a **windowed Leq**; ship **no invented hysteresis or debounce**; publish **`L_allow`** (§6) | §B3.6 — no project implements hysteresis and no source publishes a debounce or an amber margin; Smaart's `Duration` is post-fire flash time; 10EaZy's amber is an undocumented prediction algorithm; `memory/a-threshold-read-off-a-grid-is-that-grids-floor.md` | A traffic light with our own margins: a number this project originated, that nobody can check, defending against a flicker that integration already removes |
| 8 | The +2..3 dB figures are a **shorter-integration proxy**, not an amber margin (§6) | Pop Code cl. 4.12 "typically some 2-3 dB(A) above the 15 minute value"; VLAREM `LAeq,15min ≤ 102` deems `LAeq,60min ≤ 100` satisfied | Reusing them as a warning colour: the exact category error the memory file names |
| 9 | Dose is **one formula with `q` per preset**, a base-10 denominator (§7) | IEC 61252 Ed 2 Formulas (7)/(8); ANSI S1.25 cl. 4.5/4.7/7.2.2; NIOSH 98-126 §1.1.1 p.1 and §1.1.3 p.2; OSHA App A I(1)(i)/I(2) against **Table G-16a**; and the row-by-row evaluation in record §7 showing **98-126's Table 1-1 and Table 1-2 need different `q`, up to 4.2549 % apart at 140 dB(A)** | A boolean "3 dB / 5 dB": hides that IEC's own two formulas differ by 0.34 % in the denominator (**+2.40 % of dose at ΔL = 30 dB**), and cannot express the NIOSH inconsistency at all |
| 10 | **Two dose accumulators at once**, threshold a per-preset field (§7) | Larson Davis `NUM_SLM_DOSES = 2` with per-dose threshold/exchange/criterion; Smaart's `Exposure O` + `Exposure N` columns; §B3.7's four-way threshold disagreement | One dose setting: cannot produce the OSHA-and-NIOSH-side-by-side log the market expects, and forces a wrong threshold on one of them |
| 11 | Peak is **C-weighted and separate from dose**; the three 140s are named (§7a) | IEC 61252 Ed 2 cl. 4.18 makes C-peak mandatory; ISO 1996-1 cl. 3.1.4 Note 3 (C-only accuracy); OSHA's 140 is a peak limit **with no weighting stated, scoped to impulsive/impact noise**, EU's is 140 dB(C) peak for all noise, NIOSH's is a 140 dBA **level** ceiling for all noise | One "140 dB peak" readout: silently wrong in at least two of three jurisdictions, and wrong about which *events* it constrains in the third |
| 12 | **Calibration is a flow**, with a post-check and a drift against **0.5 dB** (§8) | ISO 1996-2 cl. 5.2, body read in full; 10EaZy's ±1.5 dB factory refusal and dated calibration record; Cirrus AuditStore stores the last calibration with the measurement; **Smaart and 10EaZy put no calibration pair in the report** | A typed offset: cannot perform a drift check, cannot refuse a bad calibration, and leaves the report unable to say the measurement was trustworthy |
| 13 | **One HTML document, two transports**; PDF is the browser's print (§9) | JUCE 9.0.1 has no PDF writer (§C5); both live-sound viewers serve the page from the app; the report and the viewer draw the same quantities | Bundling a PDF library: a new dependency, an AGPL licence review, and a second renderer that will drift from the on-screen one |
| 14 | The viewer is a **client of the remote API**, and the SPL publish path is this lane's first wave (§9) | Owner ruling 2026-09-06; **`docs/dsp/2026-09-16-remote-api.md` §12 at `5b62218` rules it explicitly**, with Smaart's same-port-26000 precedent and SysTune's bundled NGINX as the counter-example; §12.1 of that record independently found `Snapshot` dBFS-only and `Leq` with no `app/` caller; 10EaZy's port-80-no-auth-port-forward is the anti-pattern | A second HTTP surface: two servers, two bind rules, two attack surfaces, and a ruling quietly reversed. (An earlier draft of §9 reasoned from `juce::StreamingSocket`; superseded — that record took cpp-httplib) |
| 15 | Log is **append-only wide rows** with a `#key=value` header; settings cannot change mid-log (§10) | Cirrus, REW and Smaart independently converged on one header block plus wide rows; Smaart's temp-file finalisation leaves an unreadable file if interrupted; ISO 1996-2 cl. 5.2's discard rule is about an instrument that changed underneath the measurement | A per-row weighting column: makes the file a record of an instrument reconfigured mid-measurement, which is the thing the discard rule exists to prevent |
| 16 | The `core_makes_no_class_1_claim` glob must reach the **report templates** (§11) | §C6: the guard stops at `core/`, and the report is the one artefact a regulator reads | Leaving it: the honesty rule stops being enforced exactly where a false Class claim would do the most damage |

## Sources

Standards previews (IEC/ISO-typeset, authorised distributor, all read
2026-09-16): IEC 61672-1:2013, IEC 61672-1:2002, IEC 61672-2:2013,
IEC 61672-3:2013, IEC 61252 Ed 2.0:2025, IEC 61252 Ed 1.2:2017 redline,
ISO 1996-1:2016, ISO 1996-2:2017 — URLs inline in Parts A1–A3.
Public regulation: 29 CFR 1910.95 via govinfo.gov; NIOSH 98-126 via
stacks.cdc.gov (image-only) with the nonoise.org mirror, plus cdc.gov bulletin
and dosimeter-settings pages; NIOSH 2025-104; EU Directive 2003/10/EC via
eur-lex; VLAREM II art. 5.32.2.2bis; UK Noise Council Pop Code 1995.
ANSI S1.25-1991 via archive.org OCR. NPL technical guide to IEC 61672 part 2.
PTB (Kling et al. 2021). Norsonic NorCal manual.
Commercial documentation: Smaart SPL v9.1 User Guide and Rational Acoustics
support articles; 10EaZy User Guide v2.5; REW help (`splmeter.html`,
`api.html`); Open Sound Meter manual v1.5; Cirrus Research AuditStore; NTi XL2
support pages; Castle Group, Larson Davis and Faber Acoustical dosimetry pages.
Open-source code at the pinned SHAs in §B1.
This repo on `6d9a53d`, and the pinned JUCE 9.0.1 checkout at
`D:\DEV CAVE EP3\PROJECT005-AZ-handsfree\external\JUCE`.
