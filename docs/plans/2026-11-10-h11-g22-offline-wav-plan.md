# H11 -- G22 offline dual-FFT from a WAV file, plus the H9/H10 LOWs

*2026-11-10. Record: `docs/dsp/2026-11-10-g22-offline-wav-transfer.md` (§5 decisions
D1-D9). Code read at `d087302`. Closes master-plan row 5's last gap. Unmeasured
on hardware (HW-175 stays with the owner).*

## Tasks

| id | task | called from (production caller) | operator path | proof |
|---|---|---|---|---|
| W1 | `rta::dsp::TransferAveraging::Cumulative`: running Sxx/Syy/Sxy sums, no drop-off, O(numBins) memory; `effectiveAverages()` = `fifoEffectiveAverages(window, hop, frameCount)`; `reset()` clears it | `app/src/measure/WavTransfer.cpp` `analyseWavTransfer` (W2) | rail `LOAD WAV` -> `ANALYSE` | `core/tests/test_dualfft_cumulative.cpp`: after F frames Cumulative == Fifo(depth F) and == Fifo(depth F+7) bin-for-bin (tolerance derived from summation order); differs from Fifo(depth F-1) once F > depth; Neff identity; reset; the `Exponential` comment's "only one production value" note updated for the enum |
| W2 | pure app analysis (no JUCE): `WavTransferRequest{refChannel, measChannel, startSeconds, lengthSeconds}`, a `SampleBlockSource` interface (numChannels, sampleRate, lengthSamples, `read(channel, startSample, span<float>)`), `analyseWavTransfer(source, request, cancel) -> WavTransferOutcome` (trace or `WavTransferRefusal` + measured numbers), delay windows per D4, min range per D5, `wavTransferRefusalText()` sentences, `wavTransferTrace()` per D6 | `MainComponentWav.cpp` via `WavTransferJob` (W3) | as W1 | `app/tests/test_wav_transfer.cpp` with an in-memory source: pink ref, meas = ref through a known biquad delayed by D (D > 0 and D < 0) -> magnitude/phase vs `BiquadResponse` after removing D, coherence ~ 1, `appliedDelaySamples == D`, tolerances derived in the test; refusals: one channel, ref == meas, channel out of range, range past the end, too short (count from `fifoEffectiveAverages`), uncorrelated meas (NoCorrelation with the measured trust), delay that moves by > 1 sample between the first and last window (DelayMoved, both delays printed), cancel |
| W3 | JUCE side: `WavFileSource` (AudioFormatReader adapter, WAV + AIFF only), `WavTransferJob` worker (std::async, newest wins, cancel), rail `LOAD WAV` + WAV row (`REF ch`, `MEAS ch`, `START s`, `LENGTH s`, `ANALYSE`, readout), `MainComponentWav.cpp`, test seams `performLoadWav(File)` / `analyseWavForTest()` | `MainComponent` rail | rail `LOAD WAV` -> file chooser -> pick channels -> `ANALYSE` -> trace in library group `WAV`, visible in TRANSFER | `app/tests_juce/test_wav_transfer_file.cpp`: write a 24-bit PCM and a float32 4-channel WAV with the test's own writer, load, analyse ch2 vs ch0, library gains one `WAV` trace with the W2 closed-form magnitude; a mono file -> refusal text on the readout; snapshot `main-wav.png` |
| L1 | EQ target picker excludes group `GATED` (no fMin on a stored trace) | `EqPaneModel` candidates | EQ pane target picker | `app/tests/test_eq_pane_model*.cpp`: GATED trace absent from targets, still a measurement candidate only if it has coherence (it has none) |
| L2 | a GATED trace no longer takes the same shade as its source SWEEP: on store, shade = lowest index unused across the `SWEEP` and `GATED` groups | `MainComponentIr.cpp` store path | IR pane `STORE GATED` | JUCE/app test: first SWEEP shade 0 -> its first GATED trace shade != 0 |
| L3 | ALIGN NoSignal sentence: the early paths that never measured a confidence print no number (today "0.0 dB") | `AlignmentWizardSignals.cpp` `polarityRefusalText` | ALIGN wizard refusal | `app/tests/test_polarity_refusal_text.cpp`: early path sentence has no "dB"; measured path unchanged |

Every new `.cpp` goes into every source list (`app/cmake/rtatool_sources.cmake`,
`rtatool_snapshot_sources.cmake` if the snapshot compiles it, the test
CMakeLists). Wave close: `tools/orphan_check.py --base 9b6615a ...` exits 0,
then a read-only Fable review of the whole phase diff, HIGH/MEDIUM fixed.

## Order

W1 -> W2 -> W3 (each consumes the previous). L1-L3 are independent of G22 and
run in the same builder pass as W1 (different files, one build directory).
