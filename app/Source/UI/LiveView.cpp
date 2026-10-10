#include "LiveViewParts.h"
#include "IntegraLookAndFeel.h"

namespace integra
{
    namespace
    {
        constexpr int snapThreshold = 10;     // LiveViewBlock.maximumSnapThreshold

        /** The closest of `candidates` to `value` within the snap threshold. */
        std::optional<int> closest (int value, const std::vector<int>& candidates, int& bestDistance)
        {
            std::optional<int> best;
            for (int c : candidates)
            {
                const int d = std::abs (c - value);
                if (d < bestDistance)
                {
                    bestDistance = d;
                    best = c;
                }
            }
            return best;
        }
    }

    //==============================================================================
    juce::Rectangle<int> LiveView::LiveControl::snapped (juce::Rectangle<int> r, bool move, bool left, bool right, bool top, bool bottom)
    {
        auto* panel = dynamic_cast<TrackPanel*> (getParentComponent());
        std::vector<int> xs, ys;
        if (panel != nullptr)
        {
            for (auto* other : panel->getControls())
            {
                if (other == this)
                    continue;
                auto o = other->getBounds();
                xs.insert (xs.end(), { o.getX(), o.getRight() });
                ys.insert (ys.end(), { o.getY(), o.getBottom() });
            }
        }

        std::vector<int> snapX, snapY;
        if (move)
        {
            // doXMoveSnap / doYMoveSnap: either edge snaps, the size stays
            int dx = snapThreshold, dy = snapThreshold;
            int bestX = r.getX(), bestY = r.getY();
            if (auto s = closest (r.getX(), xs, dx))          bestX = *s;
            if (auto s = closest (r.getRight(), xs, dx))      bestX = *s - r.getWidth();
            if (auto s = closest (r.getY(), ys, dy))          bestY = *s;
            if (auto s = closest (r.getBottom(), ys, dy))     bestY = *s - r.getHeight();
            r.setPosition (juce::jmax (0, bestX), juce::jmax (topMargin, bestY));
        }
        else
        {
            // doXResizeSnap / doYResizeSnap on the edges being dragged
            int d = snapThreshold;
            if (left)   if (auto s = closest (r.getX(), xs, d))       r.setLeft (juce::jmin (*s, r.getRight() - 40));
            d = snapThreshold;
            if (right)  if (auto s = closest (r.getRight(), xs, d))   r.setRight (juce::jmax (*s, r.getX() + 40));
            d = snapThreshold;
            if (top)    if (auto s = closest (r.getY(), ys, d))       r.setTop (juce::jmin (*s, r.getBottom() - 40));
            d = snapThreshold;
            if (bottom) if (auto s = closest (r.getBottom(), ys, d))  r.setBottom (juce::jmax (*s, r.getY() + 40));
            if (r.getX() < 0)           r.setLeft (0);
            if (r.getY() < topMargin)   r.setTop (topMargin);
        }

        // the lines the control now touches
        for (int x : xs)
            if (x == r.getX() || x == r.getRight())
                snapX.push_back (x);
        for (int y : ys)
            if (y == r.getY() || y == r.getBottom())
                snapY.push_back (y);

        if (panel != nullptr && (panel->snapX != snapX || panel->snapY != snapY))
        {
            panel->snapX = snapX;
            panel->snapY = snapY;
            panel->repaint();
        }
        return r;
    }

    void LiveView::LiveControl::clearSnapLines()
    {
        if (auto* panel = dynamic_cast<TrackPanel*> (getParentComponent()))
        {
            panel->snapX.clear();
            panel->snapY.clear();
            panel->repaint();
        }
    }

    //==============================================================================
    LiveView::LiveView (AppContext& c) : context (c)
    {
        setWantsKeyboardFocus (true);
        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (true, true);
        addAndMakeVisible (viewport);
        addAndMakeVisible (projectActive);
        addAndMakeVisible (projectMeter);
        projectActive.onChange = [this] { for (auto* p : panels) p->repaint(); };
        addAndMakeVisible (keybindings);

        if (auto* settings = context.properties.getUserSettings())
        {
            keybindingsCollapsed = settings->getBoolValue ("keybindingViewCollapsed", false);
            keybindingsHeight = juce::jlimit (KeybindingView::minimumHeight, KeybindingView::maximumHeight,
                                              settings->getIntValue ("keybindingViewHeight", KeybindingView::defaultHeight));
        }

        context.model.addChangeListener (this);
        rebuild();
        startTimerHz (10);
    }

    LiveView::~LiveView()
    {
        if (keyTarget != nullptr)
            keyTarget->removeKeyListener (this);
        context.model.removeChangeListener (this);
    }

