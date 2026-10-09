#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <map>

namespace integra
{
    /*
        GUI state that Integra Live persists inside the node tree, as an XML string
        in each container's "userData" endpoint. The element names and layout are
        exactly those written by the Flex GUI (components/model/userData/) so
        that .integra files remain interchangeable between the two front ends.

        Unknown elements are preserved on round-trip.
    */

    /** Common to all userData: selection state. */
    struct UserDataBase
    {
        juce::String primarySelectedChild;
        juce::StringArray selectedChildren;

        /** Elements we didn't parse (serialised), kept so we write them back unchanged. */
        juce::StringArray unknownElements;

        void readBase (const juce::XmlElement&, const juce::StringArray& handledTags);
        void writeBase (juce::XmlElement&) const;

        static std::unique_ptr<juce::XmlElement> parse (const juce::String& text);
    };

    /** Module positions are in Flex "grid units" (multiples of a text row height). */
    struct BlockUserData  : UserDataBase
    {
        struct LiveViewControl
        {
            juce::String moduleName;
            juce::String widgetLabel;      // "controlInstanceName" = the widget's label in the module's IID
            juce::Rectangle<float> bounds; // pixels within the block's live-view area
        };

        std::map<juce::String, juce::Rectangle<float>> modulePositions;
        std::vector<LiveViewControl> liveViewControls;
        bool envelopeLock = false;
        bool curvatureMode = false;

        static BlockUserData fromString (const juce::String&);
        juce::String toString() const;

        const LiveViewControl* findLiveViewControl (const juce::String& module, const juce::String& widget) const;
        void removeModule (const juce::String& moduleName);
        void renameModule (const juce::String& oldName, const juce::String& newName);
    };

    struct TrackUserData  : UserDataBase
    {
        juce::uint32 colour = 0xff6699cc;  // Flex stored an RGB uint
        int arrangeViewHeight = 64;
        bool arrangeViewExpanded = true;
        bool liveViewExpanded = true;

        static TrackUserData fromString (const juce::String&);
        juce::String toString() const;

        juce::Colour getColour() const  { return juce::Colour (0xff000000 | colour); }
    };

    struct ProjectUserData  : UserDataBase
    {
        juce::String viewMode = "arrange";        // "arrange" or "live" (Flex ViewMode.ARRANGE / LIVE)
        bool blockPropertiesOpen = false;
        juce::StringArray popups;
        double timelineScroll = 0.0;              // ticks
        double timelineZoom = 0.4;                // pixels per tick (Flex range 0.04 .. 25)
        juce::String colorScheme = "light";       // "light" or "dark"
        bool highContrast = false;
        bool showSceneInTitlebar = false;

        static ProjectUserData fromString (const juce::String&);
        juce::String toString() const;
    };

    struct SceneUserData  : UserDataBase
    {
        juce::String keybinding;                  // single character, or empty

        static SceneUserData fromString (const juce::String&);
        juce::String toString() const;
    };
}
