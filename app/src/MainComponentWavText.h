// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src. Small formatting helpers shared by MainComponentWav.cpp and
// MainComponentWavPair.cpp (the one-file and the two-file halves of the LOAD WAV block).
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdio>
#include <string>

namespace mainwav {

inline juce::String utf8(const std::string& text) { return juce::String::fromUTF8(text.c_str()); }

inline std::string fixed(double value, int decimals) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.*f", decimals, value);
    return buffer;
}

/// `%+.2f`: the clock error carries its sign (record E9).
inline std::string signedFixed(double value, int decimals) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%+.*f", decimals, value);
    return buffer;
}

inline void fillChannels(juce::ComboBox& combo, int count, int selected1Based) {
    combo.clear(juce::dontSendNotification);
    for (int i = 1; i <= count; ++i) combo.addItem("ch" + juce::String(i), i);
    combo.setSelectedId(selected1Based, juce::dontSendNotification);
}

}  // namespace mainwav
