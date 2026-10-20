// SPDX-License-Identifier: AGPL-3.0-or-later
// Internal to AlignRunner.cpp / AlignRunnerText.cpp: the operator-facing names.
#pragma once

#include "measure/AlignRunner.h"
#include "measure/AlignVerdict.h"

#include "rta/dsp/CrossoverFit.h"

namespace rta::measure::detail {

[[nodiscard]] const char* sourceLabel(Source source);
[[nodiscard]] const char* wizardRefusalName(WizardRefusal refusal);
[[nodiscard]] const char* fitRefusalName(rta::dsp::CrossoverRefusal refusal);

}  // namespace rta::measure::detail
