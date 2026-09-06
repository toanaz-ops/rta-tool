# Lane L6b — station-1 research and the decisions it produced

*2026-09-06. Three read-only research agents ran in parallel on HEAD `60ba99c`:
literature and commercial practice (Part A), open-source code (Part B), and this
repo's plug-in surface (Part C). Their findings are preserved here because the
decision record (`docs/dsp/2026-09-06-multichannel-l6b.md`) cites them but a
citation is not the finding. Part D maps every decision to its evidence, the
alternative it rejected, and what the verifiers later corrected.*

Scope under research: spatial multi-mic averaging with per-mic weighting and
SPL alignment (**G14**), coherence-weighted blending (**G15**), measurement
sequencing with auto solo/mute and automatic bad-capture discard (**G20**), a
full routing matrix, presets, and a remote API — the L6b row of
`docs/plans/MASTER-EXECUTION-PLAN.md`.

---

## Part A — Literature and commercial practice

### Three corrections to the brief this lane was opened with

1. **SysTune "SSA" is not "Smoothed Spatial Averaging."** It is *Spectrally
   Selective Accumulation*, a patented noise-immunity filter on **temporal**
   accumulation (AFMG SysTune Manual 1.3 pp.140–142). It is a bad-data-discard
   precedent for **G20**, not a multi-mic blending precedent for G15. AFMG holds
   US 8,208,647 (priority 2007, expiry 2031) whose claims 6–7 cover averaging
   room impulse responses from a plurality of input channels in real time.
   Frequency-domain averaging of already-computed transfer functions reads as
   outside those claims; **no freedom-to-operate opinion exists** — UNVERIFIED.
2. **Open Sound Meter has a remote API** (UDP multicast `239.255.42.42:49007`;
   a Bitfocus Companion module consumes it). Details in Part B.
3. **REW has no coherence weighting** in any averaging mode; its averages are
   unweighted. The parity table's attribution to "REW Pro" is unsupported.

### G14 — spatial averaging: what ships

| Claim | Source |
|---|---|
| Spatial = across locations, temporal = one location over time | Smaart LE v9.1 User Guide p.76 |
| Two spatial types: **dB** (arithmetic mean of dB magnitudes) and **Power** (mean of squared linear magnitudes, back to dB); **"In Smaart, decibel averaging is the default"** | LE v9.1 p.77 |
| dB average = "consensus", every contributor equal; power "gives more weight to the contributors with higher levels" and "de-emphasize[s] the contributions from nulls when averaging data containing comb filters" | LE v9.1 p.77 |
| Live average = an *engine composed of engines*; contributors count whether shown or hidden; stopping an engine removes it | LE v9.1 pp.78, 87 |
| Smaart's **Polar / Complex** setting is *temporal* averaging inside one engine (dB per frame vs separate real/imag running means); "Phase traces will always use complex averaging" | LE v9.1 pp.75, 86; support article "Polar vs. Complex Magnitude Averaging" |
| REW overlay modes: *Vector average* (needs an IR), *RMS average* ("treated as incoherent"), *dB average*, *RMS + phase avg.*, *dB + phase avg.* (magnitude from RMS/dB rule, phase from the vector average) | REW help, All SPL graph; "What's new in V5.30" |
| SysTune multi-channel: up to 8 channels, "**power average in the frequency domain**", per-channel gains "to add weight", per-channel delay offsets; IR windows disabled in that mode | SysTune Manual 1.3 pp.123–127 |

The shipped maths, per bin `k`, mic `i`, `N` mics:

```
complex / vector :  H = (1/N) Σ H_i                      REW vector; Smaart "Complex" (temporal only)
magnitude-dB     :  L = (1/N) Σ 20·log10|H_i|            Smaart dB average (DEFAULT); REW dB average
power / RMS      :  L = 10·log10((1/N) Σ |H_i|²)         Smaart Power; REW RMS; SysTune; SMPTE ST 202 A.3.5
magnitude+phase  :  |H| from dB or RMS rule, arg(H) from the complex average     REW "+ phase"
```

**No surveyed product averages unwrapped phase.** Every one that answers takes
phase from the complex average.

**SPL alignment before averaging — documented, with numbers.** Smaart
*Normalized Power Averaging* (LE v9.1 p.77; support article 150000214545):
"calculating a single-figure decibel average of all frequency data points
within a given frequency range for each trace … then adjusting the overall
level of each trace so that their average level within that range is
identical." Transfer function: **0 dB over 225 Hz – 8.8 kHz**, automatic, and
only for *power* averages; RTA: 125 Hz – 4 kHz against a designated reference
trace. Failure mode printed: a subwoofer has nothing in that band — "a dB
average with coherence weighting may be a better choice." REW: *Align SPL* over
a **user-selected span**, plus *Cross corr align* for time; "Measurements
should be time and level aligned before averaging them." SysTune: manual
per-channel gains. Nobody normalises to a single frequency.

**Standards.**

- **SMPTE ST 202:2010** (free full text; clauses exact). §5.2: positions
  sufficient to bring the position-to-position standard deviation under
  **3 dB**, "typically … **four positions**"; extra series for a balcony.
  §5.3: ear height 1.0–1.2 m, ≥ 1.5 m from any wall, ≥ 5.0 m from the screen
  loudspeakers. **§A.3.5 Spatial averaging**: "the **sum of the squares** of
  the sound pressure levels" per 1/3-octave band; **"If the range of sound
  pressure levels is within 4 dB, simple arithmetic averaging may be used."**
  Minimum mic spacing 1.0 m; avoid centrelines and balcony lips; "Large
  standard deviations may indicate significant acoustic or loudspeaker
  coverage problems." §A.3.6: ≥ 20 s integration in the lowest bands for
  ±1 dB. §A.4 a): four calibrated omnis through a "microphone **multiplexer
  switch (not a mixer)**". §5.5: a band is usable directly at ≥ 10 dB above
  background; 4–10 dB may be corrected per ANSI/ASA S1.13 Table 4. **§5.6:
  "each channel or bank shall be measured separately in turn."**
- **ISO 3382-1:2009**: Clause 8 "Spatial averaging" exists (one page); **body
  not available online — do not invent it.** Readable §4.3: minimum **two**
  source positions; mic positions ≥ λ/2 apart (~2 m), ≥ λ/4 from any surface
  (~1 m), height 1.2 m; "three or four microphone positions will be adequate"
  for a homogeneous room. Web summaries claiming "6 receivers / 12
  combinations" conflate with ISO 3382-2 — trust the standard text.
