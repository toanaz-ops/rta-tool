// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/trace. No JUCE: std::filesystem only, enforced
// by the measure_has_no_framework_deps ctest. See spec §3.
#include "trace/SessionStore.h"

#include <fstream>

namespace rta::trace {
namespace {

constexpr const char* kIndexName = "session.index";
constexpr const char* kIndexTmpName = "session.index.tmp";
constexpr const char* kTracesDirName = "traces";

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
    return root / kTracesDirName / (id + ".bin");
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
        if (!out) return StoreStatus::IoError;
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
    std::error_code ec;
    const auto tracesDir = root_ / kTracesDirName;
    std::filesystem::create_directories(tracesDir, ec);
    if (ec) return StoreStatus::IoError;

    const auto path = traceBlobPath(root_, trace.meta().id);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return StoreStatus::IoError;

    const std::vector<std::byte> blob = encodeTraceBlob(trace);
    out.write(reinterpret_cast<const char*>(blob.data()), static_cast<std::streamsize>(blob.size()));
    if (!out) return StoreStatus::IoError;
    return StoreStatus::Ok;
}

StoreStatus SessionStore::readTrace(const CaptureMeta& meta, std::optional<Trace>& out) const {
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
