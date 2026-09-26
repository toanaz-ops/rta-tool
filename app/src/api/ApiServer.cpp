// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/api. See ApiServer.h for the pimpl argument and
// the real-time rule this file holds.
//
// THE ONLY TRANSLATION UNIT IN THIS REPOSITORY THAT INCLUDES <httplib.h>.
// `core/tests/check_no_server_library.cmake` scans core, platform, ui, tools
// and app with an ALLOW naming this file and nothing else, and its second
// sentinel checks that THIS FILE still carries the include -- a guard whose
// sole exception has stopped containing the thing it excepts is a guard
// announcing a proof it no longer has.

#include "api/ApiServer.h"

#include "api/ApiPolicy.h"
#include "api/ApiRoutes.h"
#include "api/ApiSerialise.h"

#include "measure/Snapshot.h"
#include "measure/SnapshotSource.h"

// The vendored header is not ours and is not clean under this project's
// global /W4, so it goes behind a warning barrier rather than by weakening
// the flag for the whole target or marking the include directory SYSTEM.
// MEASURED, not assumed (record sec.15 R7): the OFF build's `warning C` count
// is 0 before this file existed and 0 after, so the `/external:W0` fallback
// was not needed at v0.56.0. The same treatment test_api_schema.cpp already
// gives nlohmann/json.
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <httplib.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

namespace rta::api {
namespace {

using HandlerResponse = httplib::Server::HandlerResponse;

/// The `Allow` field value, spelled ONCE. It appears on a 405 and on every
/// `OPTIONS` response, and PR #18's defect was precisely that the advertised
/// set and the served set had drifted -- OPTIONS was named here and
/// registered nowhere. Two spellings of a method list is two lists that can
/// disagree, so there is one.
///
/// `methodIsAllowed` (ApiPolicy.cpp) is the third party to this agreement and
/// must permit exactly these three. It does.
constexpr const char* kAllowedMethods = "GET, HEAD, OPTIONS";

/// What `Content-Length` declares, clamped into `long long` so
/// `bodyIsAcceptable` -- which takes one deliberately, so a hostile value
/// beyond `INT_MAX` clamps rather than wraps -- sees a number and not an
/// overflow. Absent means zero: a GET should carry no body at all, and a
/// CHUNKED body declares no length, which is what the second cap in
/// configure() is for.
[[nodiscard]] long long declaredBodyBytes(const httplib::Request& request) {
    const auto declared = static_cast<std::uint64_t>(
        request.get_header_value_u64("Content-Length", 0));
    constexpr auto kCeiling =
        static_cast<std::uint64_t>(std::numeric_limits<long long>::max());
    return static_cast<long long>(declared > kCeiling ? kCeiling : declared);
}

/// `?points=`, unparsed and unclamped -- `clampPoints` is the one place that
/// decides, and it is already tested. Absent or garbage reads 0, which
/// `clampPoints` maps to the shipped cap rather than to an empty body.
[[nodiscard]] long long requestedPoints(const httplib::Request& request) {
    if (!request.has_param("points")) {
        return 0;
    }
    const std::string raw = request.get_param_value("points");
    long long parsed = 0;
    const auto* const end = raw.data() + raw.size();
    const auto result = std::from_chars(raw.data(), end, parsed);
    if (result.ec != std::errc{} || result.ptr != end) {
        return 0;
    }
    return parsed;
}

/// `?since=`, the conditional-GET form that needs no server-issued validator.
/// A partially-parsed token is no token: `12x` is not 12, for the same reason
/// `hostIsAllowed` refuses `4736.attacker.example`.
[[nodiscard]] std::optional<std::uint64_t> sinceParam(const httplib::Request& request) {
    if (!request.has_param("since")) {
        return std::nullopt;
    }
    const std::string raw = request.get_param_value("since");
    std::uint64_t parsed = 0;
    const auto* const end = raw.data() + raw.size();
    const auto result = std::from_chars(raw.data(), end, parsed);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::nullopt;
    }
    return parsed;
}

}  // namespace

