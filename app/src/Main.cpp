// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Composition root. It installs the design system and opens the window; it
// owns no DSP and draws nothing itself.

#include <juce_gui_extra/juce_gui_extra.h>

#include <az_ui/az_ui.h>

#include "AppTypefaces.h"
#include "dev/SpecimenComponent.h"

namespace
{

class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow()
        : juce::DocumentWindow ("RTA Tool", az::ui::background, juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new SpecimenComponent(), true);
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
        installAppTypefaces();
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
