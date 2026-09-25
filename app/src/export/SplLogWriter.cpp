// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/export. No JUCE: enforced by the
// measure_has_no_framework_deps ctest.
// Lane L6a task W2-C (record docs/dsp/2026-09-16-spl-pro-l6a.md §10, §13 Q7).
//
// The ONLY file in this pair that opens a stream -- the FirTextWriter.cpp
// precedent. Every number it writes comes from SplLog.h's pure functions;
// this file only decides WHICH FILE and WHEN TO ROTATE.
//
// THE APP NEVER DELETES (record §13 Q7): this file deliberately calls no
// filesystem deletion function of any kind, and that absence is itself the
// acceptance C5 checks structurally (a grep for the literal call spellings,
// which this paragraph avoids repeating for exactly that reason).
#include "export/SplLog.h"

namespace rta::splexport {

SplLogWriter::SplLogWriter(std::string basePath, rta::measure::SplConfig config,
                          SplLogHeaderInfo info, std::uint64_t segmentBlocks)
    : basePath_(std::move(basePath)), config_(std::move(config)), info_(std::move(info)),
      segmentBlocks_(segmentBlocks) {
    openSegment();
}

void SplLogWriter::openSegment() {
    if (stream_.is_open()) stream_.close();

    // gen<N>.seg<M>.csv: the generation bumps on `reconfigure` (a NEW log,
    // C4), the segment bumps on rotation within the SAME log (C5). Neither
    // ever reuses a path a previous segment already wrote, so nothing here
    // is ever opened for anything but a fresh, empty file.
    std::string path = basePath_ + ".gen" + std::to_string(logGeneration_) + ".seg" +
                        std::to_string(segmentIndex_) + ".csv";

    stream_.open(path, std::ios::out | std::ios::trunc);
    stream_ << logHeader(config_, info_);
    stream_ << csvHeaderRow() << '\n';
    stream_.flush();

    segmentPaths_.push_back(path);
    blocksInSegment_ = 0;
}

void SplLogWriter::write(const rta::meter::Block& block) {
    if (blocksInSegment_ >= segmentBlocks_) {
        ++segmentIndex_;
        openSegment();
    }
    stream_ << logRow(block, config_.referenceOffsetDb);
    stream_.flush();
    ++blocksInSegment_;
}

void SplLogWriter::reconfigure(rta::dsp::WeightingType weighting, rta::meter::TimeWeighting detector) {
    if (weighting == info_.weighting && detector == info_.detector) return;  // nothing changed

    info_.weighting = weighting;
    info_.detector = detector;
    ++logGeneration_;
    segmentIndex_ = 0;
    openSegment();  // a BRAND NEW log: fresh header, fresh CSV header row
}

}  // namespace rta::splexport
