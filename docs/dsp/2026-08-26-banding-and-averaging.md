# Fractional-octave banding and spectral averaging

*2026-08-26. Decision record for Phase 1. Written before the code.*

## The question

An RTA displays energy in fractional-octave bands. There are two ways to get
there, they disagree, and the disagreement is not cosmetic.

## What the measured numbers say

Fractional-octave bands are **constant-Q**: bandwidth scales with centre
frequency. An FFT has **constant-Δf** bins. The two do not fit, and the misfit
is worst exactly where live sound needs help most.

At 48 kHz, a 1/3-octave band at 31.5 Hz is 7.24 Hz wide. Bins per band:

| FFT size | Δf | bins in the 31.5 Hz band | block length |
|---|---|---|---|
| 4096 | 11.72 Hz | **0.62** | 85 ms |
| 16384 | 2.93 Hz | 2.47 | 341 ms |
| 65536 | 0.73 Hz | 9.88 | 1365 ms |

Below one bin per band the question stops being about accuracy: there is no
measurement there at all, only whichever neighbouring bin happens to be nearest.

Lowest frequency that still gets three bins per band:

| FFT size | 1/3 octave | 1/12 octave | block length |
|---|---|---|---|
| 4096 | 152 Hz | 607 Hz | 85 ms |
| 16384 | 38 Hz | 152 Hz | 341 ms |
| 32768 | 19 Hz | 76 Hz | 683 ms |
| 65536 | 9.5 Hz | 38 Hz | 1365 ms |

**No single FFT size serves the audio band.** Reaching 20 Hz at 1/3 octave costs
a 683 ms block, which smears every transient and lags the fader hand; and the
same transform spends 632 bins on the 2 kHz band, where twenty would do.

## What other people did, and where they disagree

| | approach | position |
|---|---|---|
| Smaart | FFT bins summed into bands, 1/1 to 1/48 | draws the distinction *banding = summation, smoothing = averaging* |
| Microstar TN-257 | FFT binning with fixed 3+4+5 bin groupings per octave, plus per-group correction factors (+0.337 / −0.152 / −0.086 dB) | FFT resolution is "vastly better" than filters, but **deliberately does not match** filter-based instruments |
| Audio Precision | weights each FFT bin by the theoretical 1/b-octave filter gain, then sums | **auto-limits the lowest band** to what the FFT length can support; IEC 61260 Class 0 at 20 Hz needs ~64x finer resolution than at 2 kHz |
| HEAD acoustics | offers both, and recommends the filter method | the filter method needs no windowing, so it avoids the artefacts windowing creates |
| Friture | filter bank with per-band decimation | — |
| pyfar / python-acoustics | Butterworth SOS, order >= 6 for 1/3 octave per IEC 61260-1 | — |

The split is not arbitrary. **Instrument vendors whose customers compare
readings against a sound level meter build filter banks. Vendors whose customers
look at a curve build FFT banding.** They are answering different questions.

## The decision

Phase 1 builds **both paths**, because they answer two different questions and
neither answer substitutes for the other.

| path | question it answers | how |
|---|---|---|
| FFT banding | "what shape is this response" | weighted bin summation into 1/1..1/48 octave bands, fast, log axis |
| IEC 61260 filter bank | "how many decibels is this band" | 6th-order Butterworth in second-order sections, with a decimation cascade |

The owner was shown the cost -- roughly double the phase, two paths to test
rather than one -- and chose both. Recorded here so the reasoning is not
rediscovered as an argument later.

What that buys: band levels that agree with a Class 1 sound level meter, which
the FFT path cannot deliver at any FFT length because it is not what it
measures. What it costs: the filter bank needs its own golden vectors. (The
decimation chain and per-band group-delay accounting originally feared here
turned out to be unnecessary — measured at ~43 M ops/s for the whole
single-rate bank; see 2026-08-27-filterbank.md.)

Broadband SPL -- Leq, LAeq, Fast/Slow -- needs neither: an A/C/Z weighting
filter and an exponential detector in the time domain is the whole of IEC
61672-1 for a broadband reading.

### The octave ratio is 10^(3/10), not 2

IEC 61260-1:2014 defines the octave frequency ratio as **G = 10^(3/10)**
(1.995262...), the base-ten system. G = 2 exactly -- the obvious choice, and the
one this project's spec originally assumed -- is the base-two system, which the
standard acknowledges but does not design to. Its words: the probability that a
base-two filter conforms decreases as the mid-band frequency moves away from the
1 kHz reference.

Measured, for 1/3-octave exact centres:

| band | base-10 | base-2 | error |
|---|---|---|---|
| 25 Hz | 25.1189 | 24.8031 | −21.9 cents |
| 100 Hz | 100.0000 | 99.2126 | −13.7 cents |
| 1 kHz (idx −1) | 794.328 | 793.701 | −1.4 cents |
| 12.5 kHz | 12589.25 | 12699.21 | +15.1 cents |

The error is zero at the reference and grows in both directions, which is
exactly the failure mode the standard warns about. Getting this wrong produces
band edges that are systematically offset from every conforming instrument,
while the plot looks entirely normal -- there is no symptom to notice.

