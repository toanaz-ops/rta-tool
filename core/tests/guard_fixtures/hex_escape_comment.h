// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// FIXTURE for core/tests/check_no_std_atomic_shared_ptr.cmake. It exists to be
// SCANNED, never compiled and never included by anything.
//
// The guard once passed each scanned file's whole CONTENT through a CMake
// MACRO argument. A macro substitutes its parameters textually and the result
// is re-lexed, so a backslash-x escape anywhere in a scanned file -- in code
// or, as on the line below, inside a comment -- aborted the guard with
// `Invalid escape sequence \x` instead of reporting a finding. PR #4 hit this
// and had to rewrite its UTF-8 BOM literal as three static_cast<char> bytes to
// get a green run; see
// https://github.com/toanaz-ops/rta-tool/pull/4#issuecomment-5689190024
//
// The two shapes below are that trap, spelled out: once in prose, once in real
// code. If this file ever stops being scanned cleanly the guard has gone back
// to re-lexing what it reads, and the next contributor will be told their
// source is a CMake syntax error.
//
//     const char* bom = "\xEF\xBB\xBF";  // UTF-8 byte order mark
//
inline const char* rtaGuardFixtureBom() { return "\xEF\xBB\xBF"; }
