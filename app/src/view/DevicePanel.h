// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. See
// docs/plans/2026-08-27-audioio-rta-impl-plan.md §3.5 (Wave D / T9).
#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "rta/platform/AudioIo.h"

#include <cstdint>
#include <vector>

namespace rta::view {

/// The SODIUM RACK device panel: driver-type / device / sample-rate /
/// buffer-size combos, a latching start/stop switch, a drop-counter readout
/// and a fault line -- everything needed to pick, start and watch one audio
/// device. Every control acts through `rta::platform::AudioIo`'s own public
/// methods; this component holds no device state of its own beyond which
/// item each combo currently shows.
///
/// Every combo is repopulated from `AudioIo::currentState()` AFTER a
/// change, never from the value the user just picked -- the UI side of the
/// decision record's "read the device type BACK from the manager" rule:
/// JUCE silently keeps the current device type if the requested one (e.g.
/// "ASIO" with no driver installed) is not registered, and echoing the
/// request back would show the user a lie. `AudioIo::setSampleRate` /
/// `setBufferSize` return false on refusal without changing anything, which
/// this panel handles the same way: re-read, never trust the request.
class DevicePanel final : public juce::Component, private juce::Timer {
public:
    explicit DevicePanel(rta::platform::AudioIo& audioIo);
    ~DevicePanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    /// Re-reads `AudioIo::currentState()` and `AudioIo::lastFault()` and
    /// repopulates every combo's item list and selection, plus the
    /// start/stop switch's label, from it. Every change handler below ends
    /// with a call to this -- it is the one place that trusts the device,
    /// never the request.
    void refreshFromState();

    void deviceTypeChanged();
    void deviceChanged();
    void sampleRateChanged();
    void bufferSizeChanged();
    void startStopClicked();

    rta::platform::AudioIo& audioIo_;

    juce::ComboBox deviceTypeCombo_;
    juce::ComboBox deviceCombo_;
    juce::ComboBox sampleRateCombo_;
    juce::ComboBox bufferSizeCombo_;
    juce::TextButton startStopButton_{"START"};

    // Set while a combo's item list/selection is being rebuilt in code, so
    // its onChange handler does not read its own programmatic change back
    // as if the user had picked something.
    bool populating_ = false;

    // The exact numeric values behind sampleRateCombo_ / bufferSizeCombo_'s
    // items, in the same order -- so a change handler indexes into what was
    // actually offered instead of re-querying AudioIo (which could, in
    // principle, answer differently the second time).
    std::vector<double> cachedRates_;
    std::vector<int> cachedBufferSizes_;

    // Rectangles resized() hands to paint(), so the legends, the drop
    // counter and the fault line line up with the controls beside them
    // without paint() re-deriving the same layout math a second time.
    juce::Rectangle<int> typeLegendArea_;
    juce::Rectangle<int> deviceLegendArea_;
    juce::Rectangle<int> rateLegendArea_;
    juce::Rectangle<int> bufferLegendArea_;
    juce::Rectangle<int> dropReadoutArea_;
    juce::Rectangle<int> faultLineArea_;

    // Repaint on the 4 Hz poll is gated on these two rather than firing
    // regardless -- the fault line and drop counter are the only things
    // that can change here with no user action in between polls.
    std::uint32_t lastPaintedFaultSequence_ = 0;
    std::uint64_t lastPaintedDropCount_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DevicePanel)
};

}  // namespace rta::view
