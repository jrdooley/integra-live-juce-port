#pragma once

#include "Common.h"

namespace modulecreator
{
    /**
        The "Widget Layout" tab (WidgetLayout.mxml): drag widgets to move them, drag their
        edges to resize them; edges snap to the other widgets and to a margin from them.
    */
    class WidgetLayoutPage  : public Page
    {
    public:
        explicit WidgetLayoutPage (Document&);

        void refresh() override;
        void resized() override;

        /** The canvas the widgets are laid out on. */
        class Canvas  : public juce::Component
        {
        public:
            explicit Canvas (WidgetLayoutPage& o) : owner (o)  {}

            void paint (juce::Graphics&) override;
            void mouseMove (const juce::MouseEvent&) override;
            void mouseDown (const juce::MouseEvent&) override;
            void mouseDrag (const juce::MouseEvent&) override;
            void mouseUp (const juce::MouseEvent&) override;

            enum DragEdges { left = 1, right = 2, top = 4, bottom = 8, move = 16 };

            /** The widget under a point, or -1, and which edges a drag from there moves. */
            int widgetAt (juce::Point<int>, int& edges) const;

        private:
            WidgetLayoutPage& owner;
            int dragWidget = -1, dragEdges = 0;
            juce::Rectangle<int> dragStartBounds;
            juce::Array<juce::Rectangle<int>> snapLines;
            bool dragged = false;

            int snap (int candidate, bool vertical, int draggedIndex, bool movingEdgeOnly, int extent);
        };

        Canvas& getCanvas()                         { return canvas; }

    private:
        juce::Label marginLabel { {}, "Margin" };
        juce::TextEditor margin;
        juce::ToggleButton useGuides { "Snap to guides" };
        juce::TextButton autoArrange { "Auto Arrange" };
        juce::Label hint;
        Canvas canvas { *this };
        juce::Viewport viewport;

        int getMargin() const                       { return juce::jlimit (0, 100, margin.getText().getIntValue()); }
    };
}
