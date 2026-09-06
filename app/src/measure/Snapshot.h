// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
// See docs/plans/2026-08-27-audioio-rta-impl-plan.md §1.3, §3.4.
#pragma once

#include "measure/Levels.h"

#include "rta/dsp/SpatialAverage.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rta::measure {

/// One fractional-octave band's reading, already in the app's own units:
/// hertz and dBFS, never a raw bin index or a linear power. A view draws
/// this directly; it does not need to know anything about FFTs.
struct BandReading {
    float centreHz = 0.0f;
    float lowerHz = 0.0f;
    float upperHz = 0.0f;
    float levelDb = static_cast<float>(kLevelFloorDb);
    /// True when the band spans fewer transform bins than
    /// `BandWeights::kMinBinsPerBand` -- the FFT physically cannot resolve
    /// it at the configured size. See plan §3.5.1 for how the view must
    /// draw this (a boundary line, not just a tint).
    bool underResolved = false;
};

/// The transfer function of measurement against reference, per bin, when a
/// reference channel was actually being fed. Built from
/// `rta::dsp::TransferSnapshot` in `Analyser::publish`.
///
/// Degrees, not radians: core wraps to (-pi, pi] because that is the natural
/// output of a complex division, and every consumer in `view/` works in
/// degrees because `PlotGeometry`'s phase pane runs +180 to -180. The
/// conversion happens exactly once, at that one seam, so no file downstream
/// has to know which unit it is holding.
struct TransferBlock {
    std::vector<float> magnitudeDb;
    std::vector<float> phaseDeg;

    /// Absent -- not empty, not a vector of 1.0 -- below the engine's
    /// effective-average gate. dual-FFT record section 3: a single frame
    /// gives coherence identically 1.0 at every frequency, so a broken
    /// engine looks perfect, and a sentinel would be plotted.
    std::optional<std::vector<float>> coherence;

    /// Effective, never a raw frame count: overlapped frames are not
    /// independent, and a gate that trusts a raw count opens too early.
    double effectiveAverages = 0.0;

    /// What was compensated BEFORE the transform, carried so a readout can
    /// state it. Not applied to the phase after the fact -- that is the
    /// mistake dual-FFT record section 4 exists to refuse.
    int appliedDelaySamples = 0;
};

/// One band of the multi-time-window engine's own layout, in the app's own
/// units. Mirrors `rta::dsp::MtwBand` (core/include/rta/dsp/MtwLayout.h) but
/// carries only what a view needs to draw a seam and a per-band readout --
/// never the FFT internals (`hopSize`, `firstBin`, `lastBin`) a drawing layer
/// has no use for.
struct MtwBandDescriptor {
    std::size_t firstIndex = 0;
    std::size_t pointCount = 0;
    std::size_t fftSize = 0;
    /// fftSize / fs -- the window's own length.
    float windowSeconds = 0.0f;
    /// fifoDepth * hopSize / fs. NOT windowSeconds, and NOT the same in every
    /// band: record §5 buys uniform effectiveAverages with non-uniform
    /// seconds, 85 ms at the top and 5.461 s below 187.5 Hz, and the view
    /// must print which -- a coherence trace that fills in from the top over
    /// five seconds is read as a fault unless the number is on screen.
    float integrationSeconds = 0.0f;
    /// 8.5866271 in EVERY band once filled (record §5, conflict C3): uniform
    /// Neff bought with non-uniform integrationSeconds above.
    double effectiveAverages = 0.0;
    /// The lower edge of this band's owned octave, and therefore the seam
    /// the view draws. 0 for the bottom band, which has no lower neighbour.
    float seamHz = 0.0f;
    /// False when this band has not passed its own gate. Absence of
    /// coherence is PER BAND here, not per block: the bottom band can still
    /// be filling while the top has been measuring for seconds (record §5,
    /// conflict C3). A single std::optional on the block would force the
    /// whole curve to read coherence-less because of the slowest band.
    bool coherenceAvailable = false;
};

