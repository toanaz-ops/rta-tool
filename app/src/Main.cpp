// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Composition root. It installs the design system and opens the window; it
// owns no DSP and draws nothing itself.

#include <juce_gui_extra/juce_gui_extra.h>

#include <az_ui/az_ui.h>

#include <BinaryData.h>

#include "MainComponent.h"

namespace
{

/// Hand az_ui the typeface bytes. The module deliberately does not reach for
/// BinaryData itself -- see the rationale in az_ui/theme/Typography.h -- so
/// this is the one place the generated symbol names appear.
void installTypefaces()
{
    az::ui::TypefaceSet faces;
    faces.legendSemiBold = { BinaryData::SairaCondensedSemiBold_ttf,
                             BinaryData::SairaCondensedSemiBold_ttfSize };
    faces.legendBold     = { BinaryData::SairaCondensedBold_ttf,
                             BinaryData::SairaCondensedBold_ttfSize };
    faces.body           = { BinaryData::IBMPlexSansRegular_ttf,
                             BinaryData::IBMPlexSansRegular_ttfSize };
    faces.mono           = { BinaryData::IBMPlexMonoRegular_ttf,
                             BinaryData::IBMPlexMonoRegular_ttfSize };
    faces.monoMedium     = { BinaryData::IBMPlexMonoMedium_ttf,
                             BinaryData::IBMPlexMonoMedium_ttfSize };
    az::ui::setTypefaces (faces);
}

class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow()
        : juce::DocumentWindow ("RTA Tool", az::ui::background, juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new MainComponent(), true);
        setResizable (true, false);
        centreWithSize (1100, 720);
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};

} // namespace

class RtaToolApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "RTA Tool"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String&) override
    {
        installTypefaces();
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        window = std::make_unique<MainWindow>();
    }

    void shutdown() override
    {
        window.reset();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override { quit(); }

private:
    az::ui::AzLookAndFeel            lookAndFeel;
    std::unique_ptr<MainWindow>      window;
};

START_JUCE_APPLICATION (RtaToolApplication)
