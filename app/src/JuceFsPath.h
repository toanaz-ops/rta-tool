// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src. Fix round (PR #43, verifier HIGH F1):
// `std::filesystem::path(juce::File::getFullPathName().toStdString())` is
// wrong on Windows. `toStdString()` returns UTF-8; MSVC's
// `std::filesystem::path(const std::string&)` decodes with the process's
// ACTIVE CODE PAGE (`<filesystem>`'s `_Convert_narrow_to_wide`), not UTF-8.
// On a stock Windows box (ACP 1252/1258 -- CI's windows-latest included), a
// folder name outside that code page's own repertoire (Vietnamese
// diacritics, or a Vietnamese Windows user name under Documents) silently
// writes to a mojibake sibling path while the readout claims success; a
// dev box whose ACP happens to be 65001 (UTF-8) never reproduces this,
// which is exactly why local runs here passed.
#pragma once

#include <juce_core/juce_core.h>

#include <filesystem>

/// `getFullPathName().toWideCharPointer()` carries the exact UTF-16 code
/// units `std::filesystem::path` needs on Windows, bypassing the code-page
/// conversion entirely -- the `std::filesystem::path(const wchar_t*)`
/// constructor takes the string as already being in the OS's native
/// encoding, which on Windows IS UTF-16. Never round-trip through
/// `toStdString()` for anything that becomes a `std::filesystem::path`.
[[nodiscard]] inline std::filesystem::path toFsPath(const juce::File& file) {
    return std::filesystem::path(file.getFullPathName().toWideCharPointer());
}
