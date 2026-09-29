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

    /// Rejects `trace.meta().id` outside `[A-Za-z0-9_-]` (non-empty, <= 128
    /// chars) with `StoreStatus::Malformed` before forming any path from it
    /// (D9: `id` is concatenated straight into a filesystem path). Otherwise
    /// writes to `<id>.bin.tmp` then renames over `<id>.bin`, the same
    /// tmp-then-rename pattern `writeIndex` uses, so an interrupted write
    /// leaves the previous blob (if any) whole rather than truncated (F5); a
    /// failed write also removes the `.tmp` it left behind.
    [[nodiscard]] StoreStatus writeTrace(const Trace& trace) const;
    /// Rejects `meta.id` the same way `writeTrace` does (D9) -- an on-disk
    /// index is untrusted input too.
    [[nodiscard]] StoreStatus readTrace(const CaptureMeta& meta,
                                        std::optional<Trace>& out) const;

private:
    std::filesystem::path root_;
};

}  // namespace rta::trace