/// The multi-time-window transfer function, stitched into one curve with an
/// EXPLICIT frequency vector -- unlike `TransferBlock`, whose axis is
/// `i * sampleRate/fftSize` and needs nothing else. Record §6
/// (docs/dsp/2026-09-05-mtw-l3.md): `Trace` derives its own axis from
/// `fftSize` on purpose, so this block is LIVE-ONLY until an L5 amendment
/// gives storage a frequency vector of its own.
struct MtwBlock {
    std::vector<double> frequencyHz;   ///< strictly increasing; [0] is DC (0 Hz)
    std::vector<float> magnitudeDb;
    std::vector<float> phaseDeg;       ///< the same radians->degrees crossing TransferBlock uses
    /// Meaningful only where the OWNING band's own descriptor says so
    /// (`bands[b].coherenceAvailable`) -- record §5's per-band fill time, not
    /// a per-block gate. An index whose band has not yet passed the gate
    /// holds 0.0f here and must not be read as a measured zero.
    std::vector<float> coherence;
    std::vector<MtwBandDescriptor> bands;  ///< ascending in frequency
    int appliedDelaySamples = 0;
};

/// The stitched spatial average (task B3, record §6), the app's own
/// conversion of `rta::dsp::SpatialAverageResult` -- same one radians ->
/// degrees crossing `TransferBlock` makes, for the same reason (`view/`
/// works in degrees throughout). Absent exactly when
/// `rta::dsp::spatialAverage` returned `std::nullopt`: every position's
/// weights summed to zero at every bin, which is a state worth showing as
/// "no average", never as a flat curve of zeros.
struct AverageBlock {
    std::vector<float> magnitudeDb;
    std::vector<float> phaseDeg;
    std::vector<float> phaseAgreement;         ///< R, record §2 -- NOT a coherence estimate
    std::vector<float> weightedCoherence;      ///< Sum(u*g2)/Sum(u), record §4 -- NOT one either
    std::vector<std::uint16_t> contributors;   ///< gate-passed AND u_i > 0 at this bin
    /// `rta::dsp::SpatialAbsence` per bin, stored as the same enum: Present /
    /// NoContributor / NoWeight (record §3). Kept as the core enum rather
    /// than re-declared here -- this file already links rta::core through
    /// Analyser.h's own dependency chain, so there is no framework-guard
    /// reason to duplicate it.
    std::vector<rta::dsp::SpatialAbsence> absence;
};

/// Why a route's `PositionSummary` reads the way it does -- station-4 fix F3
/// (docs/dsp/2026-09-06-multichannel-l6b.md §6: "a spatial average group
/// requires its members to share one reference channel, and the group
/// refuses a member that does not"). `Member` is the accepted case, mirroring
/// `AverageGroup`'s own `MemberRefusal::None` one layer down
/// (measure/AverageGroup.h) without this framework-free header including
/// that one (AverageGroup.h already includes THIS file -- the dependency
/// only goes one way). Every other value mirrors a real refusal reason
/// `AverageGroup::addMember` actually returned for this route; there is no
/// value here that is not backed by a real refusal, because a state nothing
/// in the code can produce is a claim the enum would be making on its own.
enum class Membership { Member, ExcludedDifferentReference };

/// One group member's summary -- level, trust, gate state -- with NO per-bin
/// array (record §6: publish cost must be O(1) in N, and a per-bin array
/// here is exactly the N-scaling churn that decision refuses). The full
/// per-bin blocks exist only for the group's average and for at most one
/// SOLOED member (see `Snapshot::soloTransfer` below).
///
/// Built for EVERY route in the routing plan, not only the ones that joined
/// the live average -- a route refused for naming a different reference
/// still gets one, with `membership` stating why it contributes nothing to
/// `Snapshot::average`, rather than vanishing from the Snapshot entirely
/// (the silent drop station-4 fix F3 closed; see
/// AnalysisPublish.cpp's `mergeRoutePositions`).
struct PositionSummary {
    int tfIndex = -1;
    std::string name;
    /// A single scalar standing in for the whole magnitude curve -- the
    /// arithmetic mean of `TransferBlock::magnitudeDb`, in dB, over every
    /// bin (including floored ones: a position sitting mostly at the floor
    /// should read as quiet, not be excluded from its own average).
    float levelDb = static_cast<float>(kLevelFloorDb);
    /// The mean of this position's OWN gated coherence, over bins where it
    /// is present -- not `AverageBlock::weightedCoherence`, which is a
    /// property of the GROUP, not of one member.
    float weightedCoherence = 0.0f;
    double effectiveAverages = 0.0;
    bool gatePassed = false;   ///< this position's own coherence.has_value()
    /// Latched by the capture sequencer (task B5); B3 never sets this true --
    /// there is no overload check upstream of AverageGroup yet, and a
    /// summary must not claim a fact nothing has measured.
    bool overloaded = false;
    /// Defaults to `Member` because every summary `AverageGroup::publish()`
    /// itself produces IS one (its `members_` list holds only accepted
    /// members by construction) -- only `mergeRoutePositions`'s synthesized
    /// entry for a route `AverageGroup::addMember` actually refused sets
    /// this to anything else.
    Membership membership = Membership::Member;
};

