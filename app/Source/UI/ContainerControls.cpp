#include "ContainerControls.h"
#include "IntegraLookAndFeel.h"

namespace integra
{
    AggregateLevel::AggregateLevel (AppContext& c, juce::String path) : context (c)
    {
        setContainer (path);
    }

    AggregateLevel::~AggregateLevel()
    {
        context.watcher.removeAll (this);
    }

    void AggregateLevel::setContainer (const juce::String& path)
    {
        if (path == container)
            return;

        context.watcher.removeAll (this);
        container = path;
        endpoints.clear();
        setLevel (0.0f);

        if (container.isNotEmpty())
            context.watcher.watchPrefix (container + ".", this, [this] (const juce::String& p, CCommandSource) { endpointChanged (p); });
    }

    void AggregateLevel::endpointChanged (const juce::String& path)
    {
        // AggregateVUContainer.onModuleAttributeChanged: VuMeter endpoints of audio outputs
        if (! lastElement (path).startsWith ("vu"))
            return;

        auto it = endpoints.find (path);
        if (it == endpoints.end())
        {
            static const juce::StringArray outputs { "AudioOut", "StereoAudioOut", "QuadAudioOut" };
            Endpoint e;
            e.isMeter = outputs.contains (context.session.getModuleName (parentPath (path)));
            if (e.isMeter)
                e.info = EndpointInfo::find (context.session, path);
            e.isMeter = e.isMeter && e.info.isNumeric();
            it = endpoints.emplace (path, e).first;
        }

        if (! it->second.isMeter)
            return;

        const auto value = context.session.getValue (path);
        setLevel ((float) juce::jlimit (0.0, 1.0, it->second.info.toUnit ((double) value)));
    }

    void AggregateLevel::setLevel (float newLevel)
    {
        lastUpdate = juce::Time::getMillisecondCounter();
        if (newLevel > 0.0f && ! isTimerRunning())
            startTimer (250);

        if (std::abs (newLevel - level) < 0.002f)
            return;
        level = newLevel;
        if (onChange)
            onChange();
    }

    void AggregateLevel::timerCallback()
    {
        // nothing reported for a while: the outputs stopped (inactive block, transport)
        if (juce::Time::getMillisecondCounter() - lastUpdate > 500)
        {
            stopTimer();
            level = 0.0f;
            if (onChange)
                onChange();
        }
    }

    //==============================================================================
    ContainerMeter::ContainerMeter (AppContext& c) : level (c)
    {
        level.onChange = [this] { repaint(); };
        setInterceptsMouseClicks (false, false);
    }

    void ContainerMeter::paintMeter (juce::Graphics& g, juce::Rectangle<float> r, float value, juce::Colour colour)
    {
        auto& t = Theme::get();
        g.setColour (t.controlBackground);
        g.fillRoundedRectangle (r, 2.0f);
        if (value > 0.0f)
        {
            g.setColour (value > 0.98f ? t.meterHigh : colour);
            g.fillRoundedRectangle (r.withWidth (juce::jmax (2.0f, r.getWidth() * value)), 2.0f);
        }
    }

    void ContainerMeter::paint (juce::Graphics& g)
    {
        paintMeter (g, getLocalBounds().toFloat(), level.getLevel(), colour);
    }

    //==============================================================================
    ActiveButton::ActiveButton (AppContext& c) : context (c)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    ActiveButton::~ActiveButton()
    {
        context.watcher.removeAll (this);
    }

    void ActiveButton::setContainer (const juce::String& path)
    {
        if (path == container)
            return;
        context.watcher.removeAll (this);
        container = path;
        if (container.isNotEmpty())
            context.watcher.watch (container + ".active", this, [this] (const juce::String&, CCommandSource) { repaint(); if (onChange) onChange(); });
        setTooltip ("Active: switch " + lastElement (container) + " on or off");
        repaint();
    }

    bool ActiveButton::isActive() const
    {
        return container.isEmpty() || (int) context.session.getValue (container + ".active") != 0;
    }

    void ActiveButton::toggle()
    {
        if (container.isNotEmpty())
            setActive (context, container, ! isActive());
        repaint();
        if (onChange)
            onChange();
    }

    void ActiveButton::setActive (AppContext& context, const juce::String& path, bool active)
    {
        context.controller.setValue (path + ".active", active ? 1 : 0);
    }

    bool ActiveButton::isEffectivelyActive (AppContext& context, const juce::String& path)
    {
        // a container is silent if it, or anything above it, is inactive
        for (auto p = path; p.isNotEmpty(); p = p.containsChar ('.') ? parentPath (p) : juce::String())
            if (context.session.nodeExists (p) && (int) context.session.getValue (p + ".active") == 0)
                return false;
        return true;
    }

    void ActiveButton::paintIcon (juce::Graphics& g, juce::Rectangle<float> r, bool active, juce::Colour colour)
    {
        auto& t = Theme::get();
        r = r.reduced (1.0f);
        g.setColour (active ? colour : t.controlBackground);
        g.fillEllipse (r);
        g.setColour (active ? juce::Colours::white : t.textDim);

        // power symbol
        auto c = r.reduced (r.getWidth() * 0.25f);
        juce::Path arc;
        arc.addCentredArc (c.getCentreX(), c.getCentreY(), c.getWidth() / 2, c.getHeight() / 2, 0.0f,
                           juce::MathConstants<float>::pi * 0.22f, juce::MathConstants<float>::twoPi - juce::MathConstants<float>::pi * 0.22f, true);
        g.strokePath (arc, juce::PathStrokeType (1.4f));
        g.drawLine (c.getCentreX(), c.getY() - 1.0f, c.getCentreX(), c.getCentreY(), 1.4f);
    }

    void ActiveButton::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat();
        paintIcon (g, r.withSizeKeepingCentre (juce::jmin (r.getWidth(), r.getHeight()), juce::jmin (r.getWidth(), r.getHeight())),
                   isActive(), colour.value_or (Theme::get().accent));
    }

    void ActiveButton::mouseUp (const juce::MouseEvent& e)
    {
        if (getLocalBounds().contains (e.getPosition()))
            toggle();
    }
}