- **ISO 2969:2015**: paywalled, no clause text. **AES75-2022**: single-position
  maximum-SPL (M-Noise), relevant to G13 not G14; clause text not read.
  **AES2-2012**: not retrieved.

**Practitioners against the vendors.** Lawrence (Rational Acoustics, *Know
Your Audio Analyzer Averages*): in most circumstances the dB-vs-power
difference is "very small"; more positions beat a better rule. AFMG's
Tech-Note (Manual 1.3 p.127) is the strongest published warning against the
feature: a deep gap at one position "may hardly be recognized in an average of
4 channels or more … Averaging only makes sense if the included data sets are
reasonably comparable to each other … It is not the Silver Bullet." McCarthy's
page numbers could not be verified online.

### G15 — coherence weighting: one product, no formula

- **Smaart is the only surveyed product with a shipping coherence-weighted
  average**: a "Coherence Weighted" checkbox on TF averages only (LE v9.1
  pp.49, 87). Support article 150000214546: "each data point in every
  measurement being averaged is weighted according to its coherence value";
  rationale: coherence "tends to be an indicator of a signal-to-noise ratio".
  Positioned against power averaging, which "is unable to distinguish
  uncorrelated energy from the system response".
- **Smaart contradicts itself on the default**: the article says
  coherence-weighted dB "is the default selection for transfer function
  spatial averages"; the LE v9.1 guide p.77 says dB averaging is the default
  with coherence weighting an unchecked option. Probably an LE/Suite tier
  difference; neither document says so.
- **No product publishes the weight.** Not `γ²`, not `γ^p`, not
  `n_d·γ²/(1−γ²)`, no floor, no normalisation. A genuine vacuum.
- **The statistical basis is confirmed in form, not in equation number.**
  Bendat & Piersol, *Random Data*, Ch. 9 (via Gille, UCSD SIOC 221A lecture
  notes, and a second source citing "Table 9.6"): for `n_d` averages,

  ```
  ε[|Ĥ|] = σ[φ̂] = sqrt(1 − γ²) / (|γ| · sqrt(2·n_d))         var[|Ĥ|] ≈ |H|²(1−γ²)/(2·n_d·γ²)
  σ[γ̂²]  = sqrt(2/n_d) · γ · (1 − γ²)
  ```

  so the inverse-variance weight is `w ∝ n_d·γ²/(1−γ²)`, the **same** for
  phase and for dB magnitude (the `|H|²` factor cancels under the log).
  **Equation and table numbers, and the edition, are UNVERIFIED — the book was
  not opened. Do not write "Eq. 9.90" into the record.**
- **Hard gates that are documented**: Smaart *Coherence Blanking* — display
  only, "doesn't alter the underlying data", threshold a user slider, no
  published default (80–95 % is forum folklore, UNVERIFIED). Smaart *Magnitude
  Thresholding* — a real per-bin gate on the **reference** channel, default
  **−70 dBFS** in TF mode; failing bins "are simply **not updated**", so a
  bin holds its previous value rather than blanking.
- Per bin or per band, before or after banding: **unstated everywhere.**

### G20 — sequencing and bad-capture discard

- **L-Acoustics M1** is the worst-documented product surveyed: only launch
  press (one release, six outlets) — "Autosolo: automatic solo of each source
  to measure within a measurement group", automatic labelling and grouping,
  "spatial averages and sums of elements". **No source defines a discard
  criterion, an averaging rule or a weight.** The parity table's "automatic
  bad-capture discard" is not verified by any reachable document.
- **Meyer SIM3** sequences in **hardware**: the SIM-3081 is an 8-input mic-level
  *switcher* with a network address; SIM3 multiplexes one analyser across
  positions. The "8×8 bus" in the parity table is UNVERIFIED.
- **SMPTE ST 202 §5.6 + §A.4** is the standards basis: measure each channel
  in turn, through a multiplexer, not a mixer.
- **Nobody defines "bad" at whole-capture granularity.** Two products define
  it per frame or per bin, in detail:
  - Smaart *Protecting the Averaging Buffer* (LE v9.1 pp.77–78): Magnitude
    Thresholding (above); **Overload Protection — "three or more consecutive
    samples with maximum (0 dBFS) amplitude in either the reference or the
    measurement signal … will stop updating transfer function measurement
    data until the overload condition has subsided"**; Delay Tracker; Live IR
    Signal Presence Detection (stop when the reference falls below threshold).
  - SysTune *SSA* (Manual 1.3 pp.140–142), three cascaded stages: (1) signal
    threshold per frequency against a pink-shaped threshold from a broadband
    reference level ("typically between −40 dBFS and −80 dBFS"); (2)
    **excursion filter** — a block deviating significantly from the running
    average "will be included with small weight or not included at all",
    tolerance SMALL/LARGE — **the only documented soft rejection**; (3)
    coherence filter — a block below a coherence threshold (LOW/MEDIUM/HIGH)
    is discarded. AFMG prints the failure modes: wrong thresholds include
    noise or exclude data, and per-bin gating "may … leave **undefined gaps**
    … an artificially increased noise floor in the time domain" — **a per-bin
    gate on a transfer function corrupts its impulse response.**
- The hypothesised rule "coherence below X across Y % of bandwidth" appears in
  **no** source; it would be this project's invention.

### Routing matrix

- **Smaart** (LE v9.1 pp.85–88): engines persist in a *Measurement
  Configuration* and are dragged into the Control Bar to run; per engine, two
  (Device, Channel) dropdowns for Measurement and Reference — arbitrary
  many-to-many, one input may reference many engines. *Allow Multi-Device TF*
  is an advanced option, **off by default, with the printed reason "clock
  drift"**. Live average engines are engines composed of engines.
- **SysTune**: up to 8 signal channels, **one** reference, multi-select in the
  channel button row, per-channel delay and gain.
- **SIM3**: switched, not parallel.

### Presets — what a configuration actually carries