    LiveView::TrackPanel* LiveView::getPanel (const juce::String& trackPath) const
    {
        for (auto* p : panels)
            if (p->getTrackPath() == trackPath)
                return p;
        return nullptr;
    }

    juce::String LiveView::blockToShow (const model::Track& track) const
    {
        // as LiveViewTrack: only the block under the playhead is shown
        for (auto& b : track.blocks)
            if (playPosition >= b.start && playPosition < b.end)
                return b.path;

        return {};
    }

    juce::String LiveView::describeContent() const
    {
        // everything the view shows that comes from the model
        auto& p = context.model.get();
        juce::String d;
        d << p.path << "|";
        for (auto& s : p.scenes)
            d << s.name << ":" << s.userData.keybinding << ",";
        d << "|";
        for (auto& t : p.tracks)
        {
            d << t.path << ":" << t.name << ":" << t.userData.getColour().toString() << ":";
            if (auto* b = p.findBlock (blockToShow (t)))
            {
                d << b->path << "{";
                for (auto& m : b->modules)
                    d << m.name << "=" << (m.entry != nullptr ? toString (m.entry->guid) : juce::String()) << ",";
                for (auto& c : b->userData.liveViewControls)
                    d << c.moduleName << "." << c.widgetLabel << "@" << c.bounds.toString() << ",";
                d << "}";
            }
            d << ";";
        }
        return d;
    }