/// Everything httplib. Nothing above `ApiServer.h`'s `struct Impl;` forward
/// declaration can see any of it, which is the pimpl's whole job.
struct ApiServer::Impl {
    Impl(measure::SnapshotSource& sourceRef, ApiSettings settingsIn)
        : source(sourceRef),
          settings(std::move(settingsIn)),
          limiter(settings.maxRequestsPerSecond) {}

    void configure();
    void installPreRouting();
    void installRoutes();

    /// One route's response. `body` is the serialiser, already chosen --
    /// `rta::api::Serialiser` from ApiRoutes.h, a plain function pointer.
    void serve(const httplib::Request& request, httplib::Response& response, Serialiser body);

    measure::SnapshotSource& source;
    ApiSettings settings;

    /// Shared across the thread pool's workers, so it is guarded. Why a
    /// sliding window and not a counter is ApiPolicy.h's own argument.
    RateLimiter limiter;
    std::mutex limiterMutex;

    httplib::Server svr;
    std::thread thread;

    /// The port actually bound. Also the port the `Host` allowlist compares
    /// against -- see installPreRouting().
    int boundPort = -1;
    std::optional<std::string> refusal;
};

void ApiServer::Impl::configure() {
    // Every one of these is a RUN-TIME setting on the Server object rather
    // than a CPPHTTPLIB_* macro, so the values sit next to the record's
    // reasoning instead of in a build file (record sec.8).
    svr.set_read_timeout(2, 0);
    svr.set_write_timeout(2, 0);
    svr.set_keep_alive_timeout(30);
    // Stock httplib is 100, which is five seconds at a 20 Hz poll -- a
    // reconnect every five seconds for a client doing exactly what sec.3's
    // polling contract tells it to.
    svr.set_keep_alive_max_count(1000);
    svr.set_payload_max_length(static_cast<std::size_t>(settings.maxRequestBodyBytes));
    svr.new_task_queue = [] { return new httplib::ThreadPool(2, 8); };
}

