// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/trace. No JUCE: std::filesystem only, enforced
// by the measure_has_no_framework_deps ctest. See spec §3.
#pragma once

#include "trace/SessionCodec.h"

#include <filesystem>
#include <optional>

namespace rta::trace {

enum class StoreStatus { Ok, NotFound, Malformed, NewerSchema, IoError };

/// A session is a FOLDER, not a container file: capturing during a show appends
/// one blob and rewrites a small index, so an interruption costs one trace
/// rather than the session (spec §3).
class SessionStore {
public:
    explicit SessionStore(std::filesystem::path root) : root_(std::move(root)) {}

    /// Writes to `session.index.tmp` then renames over `session.index`, so an
    /// interrupted write leaves the previous index whole rather than truncated.
    [[nodiscard]] StoreStatus writeIndex(const SessionDocument& doc) const;
    [[nodiscard]] StoreStatus readIndex(SessionDocument& out) const;

    [[nodiscard]] StoreStatus writeTrace(const Trace& trace) const;
    [[nodiscard]] StoreStatus readTrace(const CaptureMeta& meta,
                                        std::optional<Trace>& out) const;

private:
    std::filesystem::path root_;
};

}  // namespace rta::trace
