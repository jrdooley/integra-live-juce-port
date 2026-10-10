#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace integra
{
    /**
        Colours for Integra Live's "light" and "dark" colour schemes (the two schemes the
        Flex GUI offered, stored per project in ProjectUserData::colorScheme).
    */
    struct Theme
    {
        juce::Colour background, panel, panelAlt, header, outline, text, textDim, accent, accentText,
                     selection, timelineBackground, gridLine, playhead, blockFill, blockOutline,
                     moduleFill, moduleHeader, pin, link, linkSelected, sceneFill, envelope, meterLow, meterHigh,
                     controlBackground, controlFill;
        bool isDark = false, highContrast = false;

        static const Theme& get();
        static void setDark (bool dark);
        static bool isDarkScheme();
        /** High contrast (Flex: View > High Contrast, stored per project). */
        static void setHighContrast (bool);
        static bool isHighContrastScheme();
    };

    class IntegraLookAndFeel  : public juce::LookAndFeel_V4
    {
    public:
        IntegraLookAndFeel();

        /** Re-applies colours after Theme::setDark(). */
        void applyTheme();

        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
        juce::Font getLabelFont (juce::Label&) override;
        juce::Font getComboBoxFont (juce::ComboBox&) override;
        juce::Font getPopupMenuFont() override;

        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                                   bool isMouseOverButton, bool isButtonDown) override;

        void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                               float sliderPos, float minSliderPos, float maxSliderPos,
                               juce::Slider::SliderStyle, juce::Slider&) override;

        void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPosProportional,
                               float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

        void drawTabButton (juce::TabBarButton&, juce::Graphics&, bool isMouseOver, bool isMouseDown) override;
    };
}
