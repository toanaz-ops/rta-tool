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
#include "export/SplSessionHeader.h"

#include <exception>
#include <fstream>

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

    // utf8Path(), never stream_.open(path, ...) directly (fix round, PR #43
    // verifier HIGH F1): `path` is UTF-8 (it descends from `basePath_`, which
    // MainComponentSpl.cpp derives from a juce::File), and std::ofstream's
    // std::string overload decodes through the ACTIVE CODE PAGE on MSVC, not
    // UTF-8 -- see SplLog.h's own comment on utf8Path.
    //
    // D6 (HUMAN-QA-QUEUE, fix round): utf8Path() throws on malformed UTF-8
    // (that function's own comment), and this call is on the writer thread
    // (SplLogWriter is never touched from the analysis/audio thread -- record
    // its own real-time-safety note), not the message thread any
    // MainComponent-level try/catch guards -- an uncaught throw here would
    // terminate the whole process, not just this one log. `path` is built one
    // line above from `basePath_`, itself always a `juce::File`-derived,
    // well-formed UTF-8 string, so no input this app produces reaches the
    // throw today (same "zero realistic trigger" the queue's own entry
    // records) -- caught anyway, because the cost of a try/catch here is
    // nothing next to a crash, and it turns a theoretical one into the SAME
    // reported, recoverable failure an unwritable directory already gets two
    // lines down: `writeFailed_` set, this segment skipped, whatever called
    // `write()` keeps running against a stream that silently no-ops rather
    // than unwinding out of the writer thread.
    try {
        stream_.open(utf8Path(path), std::ios::out | std::ios::trunc);
    } catch (const std::exception&) {
        writeFailed_ = true;
        return;
    }
    if (!stream_.is_open()) {
        // Station-4 fix round (PR #31, finding 6): a directory that does not
        // exist or is not writable makes `open()` fail silently -- no
        // exception, no non-zero return, `stream_` just stays in a failed
        // state -- so the next `<<`/`flush()` below are harmless no-ops on a
        // closed stream rather than a crash. Recording that here is the only
        // place the failure is ever visible.
        writeFailed_ = true;
    }
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
    // Station-4 fix round (PR #31, round 3, finding 2): a stream that opened
    // fine can still fail MID-SESSION -- the disk fills, or the underlying
    // handle is closed out from under this writer -- and `<<`/`flush()` on a
    // failed std::ofstream are silent no-ops, exactly like the open failure
    // above. `stream_`'s own bool conversion is `!stream_.fail()`, so this
    // catches both operations in one check without duplicating open's own
    // is_open() test (a stream that failed to WRITE is still "open").
    if (!stream_) writeFailed_ = true;
    ++blocksInSegment_;
}

void SplLogWriter::forceStreamFailureForTest() noexcept {
    stream_.setstate(std::ios::failbit);
}

void SplLogWriter::reconfigure(rta::dsp::WeightingType weighting, rta::meter::TimeWeighting detector) {
    if (weighting == info_.weighting && detector == info_.detector) return;  // nothing changed

    info_.weighting = weighting;
    info_.detector = detector;
    ++logGeneration_;
    segmentIndex_ = 0;
    openSegment();  // a BRAND NEW log: fresh header, fresh CSV header row
}

void writeSessionHeaderFile(const std::string& path, const SplSessionHeaderInfo& info) {
    // Binary, not text mode: text mode's CRLF translation would make the
    // bytes on disk disagree with sessionHeader(info)'s own '\n'-only
    // string, which is exactly the "no atomicity claim, but at least an
    // exact one" property this file's own header rules already rely on
    // (SplLog.h's readLog strips a trailing '\r' for the SAME reason on the
    // read side).
    // utf8Path(), same reason as openSegment() above.
    std::ofstream stream(utf8Path(path), std::ios::out | std::ios::trunc | std::ios::binary);
    stream << sessionHeader(info);
}

}  // namespace rta::splexport
