#include "RibbonBar.h"
#include "IntegraLookAndFeel.h"

namespace integra
{
    RibbonBar::RibbonBar (AppContext& c) : context (c)
    {
        for (auto* b : { &arrangeButton, &liveButton })
        {
            b->setClickingTogglesState (false);
            b->setRadioGroupId (1001);
            addAndMakeVisible (b);
        }
        arrangeButton.onClick = [this] { if (onViewRequested) onViewRequested ("arrange"); };
        liveButton.onClick = [this] { if (onViewRequested) onViewRequested ("live"); };

        homeButton.setTooltip ("Return to start (Enter)");
        playButton.setTooltip ("Play / pause (Space)");
        prevScene.setTooltip ("Previous scene");
        nextScene.setTooltip ("Next scene");
        homeButton.onClick = [this] { context.controller.returnToStart(); };
        playButton.onClick = [this] { context.controller.play (! context.controller.isPlaying()); };
        prevScene.onClick = [this] { context.controller.previousScene(); };
        nextScene.onClick = [this] { context.controller.nextScene(); };
        prefsButton.onClick = [this] { if (onPreferences) onPreferences(); };

        for (auto* b : { &homeButton, &playButton, &prevScene, &nextScene, &prefsButton })
            addAndMakeVisible (b);

        // a focused button would take Enter, Space and the arrow keys, which work
        // wherever the focus is in Flex (IntegraLive.onStageKeyDown)
        for (auto* b : { &arrangeButton, &liveButton, &homeButton, &playButton, &prevScene, &nextScene, &prefsButton })
            b->setWantsKeyboardFocus (false);

        startTimerHz (20);
    }


    RibbonBar::~RibbonBar()
    {
        context.watcher.removeAll (this);
    }
    void RibbonBar::setCurrentView (const juce::String& viewName)
    {
        arrangeButton.setToggleState (viewName != "live", juce::dontSendNotification);
        liveButton.setToggleState (viewName == "live", juce::dontSendNotification);
    }

    void RibbonBar::timerCallback()
    {
        auto& p = context.model.get();
        const int rate = juce::jmax (1, p.player.rate);
        const int tick = context.controller.getPlayPosition();
        const bool playing = context.controller.isPlaying();

        playButton.setButtonText (playing ? "Pause" : "Play");
        playButton.setToggleState (playing, juce::dontSendNotification);

        const int tenths = tick * 10 / rate;
        timeText = juce::String::formatted ("%d:%02d.%d", tenths / 600, (tenths / 10) % 60, tenths % 10);

        juce::String scene;
        if (p.player.path.isNotEmpty())
            scene = context.session.getValue (p.player.path + ".scene").toString();
        sceneText = scene.isEmpty() ? juce::String ("no scene") : scene;

        outLevel = juce::jmax (outLevel * 0.85f, context.audioHost.getAndResetOutputPeak());
        inLevel = juce::jmax (inLevel * 0.85f, context.audioHost.getAndResetInputPeak());

        // every incoming message is a set of midiMessage, so watch it rather than polling
        // the value (repeated identical messages wouldn't change it)
        if (p.midiMonitorPath != watchedMidiMonitor)
        {
            context.watcher.removeAll (this);
            watchedMidiMonitor = p.midiMonitorPath;
            if (watchedMidiMonitor.isNotEmpty())
                context.watcher.watch (watchedMidiMonitor + ".midiMessage", this, [this] (const juce::String&, CCommandSource)
                {
                    lastMidi = juce::Time::getMillisecondCounter();
                });
        }
        midiActivity = juce::Time::getMillisecondCounter() - lastMidi < 250;

        repaint();
    }