Smaart (LE v9.1 pp.28–29, 86–87): whole program state in `SmaartConfig.xml`,
"including hardware-specific information, so it is not recommended to attempt
to transfer stored configuration files between computers". Per engine: name,
delay (ms; positive delays the reference), averaging depth **with a "Use
Global" checkbox**, average type (Use Global), colour, plot, invert, phase and
magnitude smoothing (Use Global), and the four input dropdowns. The
*Use Global / pinned* pattern appears on four settings. Separate asset folders
for skins and target curves; ten user view presets. Mic correction files are
data files, and the manual is sceptical of them for field work. SysTune
AUBION X.8 tracks interface gain changes into the stored calibration (±0.5 dB
per step caveat). REW: UNVERIFIED in detail.

### Remote API

| | Smaart Suite/RT | REW 5.40+ | SysTune | OSM |
|---|---|---|---|---|
| Transport | **JSON over WebSocket** | HTTP, OpenAPI at `/doc.json` | HTTP via bundled NGINX | UDP multicast + TCP |
| Bind / port | **all adapters incl. wireless**, default 26000 | **127.0.0.1**:4735 | host IP, e.g. 9000 | 239.255.42.42:49007 |
| Auth | optional password, "highly recommended" | none | none | none |
| Push | none — "clients cannot detect changes … made by the host or by other clients" | webhook subscriptions | — | multicast |
| Licensing | SDK free on request, commands not public | GET free; PUT/POST need Pro | in-product | open |

Smaart's API lets clients start/stop generators, set delay, averaging, banding,
smoothing, capture data; the host processes everything. REW binds localhost by
default — the safer default of the two.

### Where sources disagree

1. Smaart vs Smaart on whether coherence weighting is the default.
2. Default rule: Smaart **dB**; SMPTE ST 202 and SysTune **power** (SMPTE allows
   arithmetic only when the spread is ≤ 4 dB); REW neutral.
3. Level alignment: automatic fixed band and power-only (Smaart); manual span
   (REW); manual gains (SysTune); none, replaced by a spread test (SMPTE).
4. Time alignment before averaging: REW insists; Smaart never mentions it
   across mics; SysTune offers per-channel offsets.
5. Parallel engines (Smaart, SysTune) vs multiplexed capture (SIM3, SMPTE §A.4).
6. Is coherence weighting worth it: Rational Acoustics yes; REW absent; AFMG
   "not the Silver Bullet"; Lawrence "more positions beat better maths".
7. ISO 3382-1 position counts: secondary sources say 6/12, the standard says
   two sources and "three or four" mics.
8. Mic correction files: unnecessary for field work (Smaart) vs required
   calibration (SMPTE).

### What nobody documents, ranked by how much it hurts this lane

1. Any coherence-weighting formula.
2. Per bin or per band; before or after banding.
3. **How the average's own coherence is computed** — mean, weighted mean,
   minimum, or recomputed from pooled spectra. Not one source says.
4. Whether a coherence-weighted average is biased toward constructive-interference
   positions (weight correlates with the estimate). Nobody discusses it.
5. What the magnitude of a complex mean of decorrelated positions does
   (collapses toward zero) — REW hints by demanding alignment first.
6. What `n_d` is per trace in a static average (this repo carries
   `effectiveAverages` — an advantage).
7. M1's entire measurement model.
8. Any whole-capture discard criterion.
9. What a per-bin gate does to the impulse response (only AFMG admits it).
10. ISO 3382-1 Clause 8 and ISO 2969 clause text (paywalled).

### Operator vocabulary (app/ words; none may leak into ui/)

*dB Averaging*, *Power Averaging*, *Normalized Power Averaging*, *Coherence
Weighted*, *Polar / Complex* (temporal), *Live / Static average*, *Trace
Average*, *Live Average engine*, *Use Global*; REW *Vector / RMS / dB
average*, *Align SPL*, *Cross corr align*; SysTune *Multi-Channel*, *Signal
Channel*, *Reference Channel*, *Delay Offset*, *SSA Filter*, *Excursion
Tolerance*, *Coherence Threshold*; Smaart *Coherence Blanking*, *Magnitude
Thresholding*, *Overload Protection*, *Measurement Engine*, *Measurement
Configuration*, *Allow Multi-Device TF*; M1 *Measurement Group*, *Autosolo*;
SMPTE *multiplexer switch*, *position S*. **Ambiguous across products**: "RMS
average" (REW = power average; Smaart = polar/dB temporal averaging), "vector
average" (REW spatial; Smaart temporal), "SSA".

### Sources (Part A)

Smaart LE v9.1 User Guide (downloads.rationalacoustics.com), pp.28–29, 49, 63,
75–79, 85–88; Rational Acoustics support articles 150000214540/3/4/5/6,
150000186480, 150000183871, 150000187830, 150000199086, 150000092223,
150000183495; rationalacoustics.com "Integration with 3rd Party Products". AFMG
SysTune Manual 1.3 (afmg.eu), §5.3 pp.123–127, §5.5 pp.139–142, §5.8
pp.157–162, §5.9 pp.162–163, §5.10 pp.164–169; US 8,208,647 B2. REW help
`api.html`, `graph_allspl.html`, `newin5.30.html`. SMPTE ST 202:2010
(pub.smpte.org) §4.8, §5.1–5.6, §6.1, A.3.5, A.3.6, A.4. ISO 3382-1:2009 iTeh
preview (contents, §4.3, §5.1–5.2). Gille, UCSD SIOC 221A lectures 15–16.
Bendat & Piersol, *Random Data*, 4th ed. (not opened). Lawrence, *Know Your
Audio Analyzer Averages* (Sound Design Live). M1 launch coverage (AVNetwork,
audioXpress). Meyer SIM-3081 product listing. OSM remote API via Bitfocus
Companion module HELP.md.

---

## Part B — Open-source code

### Open Sound Meter `Union` — the only open multi-measurement combiner with coherence

`src/source/union.h` (commit `1e08de2`; the repo is `psmokotnin/osm`):

```cpp
enum Operation {Summation, Subtract, Avg, Min, Max, Diff, Apply};
enum Type {Vector, Polar, dB, Power};
```

`Type` is the averaging domain, `Operation` the reduction; the two are
orthogonal. Per bin `i`, over `count` non-null sources (`src/source/union.cpp`):

