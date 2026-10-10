#include "PreferencesPanel.h"
#include "IntegraLookAndFeel.h"

namespace integra
{
    PreferencesPanel::PreferencesPanel (AppContext& c)
        : context (c),
          audio (c.audioHost.getDeviceManager(), 0, 32, 1, 32, false, false, true, false)
    {
        addAndMakeVisible (audio);

        midiTitle.setText ("MIDI devices (via libIntegra / PortMidi)", juce::dontSendNotification);
        midiTitle.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        addAndMakeVisible (midiTitle);

        schemeTitle.setText ("Appearance", juce::dontSendNotification);
        schemeTitle.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        addAndMakeVisible (schemeTitle);

        darkScheme.setToggleState (Theme::isDarkScheme(), juce::dontSendNotification);
        darkScheme.onClick = [this] { if (onColourSchemeChanged) onColourSchemeChanged (darkScheme.getToggleState()); };
        addAndMakeVisible (darkScheme);

        resetButton.onClick = [this] { resetToDefaults(); };
        addAndMakeVisible (resetButton);

        // the MIDI lists follow libIntegra's device lists (Flex: SetAvailableMidiDevices)
        const auto settings = context.model.get().midiSettingsPath;
        if (settings.isNotEmpty())
        {
            juce::Component::SafePointer<PreferencesPanel> safePanel (this);    // MSVC: no `this` in a nested init-capture
            for (auto endpoint : { "availableInputDevices", "availableOutputDevices", "activeInputDevices", "activeOutputDevices" })
                context.watcher.watch (settings + "." + endpoint, this, [safePanel] (const juce::String&, CCommandSource)
                {
                    juce::MessageManager::callAsync ([safePanel] { if (safePanel != nullptr) safePanel->refreshMidi(); });
                });
        }

        refreshMidi();
        setSize (620, 660);
    }

    PreferencesPanel::~PreferencesPanel()
    {
        context.watcher.removeAll (this);
    }

    void PreferencesPanel::resetToDefaults()
    {
        auto& devices = context.audioHost.getDeviceManager();
        devices.closeAudioDevice();
        devices.initialiseWithDefaultDevices (2, 2);
        if (auto* settings = context.properties.getUserSettings())
            settings->removeValue ("audioDeviceState");
        const auto midi = context.model.get().midiSettingsPath;
        if (midi.isNotEmpty())
            context.session.bang (midi + ".restoreDefaults");
        refreshMidi();
    }

    void PreferencesPanel::refreshMidi()
    {
        midiInputs.clear();
        midiOutputs.clear();
        midiTitle.setText ("MIDI devices (via libIntegra / PortMidi)", juce::dontSendNotification);

        const auto settings = context.model.get().midiSettingsPath;
        if (settings.isEmpty())
            return;

        const auto available = unpackStringList (context.session.getValue (settings + ".availableInputDevices").toString());
        const auto active = unpackStringList (context.session.getValue (settings + ".activeInputDevices").toString());
        const auto availableOut = unpackStringList (context.session.getValue (settings + ".availableOutputDevices").toString());
        const auto activeOut = unpackStringList (context.session.getValue (settings + ".activeOutputDevices").toString());

        auto makeToggles = [this, settings] (juce::OwnedArray<juce::ToggleButton>& toggles, const juce::StringArray& all,
                                             const juce::StringArray& on, const juce::String& endpoint, const juce::String& prefix)
        {
            for (auto& name : all)
            {
                auto* t = toggles.add (new juce::ToggleButton (prefix + name));
                t->setToggleState (on.contains (name), juce::dontSendNotification);
                t->onClick = [this, &toggles, all, settings, endpoint]
                {
                    juce::StringArray selected;
                    for (int i = 0; i < toggles.size(); ++i)
                        if (toggles[i]->getToggleState())
                            selected.add (all[i]);
                    context.session.setValue (settings + "." + endpoint, packStringList (selected));
                };
                addAndMakeVisible (t);
            }
        };

        makeToggles (midiInputs, available, active, "activeInputDevices", "In: ");
        makeToggles (midiOutputs, availableOut, activeOut, "activeOutputDevices", "Out: ");

        if (available.isEmpty() && availableOut.isEmpty())
            midiTitle.setText ("MIDI devices (via libIntegra / PortMidi): none found", juce::dontSendNotification);

        resized();
    }

    void PreferencesPanel::paint (juce::Graphics& g)
    {
        g.fillAll (Theme::get().panel);
    }

    void PreferencesPanel::resized()
    {
        auto area = getLocalBounds().reduced (10);
        audio.setBounds (area.removeFromTop (330));
        area.removeFromTop (8);
        midiTitle.setBounds (area.removeFromTop (22));
        for (auto* t : midiInputs)   t->setBounds (area.removeFromTop (24).withTrimmedLeft (10));
        for (auto* t : midiOutputs)  t->setBounds (area.removeFromTop (24).withTrimmedLeft (10));
        area.removeFromTop (8);
        schemeTitle.setBounds (area.removeFromTop (22));
        darkScheme.setBounds (area.removeFromTop (26).withTrimmedLeft (10));
        resetButton.setBounds (getLocalBounds().reduced (20).removeFromBottom (26).removeFromRight (150));
    }

    void PreferencesPanel::show (AppContext& context, std::function<void (bool)> onSchemeChanged)
    {
        auto panel = std::make_unique<PreferencesPanel> (context);
        panel->onColourSchemeChanged = std::move (onSchemeChanged);

        juce::DialogWindow::LaunchOptions options;
        options.dialogTitle = "Integra Live Settings";
        options.dialogBackgroundColour = Theme::get().panel;
        options.content.setOwned (panel.release());
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = true;
        options.launchAsync();
    }
}
