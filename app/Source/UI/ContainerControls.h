#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <map>
#include <optional>
#include "Core/AppContext.h"
#include "Core/EndpointInfo.h"

namespace integra
{
    /**
        The level of a project, track or block (Flex: AggregateVUContainer): the latest value
        of any VU endpoint of an audio output module (AudioOut, StereoAudioOut, QuadAudioOut)
        inside the container, as a 0..1 meter position. Falls back to silence when no level
        arrives for a while (e.g. the block stopped).
    */
    class AggregateLevel  : private juce::Timer
    {
    public:
        explicit AggregateLevel (AppContext&, juce::String containerPath = {});
        ~AggregateLevel() override;

        void setContainer (const juce::String& containerPath);
        const juce::String& getContainer() const noexcept   { return container; }

        float getLevel() const noexcept                      { return level; }
        std::function<void()> onChange;

        /** For tests: feeds a level as if a VU endpoint reported it. */
        void setLevelForTesting (float unit)                 { setLevel (unit); }

    private:
        void endpointChanged (const juce::String& path);
        void setLevel (float);
        void timerCallback() override;

        struct Endpoint { bool isMeter = false; EndpointInfo info; };

        AppContext& context;
        juce::String container;
        std::map<juce::String, Endpoint> endpoints;     // cache, keyed by endpoint path
        float level = 0.0f;
        juce::uint32 lastUpdate = 0;
    };

    //==============================================================================
    /** Horizontal meter showing an AggregateLevel in the container's colour. */
    class ContainerMeter  : public juce::Component
    {
    public:
        explicit ContainerMeter (AppContext&);

        void setContainer (const juce::String& path)    { level.setContainer (path); repaint(); }
        void setColour (juce::Colour c)                 { colour = c; repaint(); }
        AggregateLevel& getLevel() noexcept             { return level; }

        void paint (juce::Graphics&) override;

        /** Draws a meter bar (shared with views that paint their meters themselves). */
        static void paintMeter (juce::Graphics&, juce::Rectangle<float>, float level, juce::Colour);

    private:
        AggregateLevel level;
        juce::Colour colour { 0xff808080 };
    };

    //==============================================================================
    /**
        Toggles a container's "active" endpoint (Flex: ActiveButtonSkin, SetContainerActive):
        an inactive project, track or block is silent and drawn greyed out.
    */
    class ActiveButton  : public juce::Component,
                          public juce::SettableTooltipClient
    {
    public:
        explicit ActiveButton (AppContext&);
        ~ActiveButton() override;

        void setContainer (const juce::String& path);
        void setColour (juce::Colour c)     { colour = c; repaint(); }
        bool isActive() const;
        void toggle();

        std::function<void()> onChange;

        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;

        /** The power icon (shared with views that paint their buttons themselves). */
        static void paintIcon (juce::Graphics&, juce::Rectangle<float>, bool active, juce::Colour);

        /** Whether a container and all its ancestors are active (Flex: getContainerColor greys). */
        static bool isEffectivelyActive (AppContext&, const juce::String& containerPath);
        /** Sets a container's active endpoint as one undo step. */
        static void setActive (AppContext&, const juce::String& containerPath, bool active);

    private:
        AppContext& context;
        juce::String container;
        std::optional<juce::Colour> colour;     // default: the theme's accent
    };
}