| Type | magnitude | phase | note |
|---|---|---|---|
| `Vector` | `a += phase(i)·module(i)`; `a /= count`; `|a|` | complex `m += phase(i)·magnitudeRaw(i)`; `m.normalize()` | **two** complex accumulators over two different level quantities; published magnitude/phase come from `m`, published module from `a` — undocumented |
| `Polar` | arithmetic mean of `magnitudeRaw` | sum of unit vectors `e^{jφ}`, renormalised; NaN guard `phase = {1,0}` | two mics 180° apart → residue, not an error |
| `dB` | mean of `20·log10(module)` → geometric mean | as Polar | |
| `Power` | `sqrt(mean(module²))` = RMS | as Polar | emits a complex-averaged phase next to an energy-averaged magnitude, silently |
| `Apply` | product (cascade) | sum of angles | coherence = `std::min` |

**Coherence of the average — identical five lines in all four averaging types,
applied even to `Min`/`Max`/`Diff`:**

```cpp
coherence       += std::abs((*it)->module(i) * (*it)->coherence(i));
coherenceWeight += std::abs((*it)->module(i));
coherence /= coherenceWeight;
```

i.e. `γ_out = Σ|H_k|γ_k / Σ|H_k|` — a **magnitude-weighted mean of the input
coherences**, never the coherence of the averaged transfer function. Loud mics
dominate the reported coherence; two individually coherent mics that cancel
spatially still report high coherence. (OSM's `coherence` is the un-squared
form, `src/math/coherence.cpp`, ring depth 21, SSE `_mm_rsqrt_ps` — not
bit-reproducible.)

**Per-mic weights: none** — the only division is `/= count`. Level alignment is
upstream per `Measurement`: `applyAutoGain(reference)` = `reference −
level(Weighting::A, Meter::Slow) + gain()`, one broadband A-weighted Slow scalar,
applied at the sample level.

**Mismatched grids: refused, not solved** — `Union::calc()`:

```cpp
if (s->frequencyDomainSize() != primary->frequencyDomainSize()) {
    if (0/*can resize*/) { //resize
    } else { setActive(false); emit ...(" Sources must have the same window size and sample rate: "...); return; }
}
```

The sample rate is named in the toast and never compared. Only `Vector`
realigns the impulse response, by integer re-indexing on the difference of time
origins, dropping what falls outside the buffer.

**Routing: two integers per measurement** (`m_dataChanel`, `m_referenceChanel`,
`src/meta/metameasurement.h`), de-interleaved in `Measurement::writeData`; an
index past the device's channel count silently falls back to the generator
loopback. **Each `Measurement` opens its own audio stream** (`openInput` inside a
discarded `std::async`), so N mics = N streams over the same frames. A route
change sets `m_resetDelay = true`. Delay is per measurement, integer samples,
delay finder every 25th transform.

**Persistence:** JSON project, `{"type":"sourcelsist","list":[{"type","data"}]}`
(the typo is load-bearing). A measurement saves `delay, gain, offset,
averageType, average, filtersFrequency, window.type, dataChanel,
referenceChanel, polarity, deviceName, mode, inputFilters, calibration{...}`.
The device is persisted **by name**. A `Union` saves a recipe (`sources:
[uuid…]`), a `Stored` saves data rows `[frequency, module, magnitude, phase,
coherence, peakSquared, meanSquared]` plus `icoherence` (= "ignore coherence",
a display flag — the closest thing to a per-capture quality flag).

**Remote API exists:** `src/remote/`. One port **49007** for UDP multicast
discovery (`239.255.42.42`, TTL 1) and TCP request/response; `qCompress`-ed JSON
with envelope `{api:"Open Sound Meter", version, host, message, uuid, time}`;
hello every 1000 ms with `sources:[{uuid, objectName}]`; push messages
`added/removed/changed/readyRead/levels/generator_changed`; pull
`requestChanged/update/requestData/command`. The exposed surface is generated by
**Qt reflection** — `Q_PROPERTY` revision is the allowlist — so `dataChanel`,
`referenceChanel`, `delay`, `gain`, `polarity`… are remotely **writable**, with
no authentication, to anything link-local. `requestData` ships five doubles per
bin `[frequency, module, magnitudeRaw, phase, coherence]` — the wire format
carries an explicit frequency per row even though `Union` cannot.

**No sequencing, no capture queue, no bad-capture discard anywhere in OSM.**

### pyfar `average()` (`pyfar/dsp/dsp.py`, commit `32512a3`)

Modes actually present: `'linear'`, `'magnitude_zerophase'`, `'magnitude_phase'`,
`'power'` (`sqrt(mean(|X|²))`), `'log_magnitude_zerophase'`. **There is no
`'complex'` mode in `average()`** — `'complex'` belongs to
`smooth_fractional_octave` (`interpolation.py`). Phase in `'magnitude_phase'` is
**unwrapped** (`pyfar.dsp.phase(signal, unwrap=True)`), the opposite of OSM's
unit-vector sum. Weights:

```python
weights = np.broadcast_to(np.array(weights)[..., None], data.shape)
data = np.average(data, axis=axis, weights=weights, keepdims=keepdims)
```

`[..., None]` makes weights **per-channel, constant across bins**; a
coherence-weighted (per-bin) average is not expressible. `nan_policy='omit'` is
the only discard: per element, triggered by NaN, never by a quality metric.

### python-acoustics (tree `99d7920`) — none for transfer functions

No `iso_3382`, no coherence, no H1. Scalar energy mean `dbmean(levels) =
10·log10(mean(10^(L/10)))` (`decibel.py`); the only weighted spatial average is
`mean_alpha(alphas, surfaces) = np.average(alphas, axis=0, weights=surfaces)`
(`room.py`) — per-element weights, same shape as pyfar.

### REW (closed source; documented API, `localhost:4735`, OpenAPI at `/doc.json`, no auth)

