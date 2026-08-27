// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Composition root. It installs the design system and opens the window; it
// owns no DSP and draws nothing itself.

#include <juce_gui_extra/juce_gui_extra.h>

#include <az_ui/az_ui.h>

#include "AppTypefaces.h"
#include "MainComponent.h"
#include "dev/SpecimenComponent.h"

namespace
{

// Holds both the real measurement window (MainComponent) and the developer
// specimen sheet as SIBLING children, showing exactly one at a time, rather
// than swapping DocumentWindow's owned content back and forth. Swapping
// owned content would destroy and rebuild MainComponent -- and with it
// audioIo_, the analysis thread and any running synthetic feed -- every time
// a developer wants a look at the specimen sheet. Keeping both alive costs a
// second component's worth of memory and a few timers ticking unseen; it
// buys "the running measurement session survives opening the specimen".
//
// Reached with F1, per the plan's "your call, document it" -- chosen over a
// menu because this app has no menu bar yet and one control is not worth
// adding a whole bar for.
class RootView final : public juce::Component
{
public:
    RootView()
    {
        setWantsKeyboardFocus (true);
        addAndMakeVisible (mainComponent);
        addChildComponent (specimen); // present but not visible until F1
    }

    void resized() override
    {
        mainComponent.setBounds (getLocalBounds());
        specimen.setBounds (getLocalBounds());
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::F1Key)
        {
            showingSpecimen = ! showingSpecimen;
            mainComponent.setVisible (! showingSpecimen);
            specimen.setVisible (showingSpecimen);
            return true;
        }
        return false;
    }

private:
    MainComponent     mainComponent;
    SpecimenComponent specimen;
    bool              showingSpecimen = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RootView)
};

class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow()
        : juce::DocumentWindow ("RTA Tool", az::ui::background, juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new RootView(), true);
        setResizable (true, false);
        centreWithSize (1300, 820);
        setVisible (true);

        // RootView's keyPressed() (F1, the specimen toggle) only ever fires
        // once something has keyboard focus -- grab it here, once, rather
        // than leaving the first F1 press silently swallowed by no listener
        // at all.
        if (auto* content = getContentComponent())
            content->grabKeyboardFocus();
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
