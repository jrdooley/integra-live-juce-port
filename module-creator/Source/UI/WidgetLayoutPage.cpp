#include "WidgetLayoutPage.h"

namespace modulecreator
{
    namespace
    {
        constexpr int resizeMargin = 5;             // WidgetLayoutElement._resizeMargin
        constexpr int snapThreshold = 10;           // WidgetLayout._maximumSnapThreshold
    }

    WidgetLayoutPage::WidgetLayoutPage (Document& d) : Page (d)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &marginLabel, &margin, &useGuides, &autoArrange, &hint, &viewport })
            addAndMakeVisible (c);

        styleEditor (margin);
        margin.setText ("10", false);
        margin.setInputRestrictions (3, "0123456789");
        margin.setTooltip ("The gap kept between widgets when snapping and arranging");
        marginLabel.setJustificationType (juce::Justification::centredRight);
        useGuides.setToggleState (true, juce::dontSendNotification);
        useGuides.setTooltip ("Snap widget edges to the other widgets' edges, and to the margin beside them");
        autoArrange.setTooltip ("Line the widgets up in a row, left to right");
        autoArrange.onClick = [this] { edit ([this] (ModuleDefinition& m) { m.autoArrangeWidgets (getMargin()); }); };
        hint.setColour (juce::Label::textColourId, Theme::get().textDim);
        hint.setFont (juce::FontOptions (13.0f));

        viewport.setViewedComponent (&canvas, false);
        refresh();
    }

    void WidgetLayoutPage::refresh()
    {
        hint.setText (document.definition.widgets.empty() ? "Choose widgets for control endpoints on the Endpoints tab; lay them out here."
                                                          : "Drag widgets to move them; drag their edges to resize them.",
                      juce::dontSendNotification);
        resized();
        canvas.repaint();
    }

    void WidgetLayoutPage::resized()
    {
        auto r = getLocalBounds().reduced (8);
        auto bar = r.removeFromTop (rowHeight);
        r.removeFromTop (8);
        marginLabel.setBounds (bar.removeFromLeft (60));
        margin.setBounds (bar.removeFromLeft (50));
        bar.removeFromLeft (12);
        useGuides.setBounds (bar.removeFromLeft (140));
        autoArrange.setBounds (bar.removeFromLeft (120));
        bar.removeFromLeft (12);
        hint.setBounds (bar);
        viewport.setBounds (r);

        int w = viewport.getMaximumVisibleWidth(), h = viewport.getMaximumVisibleHeight();
        for (auto& widget : document.definition.widgets)
        {
            w = juce::jmax (w, widget.bounds.getRight() + 200);
            h = juce::jmax (h, widget.bounds.getBottom() + 200);
        }
        canvas.setSize (w, h);
    }

    //==============================================================================
    int WidgetLayoutPage::Canvas::widgetAt (juce::Point<int> p, int& edges) const
    {
        auto& widgets = owner.document.definition.widgets;
        for (int i = (int) widgets.size(); --i >= 0;)
        {
            auto b = widgets[(size_t) i].bounds;
            if (! b.contains (p))
                continue;
            // WidgetLayoutElement.getDragType
            edges = 0;
            if (p.x < b.getX() + resizeMargin)              edges |= left;
            else if (p.x >= b.getRight() - resizeMargin)    edges |= right;
            if (p.y < b.getY() + resizeMargin)              edges |= top;
            else if (p.y >= b.getBottom() - resizeMargin)   edges |= bottom;
            if (edges == 0)
                edges = move;
            return i;
        }
        edges = 0;
        return -1;
    }

    void WidgetLayoutPage::Canvas::paint (juce::Graphics& g)
    {
        auto& t = Theme::get();
        auto& d = owner.document.definition;
        g.fillAll (t.panelAlt);

        // a 10 pixel grid, faint
        g.setColour (t.gridLine.withMultipliedAlpha (0.35f));
        for (int x = 0; x < getWidth(); x += 10)
            g.drawVerticalLine (x, 0.0f, (float) getHeight());
        for (int y = 0; y < getHeight(); y += 10)
            g.drawHorizontalLine (y, 0.0f, (float) getWidth());

        for (int i = 0; i < (int) d.widgets.size(); ++i)
        {
            auto& w = d.widgets[(size_t) i];
            auto b = w.bounds.toFloat();
            g.setColour (t.moduleFill.withAlpha (i == dragWidget ? 0.95f : 0.8f));
            g.fillRect (b);
            g.setColour (i == dragWidget ? t.accent : t.outline);
            g.drawRect (b, 2.0f);

            // WidgetLayoutElement.updateLabel
            juce::String text = d.labelOf (w);
            if (w.mappings.size() > 1)
                for (auto& [attribute, uid] : w.mappings)
                    if (auto* e = d.findEndpoint (uid))
                        text << "\n" << attribute << juce::String::fromUTF8 (" \xe2\x86\x92 ") << e->name;
            g.setColour (t.text);
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawFittedText (text, w.bounds.reduced (6), juce::Justification::centred, 8, 0.8f);
            g.setColour (t.textDim);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (w.type, w.bounds.reduced (4, 2), juce::Justification::bottomRight);
        }

        g.setColour (juce::Colour (0x4d808080));
        for (auto& line : snapLines)
            g.fillRect (line);
    }

    void WidgetLayoutPage::Canvas::mouseMove (const juce::MouseEvent& e)
    {
        int edges = 0;
        widgetAt (e.getPosition(), edges);
        using MC = juce::MouseCursor;
        auto newCursor = MC::NormalCursor;
        if (edges == move)                                          newCursor = MC::DraggingHandCursor;
        else if (edges == (left | top))                             newCursor = MC::TopLeftCornerResizeCursor;
        else if (edges == (right | bottom))                         newCursor = MC::BottomRightCornerResizeCursor;
        else if (edges == (right | top))                            newCursor = MC::TopRightCornerResizeCursor;
        else if (edges == (left | bottom))                          newCursor = MC::BottomLeftCornerResizeCursor;
        else if (edges == left || edges == right)                   newCursor = MC::LeftRightResizeCursor;
        else if (edges == top || edges == bottom)                   newCursor = MC::UpDownResizeCursor;
        setMouseCursor (newCursor);
    }

    void WidgetLayoutPage::Canvas::mouseDown (const juce::MouseEvent& e)
    {
        dragWidget = widgetAt (e.getPosition(), dragEdges);
        dragged = false;
        if (dragWidget >= 0)
            dragStartBounds = owner.document.definition.widgets[(size_t) dragWidget].bounds;
        repaint();
    }

    int WidgetLayoutPage::Canvas::snap (int candidate, bool vertical, int draggedIndex, bool movingEdgeOnly, int extent)
    {
        // doXMoveSnap / doXResizeSnap and their y versions, in one: `candidate` is the dragged
        // widget's leading edge (or, for a resize, the edge being moved); `extent` its size
        if (! owner.useGuides.getToggleState())
            return candidate;

        const int margin = owner.getMargin();
        int best = candidate, bestDistance = snapThreshold;
        juce::Rectangle<int> line;
        auto& widgets = owner.document.definition.widgets;

        auto consider = [&] (int snapped, int lineStart, int lineWidth)
        {
            const int distance = std::abs (snapped - candidate);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = snapped;
                line = vertical ? juce::Rectangle<int> (0, lineStart, getWidth(), juce::jmax (1, lineWidth))
                                : juce::Rectangle<int> (lineStart, 0, juce::jmax (1, lineWidth), getHeight());
            }
        };

        for (int i = 0; i < (int) widgets.size(); ++i)
        {
            if (i == draggedIndex)
                continue;
            auto b = widgets[(size_t) i].bounds;
            const int start = vertical ? b.getY() : b.getX();
            const int end = vertical ? b.getBottom() : b.getRight();

            if (movingEdgeOnly)
            {
                // the moving edge meets another widget's edge, or keeps the margin from it
                consider (start, start, 1);
                consider (end, end, 1);
                consider (start - margin, start - margin, margin);
                consider (end + margin, end, margin);
            }
            else
            {
                consider (start, start, 1);                                     // left edges line up
                consider (end + margin, end, margin);                           // beside it, after the margin
                consider (start - extent - margin, start - margin, margin);     // before it, with the margin
                consider (end - extent, end, 1);                                // right edges line up
            }
        }

        if (bestDistance < snapThreshold)
            snapLines.add (line);
        return best;
    }

    void WidgetLayoutPage::Canvas::mouseDrag (const juce::MouseEvent& e)
    {
        auto& d = owner.document.definition;
        if (dragWidget < 0 || dragWidget >= (int) d.widgets.size())
            return;
        if (! dragged && e.getDistanceFromDragStart() < 2)
            return;
        dragged = true;

        auto& widget = d.widgets[(size_t) dragWidget];
        auto* definition = owner.document.config.findWidget (widget.type);
        const auto minSize = definition != nullptr ? definition->minimumSize : juce::Point<int> (20, 20);
        const auto maxSize = definition != nullptr ? definition->maximumSize : juce::Point<int> (1000, 1000);
        const auto delta = e.getOffsetFromDragStart();
        const auto p = e.getPosition();
        auto b = dragStartBounds;
        snapLines.clear();

        if (dragEdges == move)
        {
            b.setX (juce::jmax (0, snap (dragStartBounds.getX() + delta.x, false, dragWidget, false, b.getWidth())));
            b.setY (juce::jmax (0, snap (dragStartBounds.getY() + delta.y, true, dragWidget, false, b.getHeight())));
        }
        else
        {
            // resizeLeft / resizeRight / resizeTop / resizeBottom, within the widget's size limits
            if (dragEdges & left)
            {
                const int rightEdge = b.getRight();
                b.setLeft (juce::jlimit (juce::jmax (0, rightEdge - maxSize.x), rightEdge - minSize.x, snap (p.x, false, dragWidget, true, 0)));
            }
            if (dragEdges & right)
                b.setWidth (juce::jlimit (minSize.x, maxSize.x, snap (p.x, false, dragWidget, true, 0) - b.getX()));
            if (dragEdges & top)
            {
                const int bottomEdge = b.getBottom();
                b.setTop (juce::jlimit (juce::jmax (0, bottomEdge - maxSize.y), bottomEdge - minSize.y, snap (p.y, true, dragWidget, true, 0)));
            }
            if (dragEdges & bottom)
                b.setHeight (juce::jlimit (minSize.y, maxSize.y, snap (p.y, true, dragWidget, true, 0) - b.getY()));
        }

        widget.bounds = b;
        repaint();
    }

    void WidgetLayoutPage::Canvas::mouseUp (const juce::MouseEvent&)
    {
        snapLines.clear();
        const bool changed = dragged && dragWidget >= 0 && dragWidget < (int) owner.document.definition.widgets.size()
                             && owner.document.definition.widgets[(size_t) dragWidget].bounds != dragStartBounds;
        dragWidget = -1;
        dragged = false;
        if (changed)
            owner.document.changed();
        repaint();
    }
}