`POST /measurements/process-measurements` commands, verbatim: `"Vector
average"`, `"RMS average"`, `"dB average"`, `"Magn plus phase average"`, `"dB
plus phase average"`, `"Vector sum"`, `"Time align"`, `"Cross corr align"`.
REW's definitions: RMS average converts dB → linear, squares, means, roots,
"measurements are treated as incoherent"; the result covers **only the
overlapping frequency region of all traces**. Vector average "most appropriate
for … measurements which have been time and level aligned". `Align SPL(targetdB,
frequencyHz, spanOctaves)` is a separate, **band-limited** step. Sequencing:
measurement mode "single, repeated, ramped, or sequential";
`/measure/protection-options` — "abort measurements if heavy clipping is
detected on the input or if the SPL exceeds a limit" — **the only automatic
bad-capture discard found in any source**, an abort on input fault, not a
post-hoc score. Subscriptions by POST-URL; `POST /application/blocking` makes
the API synchronous. UNVERIFIED: the `ProcessMeasurements` body schema and
whether vector average re-grids (no running instance).

### scipy `csd` / `coherence` (`_spectral_py.py`, commit `78f6a6a`)

```python
elif average == 'mean':  Pxy = Pxy.mean(axis=-1)
# 'median': real and imaginary medians separately, then Pxy /= _median_bias(n)
Cxy = np.abs(Pxy)**2 / Pxx / Pyy
```

**Mean-of-cross-spectra, then ratio** — `E[Sxy]/E[Sxx]`, never `mean(Sxy/Sxx)`.
For M mics: pooled `H = ΣSxy,k / ΣSxx,k` weights each mic by its reference
power; averaged `H = (1/M)Σ Sxy,k/Sxx,k` is a different quantity. **With one
shared reference channel the two coincide exactly; they diverge the moment mics
have different reference routings or gains.** The coherence of the pooled
estimate, `|ΣSxy|²/(ΣSxx·ΣSyy)`, is the coherence *of* the average and does see
spatial cancellation; OSM's is not and does not. `average='median'` is a
distortion-robust segment discard hiding in plain sight.

### Negative results

Friture (tree `0c6a787`): no transfer function, no coherence — full listing
read. GitHub code search for "coherence weighted" transfer-function averaging in
C++/Python: zero audio-measurement hits (rate-limited after six queries — partly
UNVERIFIED). pyroomacoustics, pyo, sc3 not opened.

### Comparison

| | averaging domain | weights | coherence of the average | grid alignment | routing | persistence | remote |
|---|---|---|---|---|---|---|---|
| OSM `Union` | Vector / Polar / dB / Power | none (`/= count`) | `Σ|H|γ / Σ|H|` of inputs | refused (`if (0/*can resize*/)`) | 2 ints per measurement, 1 stream each | JSON, device by name | UDP multicast + TCP 49007, reflection, no auth |
| pyfar | linear / mag / mag+unwrapped phase / power / log-mag | per-signal, constant over bins | n/a | equality by contract | n/a | n/a | none |
| REW | vector / RMS / dB / +phase | `Align SPL` band-limited | not exposed | **intersection** | `/measure/*` | `.mdat` | HTTP/JSON 4735, OpenAPI, no auth |
| scipy | pooled cross-spectra (mean or complex median) | none | **from pooled spectra** | one grid | n/a | n/a | none |

### Where they disagree

1. **What "coherence of an average" means**: scipy — of the pooled spectra
   (honest under cancellation); OSM — weighted mean of inputs (blind to it);
   REW, pyfar — not reported.
2. **Phase**: pyfar unwraps and averages angles; OSM sums unit vectors; REW
   "vector averages" phase in its `+ phase` modes.
3. **Mismatched grids**: REW intersects, pyfar requires equality, OSM refuses.
   Nobody resamples.
4. **Magnitude and phase from different averages**: REW and pyfar do it on
   purpose and warn to align first; OSM's `Power`/`dB` do it by accident.
5. **Where per-mic weights live**: in the call (pyfar, python-acoustics); in
   the capture path as broadband A-weighted trim (OSM); as a band-limited
   alignment pass (REW).
6. **"Bad capture"**: abort on clipping/SPL (REW), statistical segment median
   (scipy), NaN omission (pyfar), a display flag (OSM). None is a coherence
   threshold; none is a queue of captures.
7. **Remote transport**: OSM link-local multicast + TCP, reflection-generated,
   writable routing, no auth; REW loopback HTTP + OpenAPI, no auth.

### Traps a real-time C++ engine hits that these tools avoid

- **Per-mic delay is not per-mic gain.** Averaging mics at different distances
  without per-mic delay destroys HF phase first (the "stair-step" coherence
  signature); REW says vector average needs prior time alignment; OSM resets
  delay on every route change. `DelayFinder::subSample` (documented gap)
  becomes load-bearing when mic spacing is not an integer number of samples.
- **Engines at different FIFO fill.** OSM refuses unequal grid sizes but happily
  averages a mic at frame 3 against one at frame 16; with
  `fifoEffectiveAverages(hann, N/4, 16) = 8.5866` and the gate first clearing at
  frame 15, those carry very different variance. `minimumEffectiveAverages` is
  a per-engine gate with no multi-engine analogue anywhere.
- **MTW grids are not a grid.** Every routine indexes by bin `i`. Two MTW
  results average only if their `(band, bin)` index matches, not just their
  length; `K = 6` vs `K = 7` gives the same shape below 94 Hz meaning different
  things. REW's intersection rule assumes a monotone axis.
- **The seam moves under averaging.** Each mic's band-edge ripple
  (`r_w(1440) = 0.0169` for the 2048 band) is an independent realisation; it
  partially averages out in `Power`/`dB` and not in `Vector`. L3's seam tests
  T1–T7 do not predict a multi-mic seam.
- **Per-bin weights**: pyfar's broadcast is per-signal; OSM weights coherence by
  magnitude, not magnitude by coherence. No open reference implementation of a
  γ²-weighted combine was found.
- **Allocation and cost**: OSM's `Union::calc()` takes two mutexes, builds a
  `std::set`, and runs on an 80 ms Qt timer — off any audio path. `std::pow`
  / `log10` / `sqrt` per bin per mic is affordable at 12.5 Hz on one grid and
  not at hop rate over 1281 points × K mics.
- **N streams vs one stream.** OSM's per-measurement `openInput` gives K clock
  domains of buffer jitter over one interface. One capture fanned out to K
  estimators has one arrival time — but then the routing matrix must be a real
  matrix and the reference is shared, which is exactly the condition under
  which pooled-cross-spectra and averaged-H1 coincide.
- **Device identity by name** binds a stored routing to whatever device answers
  to that name after re-enumeration — silent wrong data, not a load error.
- **Remote writes race the analysis thread.** OSM writes routing properties
  under `m_sourceList->lock()` and throws the delay away. An atomic-swap
  snapshot model needs its equivalent decision written down.

---

## Part C — This repo's plug-in surface (as of `60ba99c`)

### core/

- **`DualFftEngine`** (`DualFftEngine.h:34-56`): `fftSize` 4096 default, power of
  two ≥ 4, no upper bound; `hopSize`; `sampleRate`; `window`; `averaging {Fifo,
  Exponential}`; `fifoDepth` 16; `timeConstantSeconds` 0.5;
  `referenceDelaySamples` (|D| ≤ 2^20, checked before `validate()`);
  `minimumEffectiveAverages` 8.0. **Not movable, not copyable, no default
  constructor** (`RingBuffer` atomics, `RingBuffer.h:135-136`) → `unique_ptr`
  (precedent `MtwEngine.h:53-60`). Allocates in the constructor only
  (`DualFftEngine.cpp:50-87`); `process()` throws on length mismatch. **Exposes
  the averaged spectra**: `referencePsd()` = Sxx, `measurementPsd()` = Syy,
  `crossPsd()` = Sxy, all `double` (`DualFftEngine.h:79-81`), plus
  `effectiveAverages()` and `frameCount()`.
- **Memory per engine**, `N = fftSize`, `D = fifoDepth`, S = 0, N ≥ 1024
  (`C = 4N` ring capacity; two rings `8C`; frames `16N`; bins `16B`; FIFO
  `32·D·B` allocated unconditionally; sums+means `64B`; window `4N`; FFT
  tables `14N + 8`):

  ```
  bytes(N, D) = 106·N + 16·D·N + 32·D + 88
  ```

  Cross-checked against the L3 record's published 66.6 MB (MTW at D = 32),
  4.16 MB rings, 2.08 MB per frame of depth — all three match.
- **`MtwEngine`** (`MtwEngine.h:30-67`): `bandCount()`, `band(i)` returns the
  `const DualFftEngine&` of band `i` (so a pooled combine can reach every
  band's Sxx/Syy/Sxy); `makeMtwResult(engine, estimator)` is a free function.
  `MtwResult` holds per-band `TransferSnapshot`s and **no coherence array**;
  `coherenceAt(result, index)` returns `optional` (`MtwResult.h:44`,
  `MtwEngine.cpp:81-93`). Defaults: 7 bands, N = 65536…1024, ΣN = 130 048,
  1281 points.
- **`TransferSnapshot`** (`TransferEstimator.h:60-71`): `estimator, sampleRate,
  binWidthHz, effectiveAverages, h (complex<double>), magnitudeDb (floor −120),
  phaseRadians (wrapped), std::optional<std::vector<float>> coherence`.
  Coherence is assigned in exactly two lines, `TransferEstimator.cpp:91` and
  `:111-112`, behind `effectiveAverages >= minimumEffectiveAverages`; below the
  gate the optional is `nullopt` — not NaN, not zero.
- **`coherence_gate_is_not_bypassed`** (`check_coherence_gate.cmake`): globs
  `core/src/*.cpp` + `core/include/*.h` (not tests, not `.hpp`); the single
  sentinel is `core/src/dsp/TransferEstimator.cpp` (`:33-36`), and the script
  fails if the sentinel is absent. Four patterns (`:44-47`):
  `(\.|->)coherence[ \t]*=`, `coherence[ \t]*=[ \t]*std::vector`,
  `coherence[ \t]*\.[ \t]*emplace`,
  `&[ \t]*ident[ \t]*=[^;]*(\.|->)coherence`. Consequences for a new core
  file: reading via `has_value()` / `(*snap.coherence)[k]` is safe (precedent
  `MtwEngine.cpp:89-92`); `result.coherence = …` on **any** struct trips;
  `const auto& c = snap.coherence;` trips (pattern 4, and `[^;]*` spans
  lines); a member named `coherenceWeight` evades all four. The header
  (`:9-26`) calls itself "a tripwire for the ordinary and the careless, not a
  proof" — evading it by renaming is the thing the L3 record refused to do.
- **Guard file counts today and predicted** for one new `core/include/rta/dsp/X.h`
  + `core/src/dsp/X.cpp` + `core/tests/test_x.cpp`: `core_has_no_framework_deps`
  96 → 99; `coherence_gate` 62 → 64; `filter_design_has_no_polynomial_form`
  113 → 116; `measure_has_no_framework_deps` stays 30 unless the **explicit
  file list** at `app/tests/CMakeLists.txt:68` is extended by hand (an omitted
  new `app/` file is silently un-guarded); `platform_types` 5.
- **`AverageCount`**: `fifoEffectiveAverages = K / (1 + 2Σ(1−m/K)c(m·hop)²)`;
  `exponentialEffectiveAverages = 1 + (raw−1)/D`, `D = 1 + 2Σc(m·hop)²`
  (`AverageCount.cpp:59-64, 97-112`). For a weight `w = Neff·γ²/(1−γ²)` a new
  function needs **no** `AverageCount` call: `Neff` is already on the snapshot
  (`TransferEstimator.cpp:72`).
- **Combine-N precedent**: `rta::ir::decayTimesAcross()` → median + IQR +
  `spreadIsMeaningful()` (`DecayEnsemble.h:53-88`) — the only existing
  "N captures in, central value + spread + meaningful flag out" shape.
- `core/CMakeLists.txt:11-41` and `core/tests/CMakeLists.txt:11-45` are
  explicit alphabetical lists. `core/tests/golden/` is flat (9 `.txt`).
  **`tools/gen_mtw.py` has argparse** (`--out`, `--help` exits before any
  write) — the only one of eight `gen_*.py`; the memory "no gen script has
  argparse" is true of the other seven. A new `gen_spatial.py` follows
  `gen_mtw.py`.

### platform/ and app/

- **Channels and roles.** `kMaxChannels = 64` (`ChannelConfig.h:21`);
  `CaptureBus` pre-allocates 64 rings × 65536 samples = **16 MB** at
  construction (`CaptureBus.h:33-36`, `CaptureBus.cpp:9-19`); `prepare()`
  never allocates again. `ChannelRole {Unused, Measurement, Reference}` per
  channel (`ChannelConfig.h:13-17`), set by cycling a row in
  `ChannelRoleTable`. N channels may all be `Measurement`, **but the only
  lookup is `firstChannelWithRole`** (`ChannelConfig.h:117-125`, called at
  `AnalysisThread.cpp:111-113`) — every channel past the first of each role
  is silently ignored. A routing matrix is an **interface** change (`(role,
  tfIndex)` per channel, still relaxed atomics, plus an all-channels lookup);
  the bus itself needs nothing.
- **Audio callback** copies into the bus and clears outputs, nothing else
  (`AudioIo.cpp:111-145`, `ScopedNoDenormals` first at `:124`, guarded by
  `audioio_scoped_no_denormals_is_first`).
- **`Analyser`** owns exactly one `DualFftEngine dual_` and one `MtwEngine
  mtw_` (`Analyser.h:146, 159`), both constructed unconditionally; `pushPair`
  feeds both (`Analyser.cpp:121-150`). `AnalysisThread` holds one
  `unique_ptr<Analyser>`, one `hopScratch_`, one `referenceScratch_`
  (`AnalysisThread.h:87, 96, 102`); publishes by atomic `shared_ptr` release
  store at ≤ 20 Hz (`kMinPublishIntervalMs = 50`, `AnalysisThread.cpp:22`).
  Allocation is sanctioned at the publish site only, budgeted in a comment as
  "~8 KB, mostly spectrumDb" (`AnalysisThread.cpp:196-198`).
- **`Snapshot`** (`Snapshot.h:118-157`): `sequence, sampleRate, fftSize,
  fraction, bands, spectrumDb, optional<TransferBlock> transfer,
  optional<MtwBlock> mtw, hasReference, referenceBands, framesAnalysed,
  droppedSamples, …`; no timestamp on purpose. `MtwBlock` (`:97-108`) carries
  the flat per-point `coherence`; **the flat copy is `Analyser.cpp:259-264`**,
  outside the guard's reach, after the gate ran in `makeSnapshot()`.
- **Generator output: does not exist.** `AudioIo.cpp:134-137`: "This
  deliverable produces no audio output … clearing is the entire output-side
  contract." `core/src/gen/*` has no `app/` caller; `SyntheticInput` writes
  into the capture bus, not to a device, and says so (`SyntheticInput.h:30-35`).
  `numOutputChannels` is enumerated (`DeviceState.h:25`) and never written.
  **G20 auto solo/mute has no class, file or thread to extend.**
- **Network/remote: absent.** Zero hits for OSC/URL/Socket/HTTP/asio across
  `app/ platform/ core/ tools/`; `rtatool` links `juce_audio_utils`,
  `juce_gui_extra`, `juce_opengl` only (`app/CMakeLists.txt:127-131`).
  `core_has_no_framework_deps` blocks `asio` by name.
- **Session persistence exists**: `SessionDocument {schemaVersion = 2, captures,
  entries, panes}` (`SessionCodec.h:26, 38-43`), line-oriented `key=value`,
  sections `[capture]` / `[entry]` / `[pane]` (`SessionCodec.cpp:193-215`);
  `decodeIndex` **refuses a newer schema outright** (`:51-53`); storage is a
  folder with `session.index` written via `.tmp` + rename. `CaptureMeta` is
  immutable history (`Trace.h:22-24`) — a preset is editable config and does
  not belong there. `Trace::binHz()` derives the axis from `fftSize`
  (`Trace.h:91-93`); MTW traces remain live-only.
- **`tools/snapshot.cpp`** (230 lines) builds the transfer specimen at
  `:141-151` and never assigns `snapshot->mtw`; there is no `makeSyntheticMtw`
  in `SyntheticSnapshot.h`. `TransferSource {Mtw, Fixed}` and
  `setSource(pane, src)` exist (`TransferView.h:28, 49`) with **no UI caller**.
- **Line counts near the cap** (400 hard, 300 aim): `test_dualfft.cpp` **400**
  (frozen), `test_capture_bus.cpp` 375, `TransferView.cpp` 363,
  `test_transfer_view.cpp` 354, `test_session_codec.cpp` 340,
  `test_transfer_view_mtw.cpp` 331, `test_transfer_estimator.cpp` 328,
  `DualFftEngine.cpp` **321** (frozen), `Analyser.cpp` 287, `SessionCodec.cpp`
  287, `DevicePanel.cpp` 284.

### Memory and churn for N = 8 mics (arithmetic shown)

```
fixed DualFftEngine, N = 16384, D = 16:  106·16384 + 16·16·16384 + 32·16 + 88 =  5 931 608 B  (5.93 MB)
MtwEngine defaults, ΣN = 130 048, D = 16: 106·130048 + 256·130048 + 7·(512+88) = 47 081 576 B  (47.08 MB)
per mic                                                                        = 53 013 184 B  (53.0 MB)
N = 8                                                                          = 424 105 472 B  (424 MB)
+ CaptureBus flat 16 MB                                                        ≈ 440 MB resident
```

Sixty-four reference rings (8 mics × 8 engines) duplicate the reference stream
for ≈ 4.7 MB — the direct cost of "no shared upstream skip". **Publish-time
churn**: `Analyser::publish` rebuilds `MtwResult` (≈ 1.86 MB) + the fixed
snapshot (≈ 0.23 MB) + app blocks (≈ 0.12 MB) ≈ 2.21 MB per mic per publish
(the agent's own sum read 2.09; the record verifier caught the dropped term);
× 8 × 20 Hz ≈ **354 MB/s** of allocate-and-free on the analysis thread — three
orders of magnitude above the "~8 KB" the comment at `AnalysisThread.cpp:196-198`
budgets. This is the most likely L6b performance surprise.

### What L6b would have to add, and where

| Feature | Layer | Extends | Constraint hit |
|---|---|---|---|
| N parallel TFs | app/measure | `Analyser` (one `dual_` + one `mtw_`), `AnalysisThread` (one `Analyser`, one scratch) | non-movable engines → `vector<unique_ptr>`, never resized; `Analyser.cpp` 287 → split publish out first; N scratch buffers allocated in the ctor |
| Spatial average + coherence weights | **core** (`SpatialAverage.{h,cpp}`) | `TransferSnapshot` span in, or engine spectra in; `DecayEnsemble` shape | coherence guard: read freely, never write a field named `coherence`, never bind a reference; or extend the sentinel honestly |
| Sequencing + auto-discard | **platform** (new output path) + app (state machine) | nothing — outputs are cleared today | real-time rule; `ScopedNoDenormals` must stay first; likely `AudioIo_Output.cpp` |
| Routing matrix | platform/types + app/view | `ChannelRole`, `ChannelConfig`, `ChannelRoleTable` | `firstChannelWithRole` is the only lookup; `ui/` may hold a grid widget but no `reference`/`measurement` names |
| Presets | app/trace | `SessionDocument`, `SessionStore` | schema 2 → 3; new section, not `[capture]`; `SessionCodec.cpp` 287; add new files to the explicit guard list |
| Remote API | new sibling of `platform/` | nothing | new JUCE module or dependency; never `core/`, never `ui/`; consume `SnapshotSource`, reuse `key=value` |

---

## Part D — Decisions, their evidence, and what the verifiers changed

Record: `docs/dsp/2026-09-06-multichannel-l6b.md`. The record verifier ran
before station 3 read it; its six findings are in the last column. Later
columns are filled as the code verifiers report.

| # | Decision (record §) | Evidence from research | Alternative rejected, and its cost | Verifier correction |
|---|---|---|---|---|
| 1 | **Weighted dB mean** default, power the option; phase = weighted circular mean with agreement `R` (§2) | A: Smaart default dB, "consensus"; SMPTE ST 202 A.3.5 power with the 4 dB arithmetic clause; REW/Smaart phase always from the complex mean; B: OSM `Vector` collapses on phase disagreement | Power default: hot position outvotes (0.45 dB at 4 dB spread, 2.4 dB at 10); complex spatial mean: room "loses its high end"; unwrapped phase: undefined across 2π | — |
| 2 | **`W = u·γ²`**, gate exclusion, muted member at `u = 0`, two absence reasons `noContributor` / `noWeight` (§3) | A: B&P variance → inverse-variance weight; Smaart "weighted according to its coherence value"; B: no open reference implementation | Inverse-variance: 11:1 dominance at 0.99 vs 0.9, peak bias, a floor to invent; hard threshold: grid floor; `n_d` in weight: fill-time transient | `Σ W = 0` with contributors present was undefined (0/0) → second absence reason added; peak-bias claim now states its noise model; `1/n_d` bias marked UNVERIFIED (Carter–Knapp–Nuttall, paper not opened) |
| 3 | `weightedCoherence` (u-mean of gated γ²) + `phaseAgreement R`; guard's property kept by construction (§4) | A: nobody documents the average's coherence; B: OSM's `Σ|H|γ/Σ|H|` blind to cancellation, scipy's pooled coherence is of a vector mean | OSM's rule: loud+noisy reports confident; pooled coherence: collapses beside a dB mean it does not describe | Confirmed: `weightedCoherence`/`phaseAgreement` evade all four guard patterns; `const auto& c = *snap.coherence` trips pattern 4 |
| 4 | Identical grid required (throw), MTW averaged per band then existing stitch, per-position delay in the engine (§5) | B: REW intersects, OSM refuses, nobody resamples; C: `MtwResult::bandSnapshots`, `referenceDelaySamples` | Averaging stitched vectors: second stitch + flat coherence in core (the L3 refusal) | Delay sign: positive skips measurement, negative skips reference and shifts that engine's frame grid (`DualFftEngine.cpp:116-127`) — stated |
| 5 | N `Analyser`s behind a routing table; per-TF reference channel; **average group requires a shared reference**; publish = average + one solo (§6) | C: `firstChannelWithRole` only lookup, non-movable engines, 53 MB/position, churn 2.21 MB/position/publish; A: Smaart per-engine (Device, Channel), *Allow Multi-Device TF* off with "clock drift" | One `Analyser` owning N pairs: `Analyser.cpp` past 300; pooling in core: second guard sentinel; MTW-off default: memory was never the problem | §6 said "one reference handed to every Analyser" while §9 persisted a per-TF reference → reconciled; churn 2.09 → 2.21 MB (dropped term), 335 → 354 MB/s |
| 6 | Per-position dB trim + *Align levels* over the displayed span; no fixed band (§7) | A: Smaart 225 Hz–8.8 kHz automatic, power-only, with its printed subwoofer failure; REW `Align SPL` over a chosen span; SysTune per-channel gains | Smaart's band: a threshold off someone else's grid | — |
| 7 | Sequencer in app; refusal on overload (≥ 3 consecutive `|x| ≥ 1−2⁻¹⁵`) and gate-not-cleared; trusted fraction reported not enforced; **no generator output** (§8) | A: Smaart overload rule, SMPTE §5.6 measure in turn, SysTune SSA stages, nobody defines whole-capture "bad"; C: outputs cleared, `core/gen` has no app caller | "Below X across Y %": invented number; writing the generator into the callback: lock or allocation = dropout | int16/24/32/float full-scale values confirmed |
| 8 | Presets: schema 3, `[tf]`/`[average]`, device by name **and** channel count, unbound on mismatch (§9) | B: OSM `deviceIdByName` silent rebind; A: Smaart *Use Global* pattern, configs not portable across machines; C: `decodeIndex` refuses newer schema | Store in `CaptureMeta`: immutable history; silent rebind: wrong data with no error | — |
| 9 | Remote API deferred with constraints (§10) | A/B: three transports, three bind defaults, none authenticated by default; C: no network code, guard names `asio` | Picking a transport now: a dependency and an attack surface without a research pass | Scope honesty confirmed against master plan L6b row |
| 10 | Verification T1–T13 (§11) | A: nobody publishes any verification of a spatial average; C: `DecayEnsemble` shape, golden conventions, `gen_mtw.py` argparse | Regression locks on the code's output | T5 gained the zero-weight fixture; T7's `R = |cos(πf/fs)|` confirmed |

Numbers the verifier re-derived independently and confirmed: the memory
formula and 5.93 / 47.08 / 53.0 / 424 MB; 16 MB bus; the power-vs-dB and
weight tables; frame 15 / 8.07 / 8.5866 / 80 ms / 5.1 s against
`test_mtw_layout.cpp:213,223,226`; guard scan count 62; the 30-file explicit
list; 287-line `Analyser.cpp` and `SessionCodec.cpp`; JUCE modules linked.

Not decided, on purpose (record §12): remote transport and write surface;
generator output path; numeric trusted-fraction refusal; excursion
down-weighting; complex spatial mode and sum-of-elements (G11); storing average
traces; the AFMG patent question for any future IR averaging.
