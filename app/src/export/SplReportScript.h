// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/export. No JUCE: enforced by the
// measure_has_no_framework_deps ctest.
// Lane L6a task W4a-A (docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md sec.
// "Wave 4a"; record docs/dsp/2026-09-16-spl-pro-l6a.md sec.9).
//
// The JS the frozen report and the (cut) web viewer share -- one script,
// two transports, per the record's own "one HTML document, two transports".
//
// WAVE 4a SHIPS NO NETWORK CODE AT ALL. That is what makes A1's run-time-
// fetch greps (no `fetch(`, `XMLHttpRequest`, `navigator.sendBeacon`,
// `EventSource`, `WebSocket`, dynamic `import(`) pass by construction rather
// than by a runtime branch nobody can prove untaken from the text alone: the
// branch this comment used to describe -- "fetch only when
// `window.__SPL_PAYLOAD__` is absent" -- is Wave 4b's own addition (plan:
// "Files. Extend `app/src/export/SplReportScript.h`", task W4b-B), and Wave
// 4b is CUT (orchestrator decision, 2026-09-25). Adding that branch here,
// before a viewer exists to need it, would be exactly the "inlined
// `fetch('/api/v1/spl')`" defect 9 warns about: code that reaches for the
// network the day someone opens this archived file in a browser that still
// runs JavaScript, twenty years from now.
//
// What ships instead is the one piece of interactivity a STATIC document
// benefits from with no data source beyond what is already embedded: a
// collapse/expand toggle for the section headings, so a long report (many
// metrics, a full segment list) can be read as an outline. It touches only
// the DOM.
#pragma once

#include <string_view>

namespace rta::splexport {

inline constexpr std::string_view kReportScript = R"JS(
(function () {
  "use strict";
  var sections = document.querySelectorAll(".section > h2");
  for (var i = 0; i < sections.length; i++) {
    var heading = sections[i];
    heading.style.cursor = "pointer";
    heading.addEventListener("click", function (evt) {
      var body = evt.currentTarget.parentElement;
      body.classList.toggle("collapsed");
    });
  }
})();
)JS";

}  // namespace rta::splexport
