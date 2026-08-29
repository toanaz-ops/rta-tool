# Dual-FFT conventions that must not be "tidied"

Four conventions in the transfer-function engine look arbitrary and are not.
Each was chosen for a reason recorded in `docs/dsp/2026-08-28-dual-fft.md`, and
each has a plausible-looking alternative that a later reader will be tempted to
switch to.

## 1. `Sxy = conj(X) * Y`, with X the reference

X is the **reference** (the electrical tap), Y is the **measurement** (the
microphone). `Sxy = conj(X)·Y` gives H1 = Sxy/Sxx the phase of Y relative to X,
which is what the operator wants to read.

This also matches `scipy.signal.csd(x, y)`, whose `_spectral_helper` computes
`conjugate(result_x) * result_y` — so the golden vectors in
`core/tests/golden/transfer.txt` line up with no conjugation in between.

Flipping it conjugates the whole trace's phase. Two tests pin it:
`the cross-spectrum carries the delay's SIGN, not just its size`
(test_dualfft.cpp) and the goldens. Note the earlier delay tests could NOT catch
it, because after compensation the two frames are bit-identical and
`conj(X)·Y == conj(X·conj(Y))` is real either way.

## 2. Positive delay means the MEASUREMENT lags

`findDelayPhat` returns `delaySamples` positive when the measurement lags the
reference, and `DualFftEngine::Config::referenceDelaySamples` uses the identical
convention — so the finder's output feeds the engine's field directly, with no
negation anywhere in between. A flipped sign there does not fail to compensate;
it compensates the wrong way and **doubles** the error.

The compensation is a one-time stream skip, not per-frame arithmetic: positive D
discards the first D samples of the measurement, negative D discards from the
reference, and the two rings are index-aligned for ever after. There is no
per-frame index to get wrong.

## 3. Coherence is magnitude-SQUARED, so ours reads LOWER than Open Sound Meter's

`γ² = |Sxy|²/(Sxx·Syy)`, as Bendat & Piersol, scipy and MATLAB define it. Open
Sound Meter displays the **un-squared** `|Grm|/√(Grr·Gmm)`, so on identical data
their number is higher than ours.

**That is a different quantity, not a bug.** Anyone comparing the two products
side by side will see the discrepancy; it must not be "corrected". The squared
form is also the only one for which `H1/H2 == γ²`, which is the free
self-test the suite uses to validate all three estimators against each other.

## 4. Coherence below the floor is ABSENT, not zero

For a single frame `|X·Y|² ≡ |X|²·|Y|²` identically, so coherence is exactly
1.0 at every frequency and a completely broken engine looks flawless. The same
trap returns after every `reset()` and after any config change — the three
moments someone is most likely to be watching the screen.

So `TransferSnapshot::coherence` is a `std::optional`, empty below the floor.
Not 0.0, not −1: a sentinel would be plotted. The floor is on **effective**
averages (`AverageCount.h`), never the raw frame count — overlapped frames are
not independent, and a raw count overstates independence exactly when the FIFO
saturates.

The threshold's VALUE (8) is a judgement and must never be cited as a standard.
The threshold's EXISTENCE is not: `DualFftEngine::validate()` rejects
`minimumEffectiveAverages < 1.0`, and `coherence_gate_is_not_bypassed` fails the
build if the field is written outside `makeSnapshot`.

## What proved these, and what did not

Not one of the nine test-suite holes found while building this engine came from
a failing test. Every one came from deliberately breaking working code and
noticing that nothing went red. Reasoning about whether a test is sharp is not
enough — see [[float32-fft-precision]] for the tolerances this interacts with.
