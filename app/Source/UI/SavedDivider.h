#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace integra
{
    /** A layout divider that reports when the user has dragged it, so the panel size can be
        remembered (Flex: arrangeViewPropertiesHeight / modulePropertiesHeight in the
        application state). */
    class SavedDivider  : public juce::StretchableLayoutResizerBar
    {
    public:
        using juce::StretchableLayoutResizerBar::StretchableLayoutResizerBar;
        std::function<void()> onMoved;

        void hasBeenMoved() override
        {
            juce::StretchableLayoutResizerBar::hasBeenMoved();
            if (onMoved)
                onMoved();
        }
    };

    /** Restores, once, a saved height for the last item of a vertical layout of three
        (top, divider, bottom), then lays it out again. */
    inline void restoreBottomHeight (juce::StretchableLayoutManager& layout, juce::Component** comps, juce::Rectangle<int> area,
                                     int savedHeight, bool& restored)
    {
        if (restored || savedHeight <= 0 || area.getHeight() < 200)
            return;
        restored = true;
        layout.setItemPosition (1, juce::jlimit (150, area.getHeight() - 106, area.getHeight() - savedHeight - 6));
        layout.layOutComponents (comps, 3, area.getX(), area.getY(), area.getWidth(), area.getHeight(), true, true);
    }
}