    void LiveView::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        rebuildIfChanged();
    }

    void LiveView::rebuildIfChanged()
    {
        // never pull the controls out from under the mouse (LiveViewBlock ignores
        // SetLiveViewControls while repositioning)
        if (content.isMouseButtonDown (true))
        {
            rebuildPending = true;
            return;
        }
        rebuildPending = false;

        if (describeContent() != shownContent)
            rebuild();
    }

    void LiveView::timerCallback()
    {
        if (! isShowing())
            return;

        playPosition = context.controller.getPlayPosition();
        rebuildIfChanged();     // e.g. the playhead moved into another block

        // highlight the current scene
        auto& p = context.model.get();
        const auto current = p.player.path.isNotEmpty() ? context.session.getValue (p.player.path + ".scene").toString() : juce::String();
        for (int i = 0; i < sceneButtons.size() && i < (int) p.scenes.size(); ++i)
            sceneButtons[i]->setToggleState (current == p.scenes[(size_t) i].name, juce::dontSendNotification);
    }

    void LiveView::parentHierarchyChanged()
    {
        updateKeyTarget();
    }

    void LiveView::updateKeyTarget()
    {
        // KeybindingView listened on the stage: scene keys work wherever the focus is,
        // except where a focused component (e.g. a text editor) uses the key itself.
        auto* top = isShowing() ? getTopLevelComponent() : nullptr;
        if (top == this)
            top = nullptr;
        if (top == keyTarget.getComponent())
            return;
        if (keyTarget != nullptr)
            keyTarget->removeKeyListener (this);
        keyTarget = top;
        if (keyTarget != nullptr)
            keyTarget->addKeyListener (this);
    }

    void LiveView::visibilityChanged()
    {
        updateKeyTarget();
        if (isVisible())
        {
            playPosition = context.controller.getPlayPosition();
            rebuild();
        }
    }

    void LiveView::rebuild()
    {
        auto& p = context.model.get();
        ++numRebuilds;

        sceneButtons.clear();
        for (auto& s : p.scenes)
        {
            auto* b = sceneButtons.add (new juce::TextButton (s.name + (s.userData.keybinding.isNotEmpty() ? "  [" + s.userData.keybinding + "]" : juce::String())));
            b->setClickingTogglesState (false);
            const auto path = s.path;
            b->onClick = [this, path] { context.controller.activateScene (path); };
            addAndMakeVisible (b);
        }

        projectActive.setContainer (p.path);
        projectMeter.setContainer (p.path);

        const auto viewPosition = viewport.getViewPosition();
        panels.clear();
        int y = 0;
        for (auto& t : p.tracks)
        {
            auto* panel = panels.add (new TrackPanel (context, t, blockToShow (t)));
            panel->setBounds (0, y, juce::jmax (600, viewport.getMaximumVisibleWidth()), panel->getContentHeight());
            content.addAndMakeVisible (panel);
            y += panel->getHeight();
        }
        content.setSize (juce::jmax (600, viewport.getMaximumVisibleWidth()), y);
        viewport.setViewPosition (viewPosition);
        shownContent = describeContent();

        resized();
        repaint();
    }

    bool LiveView::keyPressed (const juce::KeyPress& key)
    {
        return handleSceneKey (key);
    }

    bool LiveView::keyPressed (const juce::KeyPress& key, juce::Component*)
    {
        return isShowing() && handleSceneKey (key);
    }

    bool LiveView::handleSceneKey (const juce::KeyPress& key)
    {
        if (key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown() || key.getModifiers().isAltDown())
            return false;

        auto character = key.getTextCharacter();
        if (character == 0 && key.getKeyCode() > 32 && key.getKeyCode() < 127)
            character = (juce::juce_wchar) key.getKeyCode();   // some platforms deliver only the key code
        if (character == 0)
            return false;
        const auto c = juce::String::charToString (character).toUpperCase();

        for (auto& s : context.model.get().scenes)
        {
            if (s.userData.keybinding.isNotEmpty() && s.userData.keybinding.toUpperCase() == c)
            {
                context.controller.activateScene (s.path);
                keybindings.repaint();
                return true;
            }
        }
        return false;
    }

    juce::Rectangle<int> LiveView::keybindingHeaderBounds() const
    {
        return keybindingHeader;
    }

    void LiveView::setKeybindingViewCollapsed (bool shouldCollapse)
    {
        keybindingsCollapsed = shouldCollapse;
        if (auto* settings = context.properties.getUserSettings())
            settings->setValue ("keybindingViewCollapsed", keybindingsCollapsed);
        resized();
        repaint();
    }

    void LiveView::mouseDown (const juce::MouseEvent& e)
    {
        // the Scene Shortcuts title bar: click the arrow to collapse, drag to resize
        resizingKeybindings = false;
        if (! keybindingHeader.contains (e.getPosition()))
            return;
        if (e.x < 30)
            setKeybindingViewCollapsed (! keybindingsCollapsed);
        else
            resizingKeybindings = ! keybindingsCollapsed;
        dragStartHeight = keybindings.getHeight();
    }

    void LiveView::mouseDrag (const juce::MouseEvent& e)
    {
        if (! resizingKeybindings)
            return;
        keybindingsHeight = juce::jlimit (KeybindingView::minimumHeight, KeybindingView::maximumHeight, dragStartHeight - e.getDistanceFromDragStartY());
        if (auto* settings = context.properties.getUserSettings())
            settings->setValue ("keybindingViewHeight", keybindingsHeight);
        resized();
        repaint();
    }

    void LiveView::paint (juce::Graphics& g)
    {
        auto& t = Theme::get();
        g.fillAll (t.background);
        if (context.model.get().tracks.empty())
        {
            g.setColour (t.textDim);
            g.drawText ("No tracks", viewport.getBounds(), juce::Justification::centred);
        }

        auto header = keybindingHeaderBounds();
        g.setColour (t.header);
        g.fillRect (header);
        g.setColour (t.textDim);
        juce::Path arrow;
        auto a = header.removeFromLeft (30).toFloat().withSizeKeepingCentre (9.0f, 9.0f);
        if (keybindingsCollapsed)
            arrow.addTriangle (a.getTopLeft(), a.getBottomLeft(), { a.getRight(), a.getCentreY() });
        else
            arrow.addTriangle (a.getTopLeft(), a.getTopRight(), { a.getCentreX(), a.getBottom() });
        g.fillPath (arrow);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText ("Scene Shortcuts", header, juce::Justification::centredLeft);
    }

    void LiveView::resized()
    {
        auto area = getLocalBounds();

        auto bar = area.removeFromTop (36).reduced (6, 5);
        projectMeter.setBounds (bar.removeFromRight (128).withSizeKeepingCentre (128, 6));
        bar.removeFromRight (8);
        projectActive.setBounds (bar.removeFromRight (20).withSizeKeepingCentre (20, 20));
        bar.removeFromRight (12);
        for (auto* b : sceneButtons)
        {
            b->setBounds (bar.removeFromLeft (juce::jmin (160, juce::jmax (90, bar.getWidth() / juce::jmax (1, sceneButtons.size())))));
            bar.removeFromLeft (6);
        }

        // the bottom row: Scene Shortcuts, and the Info view at its right (Flex: _liveInfoContainer)
        const int rowHeight = juce::jmin (area.getHeight() / 2, keybindingsCollapsed ? (infoWidth > 0 ? 180 : keybindingHeaderHeight)
                                                                                       : keybindingHeaderHeight + keybindingsHeight);
        auto row = area.removeFromBottom (juce::jmax (keybindingHeaderHeight, rowHeight));
        infoArea = {};
        if (infoWidth > 0)
            infoArea = row.removeFromRight (juce::jmin (infoWidth, row.getWidth() / 2));
        keybindings.setVisible (! keybindingsCollapsed);
        if (keybindingsCollapsed)
        {
            keybindingHeader = row.removeFromBottom (keybindingHeaderHeight);
            area = area.getUnion (row);     // the rest of the row stays with the tracks
        }
        else
        {
            keybindingHeader = row.removeFromTop (keybindingHeaderHeight);
            keybindings.setBounds (row);
        }
        viewport.setBounds (area);
    }
}
