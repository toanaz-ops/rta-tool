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
    /// Assigns this instance the next value of a process-wide counter -- see
    /// `generation()`.
    TraceLibrary();

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
    /// if `id` was already the sole visible trace. An unknown id is refused
    /// like every setter above: nothing changes, revision does not move.
    /// A stale id (its trace vanished a moment ago) must not blank every
    /// other trace with no matching entry to explain why.
    void soloOnly(const std::string& id);

    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

    /// A number assigned once, at construction, from a process-wide counter --
    /// distinct from `revision()`, which counts EDITS to one living library and
    /// restarts at 0 for every instance. `StoredTraceLayer` used to key its
    /// cache on `&library`, but a freed library can be replaced by a new one at
    /// the very same address (allocators reuse addresses for same-sized objects
    /// routinely), and a fresh library at revision 0 with the same plot
    /// geometry would then false-hit a stale cached image. A generation counter
    /// that only ever goes up, shared by every instance that has ever existed,
    /// cannot repeat the way an address can.
    [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }

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

    // Assigned in the .cpp from a function-local static counter, not a class
    // static: a function-local static is guaranteed initialised before its
    // first use by the language, with no separate definition to forget in the
    // .cpp the way `inline std::atomic<...> counter_;` would need.
    std::uint64_t generation_;
};

}  // namespace rta::trace
