#include "IntegraLookAndFeel.h"
#include <juce_gui_extra/juce_gui_extra.h>

namespace integra
{
    namespace
    {
        Theme makeLight()
        {
            Theme t;
            t.isDark              = false;
            t.background          = juce::Colour (0xffe6e6e6);
            t.panel               = juce::Colour (0xfff4f4f4);
            t.panelAlt            = juce::Colour (0xffdcdcdc);
            t.header              = juce::Colour (0xffc8c8c8);
            t.outline             = juce::Colour (0xffa0a0a0);
            t.text                = juce::Colour (0xff202020);
            t.textDim             = juce::Colour (0xff6a6a6a);
            t.accent              = juce::Colour (0xff0f8aeb);
            t.accentText          = juce::Colours::white;
            t.selection           = juce::Colour (0xff0f8aeb).withAlpha (0.35f);
            t.timelineBackground  = juce::Colour (0xffd0d0d0);
            t.gridLine            = juce::Colour (0x22000000);
            t.playhead            = juce::Colour (0xffe03030);
            t.blockFill           = juce::Colour (0xfffafafa);
            t.blockOutline        = juce::Colour (0xff707070);
            t.moduleFill          = juce::Colour (0xfffdfdfd);
            t.moduleHeader        = juce::Colour (0xffd8d8d8);
            t.pin                 = juce::Colour (0xff505050);
            t.link                = juce::Colour (0xff404040);
            t.linkSelected        = juce::Colour (0xff0f8aeb);
            t.sceneFill           = juce::Colour (0xffeeaa33);
            t.envelope            = juce::Colour (0xffd04020);
            t.meterLow            = juce::Colour (0xff2eb429);
            t.meterHigh           = juce::Colour (0xffe03030);
            t.controlBackground   = juce::Colour (0xffd4d4d4);
            t.controlFill         = juce::Colour (0xff6a6a6a);
            return t;
        }

        Theme makeDark()
        {
            Theme t;
            t.isDark              = true;
            t.background          = juce::Colour (0xff1e1e1e);
            t.panel               = juce::Colour (0xff2a2a2a);
            t.panelAlt            = juce::Colour (0xff333333);
            t.header              = juce::Colour (0xff3c3c3c);
            t.outline             = juce::Colour (0xff5a5a5a);
            t.text                = juce::Colour (0xffe8e8e8);
            t.textDim             = juce::Colour (0xff9a9a9a);
            t.accent              = juce::Colour (0xff3aa0f0);
            t.accentText          = juce::Colours::white;
            t.selection           = juce::Colour (0xff3aa0f0).withAlpha (0.35f);
            t.timelineBackground  = juce::Colour (0xff252525);
            t.gridLine            = juce::Colour (0x22ffffff);
            t.playhead            = juce::Colour (0xffff5050);
            t.blockFill           = juce::Colour (0xff3a3a3a);
            t.blockOutline        = juce::Colour (0xffa0a0a0);
            t.moduleFill          = juce::Colour (0xff383838);
            t.moduleHeader        = juce::Colour (0xff4a4a4a);
            t.pin                 = juce::Colour (0xffc0c0c0);
            t.link                = juce::Colour (0xffd0d0d0);
            t.linkSelected        = juce::Colour (0xff3aa0f0);
            t.sceneFill           = juce::Colour (0xffd09020);
            t.envelope            = juce::Colour (0xffff7050);
            t.meterLow            = juce::Colour (0xff40d040);
            t.meterHigh           = juce::Colour (0xffff5050);
            t.controlBackground   = juce::Colour (0xff454545);
            t.controlFill         = juce::Colour (0xffb8b8b8);
            return t;
        }

        Theme& current()
        {
            static Theme theme = makeLight();
            return theme;
        }
    }

    namespace
    {
        bool highContrastOn = false;

        /** MainViewContainer.updateColorSchemeAndContrast: every channel twice as far from
            black (dark scheme) or from white (light scheme). */
        Theme withHighContrast (Theme t)
        {
            constexpr float multiplier = 2.0f;
            auto adjust = [&t] (juce::Colour& c)
            {
                auto channel = [&t] (juce::uint8 v)
                {
                    const float f = v / 255.0f;
                    const float r = t.isDark ? f * multiplier : 1.0f - (1.0f - f) * multiplier;
                    return (juce::uint8) juce::jlimit (0, 255, juce::roundToInt (r * 255.0f));
                };
                c = juce::Colour (channel (c.getRed()), channel (c.getGreen()), channel (c.getBlue()), c.getAlpha());
            };
            for (auto* c : { &t.background, &t.panel, &t.panelAlt, &t.header, &t.outline, &t.text, &t.textDim, &t.accent, &t.accentText,
                             &t.selection, &t.timelineBackground, &t.gridLine, &t.playhead, &t.blockFill, &t.blockOutline,
                             &t.moduleFill, &t.moduleHeader, &t.pin, &t.link, &t.linkSelected, &t.sceneFill, &t.envelope,
                             &t.meterLow, &t.meterHigh, &t.controlBackground, &t.controlFill })
                adjust (*c);
            t.highContrast = true;
            return t;
        }