    void RibbonBar::paint (juce::Graphics& g)
    {
        auto& t = Theme::get();
        g.setGradientFill (juce::ColourGradient (t.header.brighter (0.1f), 0, 0, t.header.darker (0.05f), 0, (float) getHeight(), false));
        g.fillAll();

        auto area = getLocalBounds().reduced (8, 6);

        g.setColour (t.text);
        g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
        g.drawText ("Integra Live", area.removeFromLeft (130), juce::Justification::centredLeft);

        // time & scene display (between transport and settings)
        auto display = juce::Rectangle<int> (nextScene.getRight() + 12, 6, 260, getHeight() - 12);
        g.setColour (t.panel);
        g.fillRoundedRectangle (display.toFloat(), 4.0f);
        g.setColour (t.outline);
        g.drawRoundedRectangle (display.toFloat(), 4.0f, 1.0f);
        g.setColour (t.text);
        g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 18.0f, juce::Font::bold));
        g.drawText (timeText, display.reduced (8, 0).removeFromLeft (100), juce::Justification::centredLeft);
        g.setFont (juce::FontOptions (13.0f));
        g.setColour (t.textDim);
        g.drawText ("Scene: " + sceneText, display.reduced (8, 0).withTrimmedLeft (104), juce::Justification::centredLeft);

        // meters
        auto meters = juce::Rectangle<int> (display.getRight() + 12, 10, 90, getHeight() - 20);
        auto drawMeter = [&] (juce::Rectangle<int> r, float level, const char* label)
        {
            g.setColour (t.controlBackground);
            g.fillRect (r);
            const float db = juce::Decibels::gainToDecibels (level, -60.0f);
            const float proportion = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
            g.setColour (level > 0.99f ? t.meterHigh : t.meterLow);
            g.fillRect (r.withWidth ((int) (r.getWidth() * proportion)));
            g.setColour (t.textDim);
            g.setFont (juce::FontOptions (9.0f));
            g.drawText (label, r.translated (-r.getWidth() - 2, 0).withWidth (r.getWidth()), juce::Justification::centredRight);
        };
        drawMeter (meters.removeFromTop (meters.getHeight() / 2 - 1).withTrimmedLeft (24), inLevel, "in");
        meters.removeFromTop (2);
        drawMeter (meters.withTrimmedLeft (24), outLevel, "out");

        // MIDI activity
        auto midiLed = juce::Rectangle<float> ((float) display.getRight() + 112.0f, (float) getHeight() * 0.5f - 6.0f, 12.0f, 12.0f);
        g.setColour (midiActivity ? t.meterLow : t.controlBackground);
        g.fillEllipse (midiLed);
        g.setColour (t.textDim);
        g.setFont (juce::FontOptions (10.0f));
        g.drawText ("MIDI", midiLed.translated (16.0f, 0.0f).withWidth (30.0f).toNearestInt(), juce::Justification::centredLeft);

        g.setColour (t.outline);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
    }

    void RibbonBar::resized()
    {
        auto area = getLocalBounds().reduced (8, 8);
        area.removeFromLeft (138);
        arrangeButton.setBounds (area.removeFromLeft (80));
        liveButton.setBounds (area.removeFromLeft (60));
        area.removeFromLeft (16);
        homeButton.setBounds (area.removeFromLeft (36));
        area.removeFromLeft (4);
        playButton.setBounds (area.removeFromLeft (64));
        area.removeFromLeft (12);
        prevScene.setBounds (area.removeFromLeft (30));
        area.removeFromLeft (4);
        nextScene.setBounds (area.removeFromLeft (30));
        prefsButton.setBounds (area.removeFromRight (80));
    }

    Info RibbonBar::getInfoAt (juce::Point<int> p)
    {
        // RibbonBar.getInfoToDisplay
        auto* c = getComponentAt (p);
        if (c == &arrangeButton)  return InfoTexts::forView ("Ribbon/ArrangeView");
        if (c == &liveButton)     return InfoTexts::forView ("Ribbon/LiveView");
        if (c == &homeButton)     return InfoTexts::forView ("Ribbon/Home");
        if (c == &playButton)     return InfoTexts::forView (context.controller.isPlaying() ? "Ribbon/Pause" : "Ribbon/Play");
        if (c == &prefsButton)    return InfoTexts::forView ("Preferences");
        if (p.x > getWidth() / 2 && c == this)
            return InfoTexts::forView ("Ribbon/MidiInputIndicator");
        return {};
    }
}
