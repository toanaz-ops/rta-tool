// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/trace. No JUCE: std::filesystem only, enforced
// by the measure_has_no_framework_deps ctest. See spec §3.
#include "trace/SessionStore.h"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace rta::trace {
namespace {

constexpr const char* kIndexName = "session.index";
constexpr const char* kIndexTmpName = "session.index.tmp";
constexpr const char* kTracesDirName = "traces";
constexpr const char* kTraceBlobExt = ".bin";
constexpr const char* kTraceBlobTmpExt = ".bin.tmp";

// D9 (HUMAN-QA-QUEUE): `id` reaches `traceBlobPath` as a bare `std::string`
// concatenated straight into a `path` via `operator/` -- a string containing
// '/' or '\\' is not one path COMPONENT to `operator/`, it is more path
// structure, so an id like "../../evil" or "sub/evil" escapes `traces/`
// entirely rather than merely producing an odd filename inside it. Bounded
// to the POSIX "portable filename character set" minus '.' (so ".."
// specifically can never reach `traceBlobPath` even with no separator in
// it) -- every id this app writes today already satisfies this:
// `nextCaptureId()` (CaptureConverter.cpp) always produces
// "capture-<uint64_t>" and Open only ever round-trips an id a PREVIOUS Save
// wrote (itself always `nextCaptureId()`-shaped, or another build's
// equivalent of it).
constexpr std::size_t kMaxTraceIdLength = 128;  // generous against
                                                 // "capture-<uint64_t>" (<=
                                                 // ~26 chars); just a ceiling
                                                 // against an absurd id, not
                                                 // a tight fit to today's format.

bool isValidTraceId(const std::string& id) {
    if (id.empty() || id.size() > kMaxTraceIdLength) return false;
    return std::all_of(id.begin(), id.end(), [](unsigned char c) {
        return std::isalnum(c) != 0 || c == '_' || c == '-';
    });
}

/// The codec's DecodeStatus and the store's StoreStatus overlap on purpose --
/// this is the one place that translation happens, so callers never see the
/// codec's enum (brief: "map DecodeStatus onto StoreStatus rather than
/// leaking the codec's enum").
StoreStatus fromDecodeStatus(DecodeStatus status) {
    switch (status) {
        case DecodeStatus::Ok: return StoreStatus::Ok;
        case DecodeStatus::Malformed: return StoreStatus::Malformed;
        case DecodeStatus::NewerSchema: return StoreStatus::NewerSchema;
    }
    return StoreStatus::Malformed;
}

std::filesystem::path traceBlobPath(const std::filesystem::path& root, const std::string& id) {
    return root / kTracesDirName / (id + kTraceBlobExt);
}

std::filesystem::path traceBlobTmpPath(const std::filesystem::path& root, const std::string& id) {
    return root / kTracesDirName / (id + kTraceBlobTmpExt);
}

}  // namespace

StoreStatus SessionStore::writeIndex(const SessionDocument& doc) const {
    std::error_code ec;
    std::filesystem::create_directories(root_, ec);
    if (ec) return StoreStatus::IoError;

    const auto tmpPath = root_ / kIndexTmpName;
    const auto finalPath = root_ / kIndexName;

    // Stamp the CURRENT schema on every write, unconditionally. A document
    // read back from a v1 file keeps schemaVersion == 1 in memory; without
    // this stamp, saving it after adding panes would write a v1 file
    // carrying v2 content -- the one file this change must never produce,
    // because an older build meeting that [pane] section would report
    // Malformed ("your session is corrupt", a lie) instead of NewerSchema
    // ("this needs a newer version", true). Deliberately NOT done inside
    // encodeIndex: test_session_codec.cpp encodes a document with a
    // deliberately-future schemaVersion to exercise decodeIndex's refusal
    // path, and stamping in the codec would silently rewrite that field out
    // from under the test.
    SessionDocument stamped = doc;
    stamped.schemaVersion = kSchemaVersion;

    // Binary mode so the bytes written are exactly what encodeIndex produced
    // -- no CRLF translation to reason about on top of the format's own
    // newline escaping.
    {
        std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
        if (!out) return StoreStatus::IoError;
        const std::string encoded = encodeIndex(stamped);
        out.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
        // Same reasoning as writeTrace: a flush failure surfaces at close(),
        // and must stop the rename below from committing a truncated index.
        out.close();
        if (out.fail()) return StoreStatus::IoError;
    }

    // The rename is the atomic step: a crash before it leaves only a stray
    // .tmp beside an untouched, still-valid index (readIndex never looks at
    // the .tmp). A crash after it has already committed the new index.
    std::filesystem::rename(tmpPath, finalPath, ec);
    if (ec) return StoreStatus::IoError;
    return StoreStatus::Ok;
}