        void rebuild (bool dark)
        {
            current() = dark ? makeDark() : makeLight();
            if (highContrastOn)
                current() = withHighContrast (current());
        }
    }

    const Theme& Theme::get()            { return current(); }
    void Theme::setDark (bool dark)      { rebuild (dark); }
    bool Theme::isDarkScheme()           { return current().isDark; }
    void Theme::setHighContrast (bool on)   { highContrastOn = on; rebuild (current().isDark); }
    bool Theme::isHighContrastScheme()   { return highContrastOn; }

    //==============================================================================
    IntegraLookAndFeel::IntegraLookAndFeel()
    {
        applyTheme();
    }

    void IntegraLookAndFeel::applyTheme()
    {
        auto& t = Theme::get();

        auto scheme = t.isDark ? getDarkColourScheme() : getLightColourScheme();
        scheme.setUIColour (ColourScheme::windowBackground, t.background);
        scheme.setUIColour (ColourScheme::widgetBackground, t.panel);
        scheme.setUIColour (ColourScheme::menuBackground, t.panel);
        scheme.setUIColour (ColourScheme::outline, t.outline);
        scheme.setUIColour (ColourScheme::defaultText, t.text);
        scheme.setUIColour (ColourScheme::defaultFill, t.accent);
        scheme.setUIColour (ColourScheme::highlightedText, t.accentText);
        scheme.setUIColour (ColourScheme::highlightedFill, t.accent);
        scheme.setUIColour (ColourScheme::menuText, t.text);
        setColourScheme (scheme);

        setColour (juce::ResizableWindow::backgroundColourId, t.background);
        setColour (juce::TextButton::buttonColourId, t.panelAlt);
        setColour (juce::TextButton::buttonOnColourId, t.accent);
        setColour (juce::TextButton::textColourOffId, t.text);
        setColour (juce::TextButton::textColourOnId, t.accentText);
        setColour (juce::Label::textColourId, t.text);
        setColour (juce::TextEditor::backgroundColourId, t.panel);
        setColour (juce::TextEditor::textColourId, t.text);
        setColour (juce::TextEditor::outlineColourId, t.outline);
        setColour (juce::TextEditor::focusedOutlineColourId, t.accent);
        setColour (juce::ComboBox::backgroundColourId, t.panel);
        setColour (juce::ComboBox::textColourId, t.text);
        setColour (juce::ComboBox::outlineColourId, t.outline);
        setColour (juce::ListBox::backgroundColourId, t.panel);
        setColour (juce::ListBox::textColourId, t.text);
        setColour (juce::Slider::trackColourId, t.controlFill);
        setColour (juce::Slider::backgroundColourId, t.controlBackground);
        setColour (juce::Slider::thumbColourId, t.accent);
        setColour (juce::Slider::rotarySliderFillColourId, t.controlFill);
        setColour (juce::Slider::rotarySliderOutlineColourId, t.controlBackground);
        setColour (juce::Slider::textBoxTextColourId, t.text);
        setColour (juce::Slider::textBoxBackgroundColourId, t.panel);
        setColour (juce::Slider::textBoxOutlineColourId, t.outline);
        setColour (juce::ToggleButton::textColourId, t.text);
        setColour (juce::ToggleButton::tickColourId, t.accent);
        setColour (juce::ToggleButton::tickDisabledColourId, t.outline);
        setColour (juce::TabbedButtonBar::tabOutlineColourId, t.outline);
        setColour (juce::TabbedComponent::backgroundColourId, t.panel);
        setColour (juce::TabbedComponent::outlineColourId, t.outline);
        setColour (juce::CodeEditorComponent::backgroundColourId, t.panel);
        setColour (juce::CodeEditorComponent::defaultTextColourId, t.text);
        setColour (juce::CodeEditorComponent::lineNumberBackgroundId, t.panelAlt);
        setColour (juce::CodeEditorComponent::lineNumberTextId, t.textDim);
        setColour (juce::CodeEditorComponent::highlightColourId, t.selection);
        setColour (juce::CaretComponent::caretColourId, t.text);
        setColour (juce::ScrollBar::thumbColourId, t.outline);
        setColour (juce::PopupMenu::backgroundColourId, t.panel);
        setColour (juce::PopupMenu::textColourId, t.text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, t.accent);
        setColour (juce::PopupMenu::highlightedTextColourId, t.accentText);
        setColour (juce::AlertWindow::backgroundColourId, t.panel);
        setColour (juce::AlertWindow::textColourId, t.text);
        setColour (juce::AlertWindow::outlineColourId, t.outline);
        setColour (juce::TreeView::backgroundColourId, t.panel);
    }

