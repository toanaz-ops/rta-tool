// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/export. No JUCE: enforced by the
// measure_has_no_framework_deps ctest.
// Lane L6a task W4a-A (docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md).
//
// The report's CSS, split into its own header purely to stay clear of
// SplReport.cpp's 400-line hard cap (plan: "split across three headers
// before writing, not after"). Inlined into a <style> tag by SplReport.cpp
// -- no <link rel="stylesheet">, ever (A1).
//
// SPL-R10 / A8: no CSS selector here may spell the word "class" immediately
// followed by (optional separator, then) the digit 0 or 1 --
// `core_makes_no_class_1_claim`'s scope grows to this file and that shape is
// a false positive it is designed to catch. Every class name below is a
// plain word (".section", ".kv", ".bar") for exactly that reason, never
// "tier-1"-shaped near the literal word "class".
#pragma once

#include <string_view>

namespace rta::splexport {

inline constexpr std::string_view kReportStyle = R"CSS(
:root {
  --bg: #17181c; --panel: #1f2126; --ink: #e8e6e1; --dim: #9a988f;
  --line: #2c2e34; --amber: #e0a028; --ice: #6fb8c9; --bad: #c4453a;
}
* { box-sizing: border-box; }
body {
  margin: 0; padding: 24px; background: var(--bg); color: var(--ink);
  font-family: "IBM Plex Sans", Arial, sans-serif; font-size: 14px;
  line-height: 1.45;
}
h1 { font-family: "Saira Condensed", Arial, sans-serif; font-weight: 600;
  letter-spacing: 0.02em; margin: 0 0 4px 0; }
h2 { font-family: "Saira Condensed", Arial, sans-serif; font-weight: 600;
  border-bottom: 1px solid var(--line); padding-bottom: 4px; margin: 0 0 10px 0; }
.subtitle { color: var(--dim); margin: 0 0 20px 0; }
.section { background: var(--panel); border: 1px solid var(--line);
  border-radius: 6px; padding: 16px 20px; margin: 0 0 16px 0; }
.section.collapsed > *:not(h2) { display: none; }
.kv { display: grid; grid-template-columns: 220px 1fr; gap: 4px 12px;
  margin: 2px 0; }
.kv .label { color: var(--dim); }
.kv .value { font-family: "IBM Plex Mono", monospace; }
table { border-collapse: collapse; width: 100%; margin: 6px 0; }
th, td { text-align: left; padding: 4px 10px; border-bottom: 1px solid var(--line);
  font-family: "IBM Plex Mono", monospace; font-size: 13px; }
th { color: var(--dim); font-family: "IBM Plex Sans", sans-serif; font-weight: 600; }
.state-fired { color: var(--bad); font-weight: 600; }
.state-clear { color: var(--ink); }
.state-filling { color: var(--dim); }
.absent { color: var(--dim); font-style: italic; }
.honesty { color: var(--dim); font-size: 12px; border-top: 1px solid var(--line);
  padding-top: 10px; margin-top: 10px; }
.hash { font-family: "IBM Plex Mono", monospace; word-break: break-all; }
.strip { width: 100%; height: 120px; background: #14151a; border-radius: 4px; }
.strip .trace { fill: none; stroke: var(--ice); stroke-width: 1.5; }
.strip .marker-alarm { stroke: var(--bad); }
.strip .marker-overload { stroke: var(--amber); }
.strip .marker-gap { stroke: var(--dim); stroke-dasharray: 2 2; }
)CSS";

}  // namespace rta::splexport
