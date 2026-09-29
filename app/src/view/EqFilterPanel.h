// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. The suggestion chips (ACCEPT / DECLINE) and
// the committed-filter list (tick = "already in the rig") of the EQ pane.
//
// L7-EQ UI task T5, decisions D1 and D6 (docs/plans/2026-09-29-eq-ui-lane-plan.md).
// A dumb view over EqPaneModel: it owns no state, only widgets, and reports
// clicks through the callbacks EqPaneView wires to the model. Split from
// EqPaneView to keep both under the file cap.
#pragma once

#include "measure/EqPaneModel.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>

namespace rta::view {

/// "peaking 1500 Hz  Q 2.00  +6.0 dB": frequency a whole number of hertz, Q
/// two decimals, dB one (CLAUDE.md "Reading out numbers" -- EqTextExport's
/// own precision, so the screen and the exported list agree).
[[nodiscard]] std::string eqFilterText(const rta::eq::FilterSpec& spec);

class EqFilterPanel final : public juce::Component {
public:
    static constexpr std::size_t kMaxChips = 3;      // D6
    static constexpr std::size_t kMaxFilters = 16;   // EqSession::setMaxFilters' ceiling

    EqFilterPanel();
    ~EqFilterPanel() override;

    /// Rebuilds every chip and row from the model.
    void refresh(const rta::measure::EqPaneModel& model);
    void resized() override;

    std::function<void(std::size_t)> onAccept;
    std::function<void(std::size_t)> onDecline;
    std::function<void(std::size_t, bool)> onApplied;

    // --- test hooks: each name is referenced whole-word from app/tests_juce.
    [[nodiscard]] std::size_t visibleChipCountForTest() const noexcept { return chipCount_; }
    [[nodiscard]] std::size_t visibleRowCountForTest() const noexcept { return rowCount_; }
    [[nodiscard]] juce::Button& acceptButtonForTest(std::size_t chip);
    [[nodiscard]] juce::Button& declineButtonForTest(std::size_t chip);
    [[nodiscard]] juce::ToggleButton& appliedToggleForTest(std::size_t row);
    [[nodiscard]] juce::String chipTextForTest(std::size_t chip) const;

private:
    struct Chip {
        juce::Label text;
        juce::TextButton accept{ "ACCEPT" };
        juce::TextButton decline{ "DECLINE" };
    };
    struct RowList final : juce::Component {
        std::array<std::unique_ptr<juce::ToggleButton>, kMaxFilters> toggles;
        void resized() override;
    };

    std::array<Chip, kMaxChips> chips_;
    juce::Label listCaption_;
    RowList rows_;
    juce::Viewport viewport_;
    std::size_t chipCount_ = 0;
    std::size_t rowCount_ = 0;
    bool refreshing_ = false;  ///< suppresses onApplied while toggles are set from the model

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EqFilterPanel)
};

}  // namespace rta::view