    juce::Font IntegraLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
    {
        return juce::Font (juce::FontOptions (juce::jmin (14.0f, (float) buttonHeight * 0.6f)));
    }

    juce::Font IntegraLookAndFeel::getLabelFont (juce::Label& label)
    {
        return label.getFont();
    }

    juce::Font IntegraLookAndFeel::getComboBoxFont (juce::ComboBox& box)
    {
        return juce::Font (juce::FontOptions (juce::jmin (14.0f, (float) box.getHeight() * 0.6f)));
    }

    juce::Font IntegraLookAndFeel::getPopupMenuFont()
    {
        return juce::Font (juce::FontOptions (14.0f));
    }

    void IntegraLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                                   bool isMouseOverButton, bool isButtonDown)
    {
        auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        auto colour = backgroundColour;

        if (isButtonDown)           colour = colour.contrasting (0.2f);
        else if (isMouseOverButton) colour = colour.contrasting (0.08f);

        g.setColour (colour);
        g.fillRoundedRectangle (bounds, 3.0f);
        g.setColour (Theme::get().outline);
        g.drawRoundedRectangle (bounds, 3.0f, 1.0f);
    }

    void IntegraLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                               float sliderPos, float minSliderPos, float maxSliderPos,
                                               juce::Slider::SliderStyle style, juce::Slider& slider)
    {
        auto& t = Theme::get();
        auto area = juce::Rectangle<int> (x, y, width, height).toFloat();

        if (style == juce::Slider::LinearBar || style == juce::Slider::LinearBarVertical)
        {
            g.setColour (t.controlBackground);
            g.fillRect (area);
            g.setColour (t.controlFill);
            if (style == juce::Slider::LinearBarVertical)
                g.fillRect (area.withTop (sliderPos));
            else
                g.fillRect (area.withRight (sliderPos));
            return;
        }

        const bool vertical = slider.isVertical();
        const float trackWidth = vertical ? juce::jmin (14.0f, area.getWidth() * 0.5f) : juce::jmin (14.0f, area.getHeight() * 0.5f);

        auto track = vertical ? area.withSizeKeepingCentre (trackWidth, area.getHeight())
                              : area.withSizeKeepingCentre (area.getWidth(), trackWidth);

        g.setColour (t.controlBackground);
        g.fillRoundedRectangle (track, 2.0f);

        juce::Rectangle<float> filled;
        if (style == juce::Slider::TwoValueHorizontal || style == juce::Slider::TwoValueVertical)
        {
            filled = vertical ? track.withTop (maxSliderPos).withBottom (minSliderPos)
                              : track.withLeft (minSliderPos).withRight (maxSliderPos);
        }
        else
        {
            filled = vertical ? track.withTop (sliderPos) : track.withRight (sliderPos);
        }

        g.setColour (slider.isEnabled() ? t.controlFill : t.outline);
        g.fillRoundedRectangle (filled, 2.0f);

        g.setColour (t.outline);
        g.drawRoundedRectangle (track, 2.0f, 1.0f);
    }

    void IntegraLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                               float startAngle, float endAngle, juce::Slider& slider)
    {
        auto& t = Theme::get();
        auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
        auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
        auto centre = bounds.getCentre();
        const float lineW = juce::jmax (3.0f, radius * 0.18f);
        auto arcRadius = radius - lineW * 0.5f;
        const float angle = startAngle + pos * (endAngle - startAngle);

        juce::Path background;
        background.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
        g.setColour (t.controlBackground);
        g.strokePath (background, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, angle, true);
        g.setColour (slider.isEnabled() ? t.controlFill : t.outline);
        g.strokePath (value, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Point<float> tip (centre.x + (arcRadius - lineW) * std::sin (angle), centre.y - (arcRadius - lineW) * std::cos (angle));
        g.setColour (t.text);
        g.drawLine ({ centre, tip }, 2.0f);
    }

    void IntegraLookAndFeel::drawTabButton (juce::TabBarButton& button, juce::Graphics& g, bool isMouseOver, bool)
    {
        auto& t = Theme::get();
        auto area = button.getActiveArea().toFloat();
        const bool front = button.isFrontTab();

        g.setColour (front ? t.panel : (isMouseOver ? t.panelAlt.brighter (0.05f) : t.panelAlt));
        g.fillRect (area);

        if (front)
        {
            g.setColour (t.accent);
            g.fillRect (area.removeFromTop (3.0f));
        }

        g.setColour (front ? t.text : t.textDim);
        g.setFont (juce::FontOptions (14.0f, front ? juce::Font::bold : juce::Font::plain));
        g.drawText (button.getButtonText(), button.getTextArea(), juce::Justification::centred);
    }
}