StoreStatus SessionStore::readIndex(SessionDocument& out) const {
    const auto path = root_ / kIndexName;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return StoreStatus::NotFound;

    std::ifstream in(path, std::ios::binary);
    if (!in) return StoreStatus::NotFound;
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    return fromDecodeStatus(decodeIndex(content, out));
}

StoreStatus SessionStore::writeTrace(const Trace& trace) const {
    // D9: reject before any path is formed -- traceBlobPath/traceBlobTmpPath
    // below both concatenate this id straight into a filesystem path.
    if (!isValidTraceId(trace.meta().id)) return StoreStatus::Malformed;

    std::error_code ec;
    const auto tracesDir = root_ / kTracesDirName;
    std::filesystem::create_directories(tracesDir, ec);
    if (ec) return StoreStatus::IoError;

    // F5: write to a tmp file, then rename over the final blob -- the same
    // pattern writeIndex already uses for session.index (rename is the
    // atomic step). Unlike writeIndex, a leftover .tmp is deliberately
    // cleaned up on every failure path below: writeIndex's stray-.tmp case
    // is a genuine mid-write crash the NEXT run has to tolerate finding,
    // whereas here `remove` runs synchronously in the same call that
    // detected the failure, so there is no reason to leave the litter
    // behind.
    const auto finalPath = traceBlobPath(root_, trace.meta().id);
    const auto tmpPath = traceBlobTmpPath(root_, trace.meta().id);

    const std::vector<std::byte> blob = encodeTraceBlob(trace);
    {
        std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
        if (!out) {
            std::filesystem::remove(tmpPath, ec);
            return StoreStatus::IoError;
        }
        out.write(reinterpret_cast<const char*>(blob.data()), static_cast<std::streamsize>(blob.size()));
        // close() is checked explicitly (PR #55 verifier LOW): a buffered
        // write's ENOSPC/EIO can surface only at the final flush inside
        // close(), where the destructor would swallow it -- and the
        // truncated tmp would then be renamed over the good blob. close()
        // also releases the handle before remove() on Windows. fail()
        // covers both a failed write() above and a failed close().
        out.close();
        if (out.fail()) {
            std::filesystem::remove(tmpPath, ec);
            return StoreStatus::IoError;
        }
    }

    std::filesystem::rename(tmpPath, finalPath, ec);
    if (ec) {
        std::filesystem::remove(tmpPath, ec);
        return StoreStatus::IoError;
    }
    return StoreStatus::Ok;
}

StoreStatus SessionStore::readTrace(const CaptureMeta& meta, std::optional<Trace>& out) const {
    // D9: same reasoning as writeTrace -- meta.id reaches traceBlobPath the
    // same way, whether it came from a fresh capture or from an on-disk
    // index Open just parsed (a hand-edited or foreign index is exactly the
    // untrusted-id case this guards against).
    if (!isValidTraceId(meta.id)) return StoreStatus::Malformed;

    const auto path = traceBlobPath(root_, meta.id);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return StoreStatus::NotFound;

    std::ifstream in(path, std::ios::binary);
    if (!in) return StoreStatus::NotFound;

    in.seekg(0, std::ios::end);
    const auto size = in.tellg();
    if (size < 0) return StoreStatus::IoError;
    in.seekg(0, std::ios::beg);

    std::vector<std::byte> blob(static_cast<std::size_t>(size));
    if (!blob.empty() && !in.read(reinterpret_cast<char*>(blob.data()), size)) {
        return StoreStatus::IoError;
    }

    return fromDecodeStatus(decodeTraceBlob(blob, meta, out));
}

}  // namespace rta::trace
