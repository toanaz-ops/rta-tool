// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The device-free snapshot both API serialiser suites read, and the golden
// file's own subject. No sound card, no microphone, no thread, no device.
//
// The composition is `tools/snapshot.cpp:181-190`'s, lifted here rather than
// re-invented: makeSyntheticSnapshot for bands and spectrum, then
// makeSyntheticTransfer, makeSyntheticMtw and makeSyntheticAverage. Every
// one of those is documented bit-identical for a given spec
// (app/src/measure/SyntheticSnapshot.h:44 and the comments at :57, :77, :90)
// -- the same property that makes rta-view.png reviewable as a byte-for-byte
// diff, and the precondition for a golden JSON file being a lock rather than
// a record of one machine's mood.
#pragma once

#include "measure/Snapshot.h"
#include "measure/SyntheticSnapshot.h"

#include <cstddef>
#include <memory>
#include <utility>

namespace rta::api::test {

/// The four-position group the specimen renders, at the default spec. The
/// delay of 18 samples and the position count of 4 are tools/snapshot.cpp's
/// own figures, kept so the API golden and the picture describe the same
/// synthetic measurement.
[[nodiscard]] inline measure::Snapshot makeApiFixture() {
    const measure::SyntheticSpec spec;
    measure::Snapshot snapshot = *measure::makeSyntheticSnapshot(spec);
    snapshot.transfer = measure::makeSyntheticTransfer(snapshot.fftSize, snapshot.sampleRate, 18);
    snapshot.mtw = measure::makeSyntheticMtw();
    auto [average, positions] =
        measure::makeSyntheticAverage(snapshot.fftSize, snapshot.sampleRate, 4);
    snapshot.average = std::move(average);
    snapshot.positions = std::move(positions);
    snapshot.hasReference = true;
    // A sequence a reader can recognise in the golden, and one large enough
    // that a 32-bit truncation somewhere would be visible rather than
    // plausible.
    snapshot.sequence = 12345;
    return snapshot;
}

/// The same fixture with every optional block ABSENT. Absence is a state
/// this wire format has to carry, not a case to skip: `transfer->coherence`
/// is an optional for a stated reason, and a snapshot with no reference fed
/// does not have a flat transfer function -- it has none.
[[nodiscard]] inline measure::Snapshot makeEmptyApiFixture() {
    const measure::SyntheticSpec spec;
    measure::Snapshot snapshot = *measure::makeSyntheticSnapshot(spec);
    snapshot.sequence = 12345;
    snapshot.hasReference = false;
    return snapshot;
}

}  // namespace rta::api::test