/// One immutable measurement, published by the analysis thread and read by
/// the message thread through an atomic pointer swap (decision record: "one
/// struct, published by atomic pointer swap"; trap T-5).
///
/// Deliberately carries NO timestamp: two snapshots built from the same
/// input must compare equal field-for-field, which is what makes
/// `makeSyntheticSnapshot` deterministic and `rta-view.png` reviewable as a
/// byte-for-byte diff (plan §1.4, §5.6).
struct Snapshot {
    /// Increments by exactly one on every `Analyser::publish`. The only
    /// field a reader needs to notice "there is something new" without
    /// comparing pointers.
    std::uint64_t sequence = 0;

    double sampleRate = 0.0;
    std::size_t fftSize = 0;
    int fraction = 3;

    std::vector<BandReading> bands;

    /// The raw per-bin spectrum, dBFS, `fftSize/2 + 1` long -- the trace a
    /// spectrum overlay reads, as opposed to the banded `bands` a bar chart
    /// reads. Optional in the sense that a caller uninterested in it can
    /// ignore the vector; it is always populated by `Analyser::publish`.
    std::vector<float> spectrumDb;

    /// Absent when no reference was fed. A single-channel capture has no
    /// transfer function; it does not have a flat one.
    std::optional<TransferBlock> transfer;

    /// The multi-time-window transfer function, absent under the exact same
    /// condition as `transfer` (no reference fed / MTW disabled) -- see
    /// `MtwBlock`'s own comment for why it carries its own frequency vector
    /// instead of reusing `fftSize`.
    std::optional<MtwBlock> mtw;

    bool hasReference = false;
    std::vector<BandReading> referenceBands;

    std::uint64_t framesAnalysed = 0;
    /// Carried straight from `Analyser::publish`'s argument -- the audio
    /// callback's drop count as of this snapshot, so a drop the analysis
    /// thread never asked about still reaches the UI (plan §5.5).
    std::uint64_t droppedSamples = 0;

    float peakBandLevelDb = static_cast<float>(kLevelFloorDb);
    float peakBandCentreHz = 0.0f;

    /// Task B3 (record §6): the group's own spatial average, absent when no
    /// group is configured or `rta::dsp::spatialAverage` itself returned
    /// nothing (every position's weights summed to zero everywhere).
    std::optional<AverageBlock> average;

    /// One summary per group member, always -- this is the O(1)-in-N part
    /// of the publish (record §6's own cost argument, task B3(d)'s counting
    /// allocator). Empty when no group is configured.
    std::vector<PositionSummary> positions;

    /// The full per-bin transfer function of at most ONE soloed position --
    /// `nullopt` unless the operator asked for one. This, plus `average`
    /// above, is the ONLY per-bin data an N-member group publishes; every
    /// other position's curve is summarised in `positions`, not carried in
    /// full (record §6: "the spatial average is the published trace, plus
    /// one soloed position").
    std::optional<TransferBlock> soloTransfer;
};

/// Readers only ever see a `const Snapshot`: nothing downstream of
/// `Analyser::publish` is allowed to mutate a snapshot in place, which is
/// what makes handing the same pointer to two frames of drawing safe with
/// no lock.
using SnapshotPtr = std::shared_ptr<const Snapshot>;

}  // namespace rta::measure
