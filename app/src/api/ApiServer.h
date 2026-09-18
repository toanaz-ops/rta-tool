// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/api. No JUCE, no Qt, no audio-device API, and
// -- read the next paragraph -- NOTHING FROM httplib EITHER.
// See docs/dsp/2026-09-16-remote-api.md sec.4, sec.8, sec.9, sec.10 as
// amended by sec.15 (R1, R15, R16a).
#pragma once

#include "api/ApiSettings.h"

#include <memory>
#include <optional>
#include <string>

namespace rta::measure {
struct SnapshotSource;
}   // namespace rta::measure

namespace rta::api {

/// The read-only remote API's HTTP listener: one `std::thread`, one
/// `httplib::Server`, eight GET routes.
///
/// THIS HEADER IS A PIMPL AND THE GUARD IS WHY. A by-value `httplib::Server`
/// member would force `#include <httplib.h>` into this file, and
/// `core/tests/check_no_server_library.cmake` scans `app/**/*.h` with an
/// ALLOW naming `ApiServer.cpp` and nothing else -- so a CORRECT build would
/// turn that guard red. That is a guard punishing the design rather than
/// protecting it, and the fix is one line of C++ rather than a weaker guard.
/// `grep -nE '^[ \t]*#[ \t]*include.*httplib' app/src/api/ApiServer.h` must
/// come back empty. Not `grep httplib` -- this comment names the library
/// several times on purpose, and the guard is anchored on the INCLUDE
/// DIRECTIVE for exactly that reason: a comment that merely discusses
/// cpp-httplib is fine, and only a literal include trips it.
///
/// The destructor is declared here and DEFINED OUT OF LINE. That is
/// required, not stylistic: `std::unique_ptr<Impl>` cannot be destroyed where
/// `Impl` is incomplete, and letting the compiler generate it in this header
/// is exactly the mistake that drags the include back in.
///
/// A `std::thread`, not a `juce::Thread` (sec.15 R15). CI has one job,
/// configured `RTA_BUILD_APP=OFF` on three operating systems, and there is no
/// ON job anywhere in the repository -- so a JUCE thread here would have left
/// the entire network layer, including sec.9's "highest-value control in the
/// whole API", proven on zero CI machines.
///
/// THE REAL-TIME RULE THIS CLASS HOLDS, and it is the one a maintainer can
/// break. Each request does exactly ONE `SnapshotSource::latest()`, takes the
/// `shared_ptr<const Snapshot>` copy, and serialises from that copy WITH THE
/// SLOT RELEASED.
///
/// **Stated precisely, because the loose version of this sentence is wrong.**
/// This thread never holds a lock ACROSS serialisation -- the expensive work,
/// thousands of floats into JSON, happens entirely after the load and touches
/// no shared state. It is NOT true that it never blocks the analysis thread:
/// `AtomicSharedPtr` is **not lock-free on this project's own toolchain** (its
/// class comment records the measurement on MSVC 14.51), so the atomic load
/// itself can contend with the publish. The bound on that contention is
/// arithmetic, not structural -- at most `maxRequestsPerSecond` loads per
/// second, which is why the rate limiter is a real-time-safety control and
/// not hygiene, and why it sits immediately before the load rather than
/// anywhere after it.
///
/// Nothing test-visible catches a second `latest()` inside one handler --
/// that constraint is held by review and by this comment, and saying so is
/// better than leaving a reader to assume a test exists.
class ApiServer {
public:
    /// Binds ON THIS THREAD and only then starts the server thread, so
    /// `boundPort()` is already valid when the constructor returns -- see its
    /// own comment. `source` must outlive this object; the composition root
    /// guarantees that by declaration order.
    ///
    /// Starts nothing at all when `settings.enabled` is false (the shipped
    /// default) or when `startRefusal(settings)` names a reason. Neither is
    /// an error: nothing observable changes for an operator who did not ask
    /// for the API, and a refusal is reported through `refusal()` rather than
    /// thrown, because a composition root that cannot start a read-only
    /// listener still has an application to run.
    ApiServer(measure::SnapshotSource& source, ApiSettings settings);

    ~ApiServer();

    ApiServer(const ApiServer&) = delete;
    ApiServer& operator=(const ApiServer&) = delete;
    ApiServer(ApiServer&&) = delete;
    ApiServer& operator=(ApiServer&&) = delete;

    /// True when this object owns a bound listening socket and a live thread.
    /// False before a successful bind, after the destructor's stop, and for
    /// every disabled or refused configuration.
    [[nodiscard]] bool running() const noexcept;

    /// The port actually bound, or -1 when nothing was. **This accessor
    /// exists because httplib does not have one:**
    /// `Server::bind_to_port(host, port, flags)` returns `bool` and discards
    /// the port, and `Server` has no `port()` member at all -- that one is on
    /// `Client`. The call that returns a port is
    /// `Server::bind_to_any_port(host, flags)`, which is what a
    /// `settings.port == 0` configuration uses.
    [[nodiscard]] int boundPort() const noexcept;

    /// The named reason the server did not start, when `startRefusal` gave
    /// one, or `nullopt`. A setting that silently did nothing and a setting
    /// that silently did it are the two failure modes `allowLanBind` exists
    /// to refuse, so the refusal has to be readable from outside.
    [[nodiscard]] const std::optional<std::string>& refusal() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rta::api