void ApiServer::Impl::installPreRouting() {
    // LOW follow-up batch, item 12: controls 1-5's sequencing and the WHY of
    // each one now live in `decidePreRoutingRefusal` (ApiPolicy.h/.cpp),
    // proven in RTA_BUILD_APP=OFF (test_api_pre_routing.cpp) rather than only
    // through a real socket here. What stays in THIS file is the httplib
    // glue -- extracting plain values from `request`, applying the decision
    // to `response` -- plus the two things a pure function cannot own:
    //
    // THE RATE LIMITER RUNS LAST, AFTER EVERY REFUSAL ABOVE, AND PR #18's
    // FIRST VERSION HAD IT FIRST. That was wrong, and the station-5 verifier
    // measured it: at a limit of 3, three forged-`Host` requests filled the
    // sliding window and the legitimate fourth got 429. A caller who cannot
    // read one byte of this API, from outside the allowlist, with no token,
    // could deny it to the operator during a show. sec.4's accounting is a
    // bound on `SnapshotSource::latest()` LOADS, and every refusal above
    // returns WITHOUT touching the publish slot, so admitting a refused
    // request into the window would spend a legitimate client's quota on
    // work that never happened. `RateLimiter::admit` also mutates shared
    // state and needs a clock reading, so it is not a decision
    // `decidePreRoutingRefusal` could make purely.
    //
    // WHAT THIS GIVES UP, stated rather than glossed: the limiter no longer
    // bounds total INBOUND traffic, only SERVED traffic. A hostile caller
    // can send forged-`Host` requests as fast as it likes and each costs an
    // accept plus a header parse. That is the right trade -- the control
    // exists to protect the publish slot, httplib's 2/8 thread pool bounds
    // the concurrency, and the alternative hands that same caller a denial
    // of service against the operator.
    svr.set_pre_routing_handler(
        [this](const httplib::Request& request, httplib::Response& response) {
            // httplib::Request::get_header_value returns std::string BY VALUE.
            // PreRoutingInputs::hostHeaderValue/authorizationHeader are
            // string_view, so each header must be held in a named std::string
            // that outlives the decidePreRoutingRefusal(in, ...) call below --
            // assigning the temporary return straight into the view would
            // dangle the instant this statement ends.
            const std::string hostHeader = request.get_header_value("Host");
            const std::string authHeader = request.get_header_value("Authorization");

            PreRoutingInputs in;
            // get_header_value_count returns size_t; decidePreRoutingRefusal only
            // ever compares hostHeaderCount against 1, so a bounded int is enough
            // and this cast is what keeps the build warning-free (C4267).
            in.hostHeaderCount = static_cast<int>(request.get_header_value_count("Host"));
            in.hostHeaderValue = hostHeader;
            in.boundPort = boundPort;
            in.method = methodOf(request.method);
            in.authorizationHeader = authHeader;
            in.declaredBodyBytes = declaredBodyBytes(request);

            switch (decidePreRoutingRefusal(in, settings)) {
                case PreRoutingRefusal::MultipleHostHeaders:
                    response.status = 400;
                    return HandlerResponse::Handled;
                case PreRoutingRefusal::HostNotAllowed:
                    response.status = 403;
                    return HandlerResponse::Handled;
                case PreRoutingRefusal::MethodNotAllowed:
                    // All three names in `kAllowedMethods` are SERVED: `Get`
                    // routes answer GET and HEAD (httplib dispatches both to
                    // `get_handlers_`), and `Options` routes answer OPTIONS.
                    // PR #18 advertised OPTIONS here and registered none, so
                    // the method passed this check, found no route and
                    // answered 404 -- an API naming a method it does not
                    // serve.
                    response.status = 405;
                    response.set_header("Allow", kAllowedMethods);
                    return HandlerResponse::Handled;
                case PreRoutingRefusal::Unauthorized:
                    response.status = 401;
                    response.set_header("WWW-Authenticate", "Bearer");
                    return HandlerResponse::Handled;
                case PreRoutingRefusal::BodyTooLarge:
                    response.status = 413;
                    return HandlerResponse::Handled;
                case PreRoutingRefusal::None:
                    break;
            }

            // THE RATE LIMIT, last, immediately before routing -- the last
            // point before a `latest()`. See this function's own header
            // comment for why it moved here and what that gives up.
            //
            // OPTIONS IS EXEMPT, and that is R19's own argument carried one
            // step further rather than an exception to it. The round-2
            // verifier measured the gap: at a limit of 2, two `OPTIONS`
            // requests then a legitimate `GET` answered 429 -- and an
            // `OPTIONS` never reaches `serve()`, so it performs no
            // `latest()` and there is nothing for a bound on LOADS to bound.
            // Its whole cost is an accept, a header parse and a 204, which
            // is the cost of the 403 that R19 already stopped charging for.
            //
            // HEAD IS NOT EXEMPT, and that is the half worth stating: it
            // routes to the same `Get` handler, so it does the full
            // `latest()` AND the full serialisation -- httplib strips the
            // body on the way out, after the work. A "no body, so no cost"
            // reading of HEAD would be wrong here and would put an
            // unbounded load path on the publish slot.
            if (in.method != Method::Options) {
                const std::lock_guard<std::mutex> lock(limiterMutex);
                if (!limiter.admit(std::chrono::steady_clock::now())) {
                    response.status = 429;
                    return HandlerResponse::Handled;
                }
            }

            // NO CORS HEADERS, and no pretence that their absence is a
            // defence: a GET with only safelisted headers is a SIMPLE
            // request, gets no preflight, and is EXECUTED by this program
            // before the browser decides whether the calling script may
            // read the reply (sec.9). `settings.corsOrigins` is empty and
            // nothing here reads it -- there is no code path that emits an
            // `Access-Control-*` header at all.
            return HandlerResponse::Unhandled;
        });
}

void ApiServer::Impl::serve(const httplib::Request& request, httplib::Response& response,
                            Serialiser body) {
    Request shape;
    shape.points = clampPoints(requestedPoints(request), settings);

    // ONE `latest()` per request, and the expensive work -- thousands of
    // floats into JSON -- happens entirely after it, from this copy, with the
    // publish slot released. That ordering IS the decision (record sec.4).
    const measure::SnapshotPtr snapshot = source.latest();
    if (!snapshot) {
        // Nothing has been published yet. 503 rather than an empty document:
        // "no measurement exists" and "a measurement of nothing" are
        // different answers, and a client that cannot tell them apart will
        // plot the second one.
        response.status = 503;
        return;
    }

    const std::uint64_t sequence = snapshot->sequence;
    response.set_header("ETag", etagFor(sequence));

    if (conditionalVerdict(request.get_header_value("If-None-Match"), sinceParam(request),
                           sequence) == Verdict::NotModified) {
        response.status = 304;
        return;
    }

    response.status = 200;
    response.set_content(body(*snapshot, shape, settings), "application/json");
}