**Default is base-10.** Base-2 is selectable, because instruments and older
measurement sets exist that used it and a comparison against them has to be
possible. HEAD acoustics exposes the same choice, as "Decade" versus "Octave".

### Bin-to-band mapping: weighted, not grouped

Bins are combined using a **precomputed sparse weight matrix**: each band holds
a list of (bin index, weight), where the weight is the theoretical 1/b-octave
filter's power gain at that bin's frequency. Built once when the FFT size,
sample rate or band resolution changes.

Rejected: hard bin grouping with per-group correction factors (Microstar). It is
simpler, but a bin sits either wholly inside a band or wholly outside it, so a
tone drifting across a band edge makes the display jump. The correction factors
exist precisely because the grouping is biased -- +0.34 dB for a 3-bin group is
a real error being papered over. Weighting removes the bias instead of
compensating for it, and costs one precomputed multiply per bin.

### Bands with too few bins are drawn, and marked

A band holding fewer than **three** bins is still drawn, but marked as
under-resolved.

Audio Precision and HEAD acoustics both hide such bands instead -- HEAD calls it
"skip bands below minimum bandwidth" -- and hiding them was the recommendation
here, on the grounds that a band computed from 0.62 bins is not a measurement.
The owner chose to keep them visible: at a show, seeing the shape of the bottom
octave matters even when its accuracy is qualified.

The marking must therefore not rely on tint alone. A dimmed bar in a dark room
at six feet is a dimmed bar nobody notices. The under-resolved region carries a
visible boundary as well, so the qualification survives the viewing conditions
the tool is actually used in.

### A band is summed from PSD, never from the power spectrum

Measured here, with a periodic Hann window, 8192 points, white noise of variance
4.0000:

```
sum(power spectrum)               = 6.0040     wrong, high by exactly 1.5x
integral(power spectral density)  = 4.0026     right
ratio                             = 1.500000   = the window's NENBW
```

Summing power-spectrum bins over-counts broadband energy by exactly the window's
equivalent noise bandwidth, because the window correlates neighbouring bins: a
Hann mainlobe spreads one bin's worth of noise over 1.5 bins, so adjacent bins
share energy and adding them counts it twice.

With Hann that is **1.76 dB high in every band**, on every noise or music
source, with a plot that looks entirely reasonable. It is the same trap as
confusing a window's amplitude correction with its energy correction, arriving
one layer up.

So: band level is `sum over bins of PSD[k] * delta_f`. The power spectrum is
still exposed, because it is the correct reading for a **tone** -- a sine at bin
centre reads its true mean square there, verified exact to the last digit -- but
it is never what a band is summed from.

Conventions, following Heinzel's treatment of DFT spectrum estimation:

```
S1 = sum(w)                 S2 = sum(w^2)
NENBW = N * S2 / S1^2       (bins; exactly 1.5 for periodic Hann)
ENBW  = fs * S2 / S1^2      (hertz)
power spectrum      PS[k]  = 2 |X[k]|^2 / S1^2          (halved at DC and Nyquist)
power spectral density PSD[k] = 2 |X[k]|^2 / (fs * S2)  (halved at DC and Nyquist)
PS[k] = PSD[k] * ENBW
```

Verified numerically before any of it was implemented: a sine of amplitude A
reads `A^2/2` in PS at its bin, and white noise of variance sigma^2 integrates
to sigma^2 under PSD.

### Averaging

Following Smaart's distinction, banding is summation and averaging is separate:

- **Linear (Welch)**: N frames averaged with equal weight. For a settled
  measurement of a stationary signal.
- **Exponential**: a one-pole smoother over frames, for a live display that
  keeps updating. This is the mode a live engineer actually watches.

Overlap defaults to **50%** with a Hann window, which is where the windowed
segments sum to unity and no sample is over-counted; 75% is offered because it
makes the display visibly steadier at the cost of correlated frames, which buy
less variance reduction than their count suggests.

## Consequences to check later

- **The two paths will disagree, and that is not a bug.** FFT band levels and
  filter-bank band levels differ by an amount no standard bounds: an FFT band
  interacts only with its immediate neighbour, while a 6th-order Butterworth
  band still responds three bands away. The UI must never present a number
  without saying which path produced it, and a comparison against a handheld
  meter is only meaningful against the filter-bank path.
- Phase 3's multi-time-window engine solves the low-frequency deficit for the
  FFT path, by running different FFT sizes on decimated streams. When it lands,
  the RTA should be able to use it, so `OctaveBands` must not assume a single
  delta-f.
- ~~The filter bank's decimation cascade introduces a different group delay per
  band~~ — resolved 2026-08-27: the bank runs single-rate, so this trap was
  removed rather than managed. See 2026-08-27-filterbank.md.

## Sources

- Microstar Laboratories, *Versatile FFT Supports Accurate 1/3 Octave Analysis*, TN-257
- Audio Precision, *Deriving Fractional-Octave Spectra from the FFT with APx*
- HEAD acoustics, *FFT - 1/n-octave analysis - wavelet*, Application Note 02/18
- Rational Acoustics, *Banding vs. Smoothing*
- IEC 61260-1:2014; IEC 61672-1
- Welch (1967); Harris (1978)
