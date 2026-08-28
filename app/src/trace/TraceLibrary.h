// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/trace. No JUCE: enforced by the
// measure_has_no_framework_deps ctest. See
// docs/specs/2026-08-28-trace-library-and-session.md.
#pragma once

#include "trace/SessionCodec.h"
#include "trace/Trace.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rta::trace {

/// How the user has organised the traces they have captured: names, groups,
/// visibility, and the shade cycling order within a group. `LibraryEntry` is
/// shared with SessionCodec.h on purpose (see that header) -- the library's
/// live state and a session file's saved entries are the same shape, so there
/// is exactly one place that shape can drift out from under the other.
///
/// `revision()` exists so a caller (Task 6's cached plot image) can ask
/// "has anything changed?" in O(1) instead of diffing every entry on every
/// frame. That guarantee only holds if a call that changes nothing never
/// advances it -- see the .cpp for how each setter honours that.
class TraceLibrary {
public:
    /// Takes the id from `trace.meta().id` rather than generating one: the
    /// capture already has an identity, and a second one invented here would
    /// just be a second name for the same thing. Refuses (returns "", leaves
    /// the library and revision untouched) when that id is already present --
    /// two entries for one id would make entry()/trace() ambiguous. On
    /// success, assigns the lowest shade index not already used by another
    /// trace in `group` and returns the id that was stored.
    std::string add(Trace trace, std::string name, std::string group);

    /// Every setter below: unknown id -> returns false, nothing changes,
    /// revision does not move. Known id but the value already matches ->
    /// returns true (the state is now as asked, trivially), revision still
    /// does not move. Known id with a real change -> applies it and advances
    /// revision by one.
    [[nodiscard]] bool rename(const std::string& id, std::string name);
    [[nodiscard]] bool setVisible(const std::string& id, bool visible);
    [[nodiscard]] bool setGroup(const std::string& id, std::string group);
    [[nodiscard]] bool setShadeIndex(const std::string& id, int shadeIndex);

    /// Makes `id` the only visible trace. One user action, so at most one
    /// revision step regardless of how many entries flip -- and none at all
    /// if `id` was already the sole visible trace.
    void soloOnly(const std::string& id);

    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

    [[nodiscard]] const LibraryEntry* entry(const std::string& id) const noexcept;
    [[nodiscard]] const Trace* trace(const std::string& id) const noexcept;
    [[nodiscard]] std::span<const LibraryEntry> entries() const noexcept { return entries_; }

private:
    [[nodiscard]] LibraryEntry* findEntry(const std::string& id) noexcept;
    [[nodiscard]] std::size_t indexOf(const std::string& id) const noexcept;
    [[nodiscard]] int nextFreeShadeIndex(const std::string& group) const;

    // Parallel to entries_ by index: entries_[i] describes traces_[i]. Kept as
    // two vectors rather than one struct-of-both because SessionCodec already
    // owns LibraryEntry's shape (spec note above) and Trace is not copyable
    // cheaply enough to want it duplicated into a wrapper struct.
    std::vector<LibraryEntry> entries_;
    std::vector<Trace> traces_;
    std::uint64_t revision_ = 0;
};

}  // namespace rta::trace