void ApiServer::Impl::installRoutes() {
    const auto route = [this](const char* path, Serialiser body) {
        const std::string target = std::string("/api/v1/") + path;
        svr.Get(target, [this, body = std::move(body)](const httplib::Request& request,
                                                      httplib::Response& response) {
            serve(request, response, body);
        });

        // OPTIONS on the same resource -> 204 with `Allow`, reading NO
        // snapshot; HEAD needs no registration at all. ApiRoutes.h's "Which
        // methods each resource answers" carries both arguments, including
        // what PR #18 got wrong and why 204 rather than 200.
        svr.Options(target, [](const httplib::Request&, httplib::Response& response) {
            response.status = 204;
            response.set_header("Allow", kAllowedMethods);
        });
    };

    // The endpoint table is `ApiRoutes.h` -- eight entries, framework-free
    // and server-library-free, so "which paths exist and what each serialises"
    // is asserted directly in the RTA_BUILD_APP=OFF target with no server in
    // the picture (test_api_server_refusals.cpp). This loop is the only place
    // that knows a path is an HTTP route at all.
    //
    // And NOTHING ELSE is registered: no `WebSocket(...)`, which is the entire
    // reason no upgrade can be established here (sec.15 R16a, and I11 measures
    // it).
    for (const RouteEntry& entry : apiRoutes()) {
        route(entry.path, entry.body);
    }
}

ApiServer::ApiServer(measure::SnapshotSource& source, ApiSettings settings)
    : impl_(std::make_unique<Impl>(source, std::move(settings))) {
    // A refusal is not an error. `allowLanBind` exists so the roadmap is
    // visible and the code path behind it does not; a composition root that
    // cannot start a read-only listener still has an application to run.
    impl_->refusal = startRefusal(impl_->settings);
    if (impl_->refusal.has_value() || !impl_->settings.enabled) {
        return;
    }

    impl_->configure();
    impl_->installPreRouting();
    impl_->installRoutes();

    // BIND ON THIS THREAD, LISTEN ON THE OTHER, and splitting the two is what
    // removes the startup race: the bind completes before the constructor
    // returns, so `boundPort()` is valid before the server thread has run at
    // all and no test in this lane sleeps or polls `is_running()`.
    //
    // `bind_to_port` returns `bool` and DISCARDS the port; `Server` has no
    // `port()` accessor (that member is on `Client`). The one that returns a
    // port is `bind_to_any_port`, which is why a `port == 0` configuration
    // takes the other branch.
    impl_->boundPort = (impl_->settings.port == 0)
                           ? impl_->svr.bind_to_any_port(impl_->settings.bindAddress)
                           : (impl_->svr.bind_to_port(impl_->settings.bindAddress,
                                                      impl_->settings.port)
                                  ? impl_->settings.port
                                  : -1);
    if (impl_->boundPort <= 0) {
        impl_->boundPort = -1;
        return;
    }

    impl_->thread = std::thread([impl = impl_.get()] { impl->svr.listen_after_bind(); });
}

ApiServer::~ApiServer() {
    // `stop()` THEN join, and the belt-and-braces second guarantee is the one
    // `AnalysisThread`'s destructor comment already argues for: getting
    // shutdown wrong is a crash that happens once, at exit, on a customer's
    // machine. `stop()` is safe on a server that never bound.
    impl_->svr.stop();
    if (impl_->thread.joinable()) {
        impl_->thread.join();
    }
}

bool ApiServer::running() const noexcept {
    return impl_->boundPort > 0 && impl_->thread.joinable();
}

int ApiServer::boundPort() const noexcept { return impl_->boundPort; }

const std::optional<std::string>& ApiServer::refusal() const noexcept { return impl_->refusal; }

}  // namespace rta::api
