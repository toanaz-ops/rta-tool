// SPDX-License-Identifier: AGPL-3.0-or-later
#include "trace/TraceLibrary.h"

namespace rta::trace {

std::size_t TraceLibrary::indexOf(const std::string& id) const noexcept {
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].traceId == id) return i;
    }
    return entries_.size();
}

LibraryEntry* TraceLibrary::findEntry(const std::string& id) noexcept {
    const std::size_t i = indexOf(id);
    return i < entries_.size() ? &entries_[i] : nullptr;
}

const LibraryEntry* TraceLibrary::entry(const std::string& id) const noexcept {
    const std::size_t i = indexOf(id);
    return i < entries_.size() ? &entries_[i] : nullptr;
}

const Trace* TraceLibrary::trace(const std::string& id) const noexcept {
    const std::size_t i = indexOf(id);
    return i < entries_.size() ? &traces_[i] : nullptr;
}

int TraceLibrary::nextFreeShadeIndex(const std::string& group) const {
    // Lowest non-negative integer not already used by another trace in this
    // group -- a plain "used" flag per candidate index, grown lazily so a
    // group with two traces never scans past index 2.
    std::vector<bool> used;
    for (const auto& e : entries_) {
        if (e.group != group || e.shadeIndex < 0) continue;
        const auto idx = static_cast<std::size_t>(e.shadeIndex);
        if (idx >= used.size()) used.resize(idx + 1, false);
        used[idx] = true;
    }
    for (std::size_t i = 0; i < used.size(); ++i) {
        if (!used[i]) return static_cast<int>(i);
    }
    return static_cast<int>(used.size());
}

std::string TraceLibrary::add(Trace newTrace, std::string name, std::string group) {
    const std::string id = newTrace.meta().id;
    if (findEntry(id) != nullptr) return {};  // duplicate id: refused, nothing moves

    const int shade = nextFreeShadeIndex(group);
    entries_.push_back(LibraryEntry{id, std::move(name), std::move(group), shade, true});
    traces_.push_back(std::move(newTrace));
    ++revision_;
    return id;
}

bool TraceLibrary::rename(const std::string& id, std::string name) {
    auto* e = findEntry(id);
    if (!e) return false;
    if (e->name == name) return true;  // no-op: revision must not move
    e->name = std::move(name);
    ++revision_;
    return true;
}

bool TraceLibrary::setVisible(const std::string& id, bool visible) {
    auto* e = findEntry(id);
    if (!e) return false;
    if (e->visible == visible) return true;
    e->visible = visible;
    ++revision_;
    return true;
}

bool TraceLibrary::setGroup(const std::string& id, std::string group) {
    auto* e = findEntry(id);
    if (!e) return false;
    if (e->group == group) return true;
    e->group = std::move(group);
    ++revision_;
    return true;
}

bool TraceLibrary::setShadeIndex(const std::string& id, int shadeIndex) {
    auto* e = findEntry(id);
    if (!e) return false;
    if (e->shadeIndex == shadeIndex) return true;
    e->shadeIndex = shadeIndex;
    ++revision_;
    return true;
}

void TraceLibrary::soloOnly(const std::string& id) {
    // An id that no longer exists (its trace was just removed elsewhere) must
    // not blank the whole plot: refuse exactly like every other setter does
    // for an unknown id, rather than "helpfully" hiding everything because
    // nothing matches.
    if (findEntry(id) == nullptr) return;

    // One sweep, one possible bump: the whole operation is a single user
    // action, however many entries end up flipping visibility.
    bool changed = false;
    for (auto& e : entries_) {
        const bool shouldBeVisible = (e.traceId == id);
        if (e.visible != shouldBeVisible) {
            e.visible = shouldBeVisible;
            changed = true;
        }
    }
    if (changed) ++revision_;
}

}  // namespace rta::trace
