// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

/// Hand az_ui the typeface bytes from this application's binary data.
///
/// az_ui deliberately does not reach for BinaryData itself -- see the rationale
/// in az_ui/theme/Typography.h -- so this is the one place in the project where
/// the generated symbol names appear. Both the application and the offscreen
/// snapshot tool call it, which is why it is not buried in Main.cpp.
void installAppTypefaces();
