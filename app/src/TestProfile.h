// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Test-profile isolation for real-app UI tests.
//
// WHY. JUCE's getSpecialLocation on Windows uses CSIDL, so overriding APPDATA
// or USERPROFILE does not redirect it: a UI test launched against the real
// exe would read and write the owner's real Documents. The shared launcher
// (shared/harness/guards/test_app_launcher.ps1 -App rta) therefore sets
// DEVCAVE_TEST_PROFILE=<absolute dir>, laid out as
//     <profile>\AppData   <profile>\LocalAppData   <profile>\Documents
// and this header is the ONLY place the app maps "user documents" and "user
// app data" to a directory.
//
// RULES.
//  - Variable unset or empty: behaviour is exactly the old one (real paths).
//  - Variable set and absolute: documents -> <profile>/Documents,
//    app data -> <profile>/AppData/<app folder>, window title gets a suffix.
//  - Variable set but relative: refused. The helpers then return a path under
//    the system temp directory (never a real-data path) and `refusal()`
//    carries the message; Main.cpp shows it and quits before a window opens.
//
// Pure logic takes the variable's value as an argument so tests need not touch
// the process environment.
#pragma once

#include <juce_core/juce_core.h>

namespace rta::app::testprofile {

inline constexpr const char* kEnvName = "DEVCAVE_TEST_PROFILE";
inline constexpr const char* kTitleSuffix = " [TEST PROFILE]";

struct Resolution {
    bool active = false;   // variable set and non-empty
    bool valid = true;     // false when set but unusable (relative)
    juce::File root;       // profile root; meaningful only when active && valid
    juce::String message;  // refusal text when !valid
};

[[nodiscard]] inline Resolution resolve(const juce::String& envValue) {
    Resolution r;
    const auto trimmed = envValue.trim();
    if (trimmed.isEmpty()) return r;
    r.active = true;
    if (!juce::File::isAbsolutePath(trimmed)) {
        r.valid = false;
        r.message = juce::String(kEnvName) + " must be an absolute directory path, got '" + trimmed +
                    "'. Refusing to start rather than fall back to the real user data.";
        return r;
    }
    r.root = juce::File(trimmed);
    return r;
}

[[nodiscard]] inline Resolution resolveFromEnvironment() {
    return resolve(juce::SystemStats::getEnvironmentVariable(kEnvName, {}));
}

/// Where a refused (relative) profile sends I/O: a scratch place, never real data.
[[nodiscard]] inline juce::File refusedSink() {
    return juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("rta-test-profile-refused");
}

[[nodiscard]] inline juce::File documentsDirectory(const Resolution& r) {
    if (!r.active) return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    return r.valid ? r.root.getChildFile("Documents") : refusedSink().getChildFile("Documents");
}

[[nodiscard]] inline juce::File appDataDirectory(const Resolution& r, const juce::String& appFolder = "RTA Tool") {
    if (!r.active) return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile(appFolder);
    return (r.valid ? r.root : refusedSink()).getChildFile("AppData").getChildFile(appFolder);
}

[[nodiscard]] inline juce::String windowTitle(const Resolution& r, const juce::String& base) {
    return r.active ? base + kTitleSuffix : base;
}

/// JUCE derives the single-instance lock (and forwards a second launch's
/// command line) from getApplicationName(). A test instance must therefore
/// have its own name, or an owner launch (e.g. double-clicking a session file)
/// would be forwarded into the TEST window. Real mode: byte-identical `base`.
[[nodiscard]] inline juce::String applicationName(const Resolution& r, const juce::String& base) {
    if (!r.active) return base;
    const auto key = (r.valid ? r.root.getFullPathName() : juce::String("refused")).toLowerCase();
    return base + "_testprofile_" + juce::String::toHexString(key.hashCode64() & 0xffffffffLL);
}

// Convenience wrappers that read the real environment.
[[nodiscard]] inline bool isActive() { return resolveFromEnvironment().active; }
[[nodiscard]] inline juce::File documentsDirectory() { return documentsDirectory(resolveFromEnvironment()); }
[[nodiscard]] inline juce::File appDataDirectory() { return appDataDirectory(resolveFromEnvironment()); }
[[nodiscard]] inline juce::String applicationName(const juce::String& base) {
    return applicationName(resolveFromEnvironment(), base);
}
[[nodiscard]] inline juce::String windowTitle(const juce::String& base) {
    return windowTitle(resolveFromEnvironment(), base);
}

}  // namespace rta::app::testprofile
