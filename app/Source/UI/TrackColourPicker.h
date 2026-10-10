#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Core/AppContext.h"

namespace integra
{
    /**
        The track colour wheel (Flex: ArrangeView/TrackColorPicker.as): a 128 px disc of hues
        that pops up at the mouse. Pressing on it and dragging shows the colour on the track
        as it changes (outside the wheel, the nearest colour on its rim); releasing sets it as
        one undo step and closes the wheel. A click outside it closes it unchanged.
    */
    class TrackColourPicker  : public juce::Component
    {
    public:
        /** Opens the wheel with its top-left corner at a point of `anchor`. */
        static TrackColourPicker* show (AppContext&, const juce::String& trackPath, juce::Component& anchor, juce::Point<int> position);
        /** The open wheel, if any (for tests). */
        static TrackColourPicker* getCurrent() noexcept;

        ~TrackColourPicker() override;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        bool hitTest (int x, int y) override;

        /** The colour shown at a point of the wheel (clamped to its rim). */
        juce::Colour colourAt (juce::Point<float>) const;
        /** Where the crosshair is drawn: the point whose colour is nearest the track's. */
        juce::Point<int> getCrosshair() const noexcept     { return crosshair; }
        void close();

        static constexpr int diameter = 128;

    private:
        TrackColourPicker (AppContext&, const juce::String& trackPath);
        void globalMouseDown (const juce::MouseEvent&);
        void choose (juce::Point<float>);
        static juce::Colour wheelColour (float bearing, float distance);
        juce::Point<float> clampToWheel (juce::Point<float>) const;

        struct OutsideClickListener  : public juce::MouseListener
        {
            explicit OutsideClickListener (TrackColourPicker& p) : picker (p) {}
            void mouseDown (const juce::MouseEvent& e) override    { picker.globalMouseDown (e); }
            TrackColourPicker& picker;
        } outsideClicks { *this };

        AppContext& context;
        juce::String trackPath;
        juce::Image wheel;
        juce::Colour original;
        juce::Point<int> crosshair;
        bool choosing = false, closing = false;
    };
}
