// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/trace. No JUCE: enforced by the
// measure_has_no_framework_deps ctest. The binary trace-blob half of the
// session codec, split out of SessionCodec.cpp to keep both files under the
// 300-line house limit.
#include "trace/SessionCodec.h"

#include <cstdint>
#include <cstring>

namespace rta::trace {
namespace {

constexpr std::uint32_t kMagnitudeBit = 1u << 0;
constexpr std::uint32_t kPhaseBit = 1u << 1;
constexpr std::uint32_t kCoherenceBit = 1u << 2;

// Little-endian by construction, not by relying on host byte order -- the
// format must read back the same on a big-endian host too.
void appendU32(std::vector<std::byte>& out, std::uint32_t v) {
    for (int shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<std::byte>((v >> shift) & 0xFFu));
    }
}

bool readU32(std::span<const std::byte>& in, std::uint32_t& v) {
    if (in.size() < 4) return false;
    v = 0;
    for (int i = 0; i < 4; ++i) {
        v |= static_cast<std::uint32_t>(in[static_cast<std::size_t>(i)]) << (8 * i);
    }
    in = in.subspan(4);
    return true;
}

void appendFloats(std::vector<std::byte>& out, std::span<const float> values) {
    auto bytes = std::as_bytes(values);
    out.insert(out.end(), bytes.begin(), bytes.end());
}

bool readFloats(std::span<const std::byte>& in, std::size_t count, std::vector<float>& out) {
    std::size_t byteCount = count * sizeof(float);
    if (in.size() < byteCount) return false;
    out.resize(count);
    std::memcpy(out.data(), in.data(), byteCount);
    in = in.subspan(byteCount);
    return true;
}

}  // namespace

std::vector<std::byte> encodeTraceBlob(const Trace& trace) {
    std::vector<std::byte> out;
    const char magic[4] = {'R', 'T', 'A', 'T'};
    for (char c : magic) out.push_back(static_cast<std::byte>(c));

    appendU32(out, static_cast<std::uint32_t>(trace.pointCount()));

    std::uint32_t presence = 0;
    if (trace.has(Field::Magnitude)) presence |= kMagnitudeBit;
    if (trace.has(Field::Phase)) presence |= kPhaseBit;
    if (trace.has(Field::Coherence)) presence |= kCoherenceBit;
    appendU32(out, presence);

    const std::string& id = trace.meta().id;
    appendU32(out, static_cast<std::uint32_t>(id.size()));
    for (char c : id) out.push_back(static_cast<std::byte>(c));

    // Field order fixed here, matching the presence-bit order, so
    // decodeTraceBlob can read them back without re-deriving the order.
    if (trace.has(Field::Magnitude)) appendFloats(out, trace.field(Field::Magnitude));
    if (trace.has(Field::Phase)) appendFloats(out, trace.field(Field::Phase));
    if (trace.has(Field::Coherence)) appendFloats(out, trace.field(Field::Coherence));

    return out;
}

DecodeStatus decodeTraceBlob(std::span<const std::byte> blob, const CaptureMeta& meta,
                              std::optional<Trace>& out) {
    if (blob.size() < 4 || std::memcmp(blob.data(), "RTAT", 4) != 0) {
        return DecodeStatus::Malformed;
    }
    blob = blob.subspan(4);

    std::uint32_t pointCount = 0;
    if (!readU32(blob, pointCount)) return DecodeStatus::Malformed;
    if (pointCount != pointCountFor(meta.fftSize)) return DecodeStatus::Malformed;

    std::uint32_t presence = 0;
    if (!readU32(blob, presence)) return DecodeStatus::Malformed;

    std::uint32_t idLen = 0;
    if (!readU32(blob, idLen)) return DecodeStatus::Malformed;
    if (blob.size() < idLen) return DecodeStatus::Malformed;
    std::string id(reinterpret_cast<const char*>(blob.data()), idLen);
    blob = blob.subspan(idLen);

    // A blob paired with the wrong metadata is a plausible wrong measurement,
    // not merely a format error -- refuse it rather than build a mismatched
    // Trace (spec: decodeTraceBlob cross-checks the id).
    if (id != meta.id) return DecodeStatus::Malformed;

    if (!(presence & kMagnitudeBit)) return DecodeStatus::Malformed;  // make() requires magnitude.

    std::vector<float> magnitude, phase, coherence;
    if (!readFloats(blob, pointCount, magnitude)) return DecodeStatus::Malformed;

    auto trace = Trace::make(meta, std::move(magnitude));
    if (!trace.has_value()) return DecodeStatus::Malformed;

    if (presence & kPhaseBit) {
        if (!readFloats(blob, pointCount, phase)) return DecodeStatus::Malformed;
        if (!trace->setPhase(std::move(phase))) return DecodeStatus::Malformed;
    }
    if (presence & kCoherenceBit) {
        if (!readFloats(blob, pointCount, coherence)) return DecodeStatus::Malformed;
        if (!trace->setCoherence(std::move(coherence))) return DecodeStatus::Malformed;
    }

    out = std::move(trace);
    return DecodeStatus::Ok;
}

}  // namespace rta::trace
