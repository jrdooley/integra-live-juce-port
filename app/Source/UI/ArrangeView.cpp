#include "ArrangeView.h"
#include "IntegraLookAndFeel.h"

namespace integra
{
    namespace
    {
        juce::String formatTime (double seconds)
        {
            const int total = (int) std::floor (seconds);
            return juce::String::formatted ("%d:%02d", total / 60, total % 60);
        }

        double gridSecondsFor (double pixelsPerSecond)
        {
            for (double s : { 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0, 300.0, 600.0, 1200.0 })
                if (s * pixelsPerSecond >= 70.0)
                    return s;
            return 3600.0;
        }

        int snapTicks (int ticks, int rate)
        {
            const int quantum = juce::jmax (1, rate / 4);   // quarter seconds
            return (int) std::round ((double) ticks / quantum) * quantum;
        }

        void askForName (const juce::String& title, const juce::String& current, std::function<void (juce::String)> done)
        {
            auto* w = new juce::AlertWindow (title, "New name:", juce::MessageBoxIconType::NoIcon);
            w->addTextEditor ("name", current);
            w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            w->enterModalState (true, juce::ModalCallbackFunction::create ([w, done] (int result)
            {
                if (result == 1)
                    done (w->getTextEditorContents ("name"));
            }), true);
        }
    }

    //==============================================================================
    TimelineRuler::TimelineRuler (AppContext& c, TimelineMapping& m) : context (c), mapping (m) {}

    juce::Rectangle<float> TimelineRuler::sceneBounds (int start, int end) const
    {
        const float x1 = mapping.tickToX (start), x2 = mapping.tickToX (end);
        return { x1, 2.0f, juce::jmax (4.0f, x2 - x1), (float) sceneBarHeight - 4.0f };
    }

    juce::Range<int> TimelineRuler::displayedRange (const model::Scene& s) const
    {
        auto it = repositioned.find (s.path);
        return it != repositioned.end() ? it->second : juce::Range<int> (s.start, s.end());
    }

    const model::Scene* TimelineRuler::sceneAt (juce::Point<float> pos, Zone& zone) const
    {
        zone = Zone::none;
        if (pos.y >= (float) sceneBarHeight || pos.x < (float) mapping.laneX)
            return nullptr;

        for (auto& s : context.model.get().scenes)
        {
            auto r = sceneBounds (s.start, s.end());
            if (! r.contains (pos))
                continue;

            // as SceneBar: the end handle wins over the start handle on narrow scenes
            if (pos.x >= r.getRight() - sceneResizeAreaWidth)
                zone = Zone::resizeEnd;
            else if (pos.x < r.getX() + sceneResizeAreaWidth)
                zone = Zone::resizeStart;
            else
                zone = Zone::move;
            return &s;
        }
        return nullptr;
    }

    TimelineRuler::Snap TimelineRuler::snap (int ticks) const
    {
        // Timeline.getSnap: snap to the start or end of any block within 10 pixels
        ticks = juce::jmax (0, ticks);
        const int margin = (int) (10.0 / mapping.pixelsPerTick);
        Snap result { ticks, false, 0 };
        int best = margin + 1;

        for (auto& track : context.model.get().tracks)
        {
            if (! track.userData.arrangeViewExpanded)
                continue;
            for (auto& b : track.blocks)
                for (int edge : { b.start, b.end })
                    if (std::abs (edge - ticks) <= margin && std::abs (edge - ticks) < best)
                    {
                        best = std::abs (edge - ticks);
                        result = { edge, true, best };
                    }
        }
        return result;
    }

    void TimelineRuler::paint (juce::Graphics& g)
    {
        auto& t = Theme::get();
        auto& p = context.model.get();
        const int rate = juce::jmax (1, p.player.rate);

        g.fillAll (t.timelineBackground);

        auto lanes = getLocalBounds().withTrimmedLeft (mapping.laneX);
        g.saveState();
        g.reduceClipRegion (lanes);

        auto drawScene = [&] (juce::Rectangle<float> r, const juce::String& label, bool current)
        {
            g.setColour (t.sceneFill.withAlpha (current ? 1.0f : 0.6f));
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (t.outline);
            g.drawRoundedRectangle (r, 3.0f, current ? 2.0f : 1.0f);

            // resize handles
            if (r.getWidth() > 3.0f * sceneResizeAreaWidth)
            {
                g.setColour (t.outline.withAlpha (0.5f));
                for (float x : { r.getX() + 4.0f, r.getRight() - 6.0f })
                    g.fillRect (juce::Rectangle<float> (x, r.getY() + 5.0f, 2.0f, r.getHeight() - 10.0f));
            }

            g.setColour (juce::Colours::black);
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawFittedText (label, r.reduced (sceneResizeAreaWidth, 0).toNearestInt(), juce::Justification::centredLeft, 1);
        };

        // scenes, at their dragged or nudged positions while a drag is in progress
        for (auto& s : p.scenes)
        {
            auto range = displayedRange (s);
            const bool current = drag == Drag::newScene ? false
                               : (drag == Drag::moveScene || drag == Drag::resizeSceneStart || drag == Drag::resizeSceneEnd) ? s.path == dragScene
                               : p.player.currentScene == s.name;
            drawScene (sceneBounds (range.getStart(), range.getEnd()),
                       s.name + (s.userData.keybinding.isNotEmpty() ? "  [" + s.userData.keybinding + "]" : juce::String()),
                       current);
        }

        if (drag == Drag::newScene && ! newScene.isEmpty())
            drawScene (sceneBounds (newScene.getStart(), newScene.getEnd()), {}, true);

        // time ruler
        const double pixelsPerSecond = mapping.pixelsPerTick * rate;
        const double gridSeconds = gridSecondsFor (pixelsPerSecond);
        const double firstSecond = std::floor (mapping.scrollTicks / rate / gridSeconds) * gridSeconds;

        g.setFont (juce::FontOptions (11.0f));
        for (double s = firstSecond;; s += gridSeconds)
        {
            const float x = mapping.tickToX (s * rate);
            if (x > (float) getWidth())
                break;
            g.setColour (t.textDim);
            g.drawVerticalLine ((int) x, (float) sceneBarHeight + 8, (float) getHeight());
            g.drawText (formatTime (s), (int) x + 3, sceneBarHeight, 60, getHeight() - sceneBarHeight, juce::Justification::centredLeft);
        }

        // loop range
        if (p.player.start >= 0 && p.player.end > p.player.start)
        {
            g.setColour (t.accent.withAlpha (0.25f));
            g.fillRect (juce::Rectangle<float> (mapping.tickToX (p.player.start), (float) getHeight() - 5.0f,
                                                mapping.tickToX (p.player.end) - mapping.tickToX (p.player.start), 5.0f));
        }

        // snap lines
        g.setColour (t.accent);
        for (int tick : mapping.snapLines)
            g.drawVerticalLine ((int) mapping.tickToX (tick), 0.0f, (float) getHeight());

        // playhead
        const float px = mapping.tickToX (playPosition);
        g.setColour (t.playhead);
        g.drawLine (px, 0.0f, px, (float) getHeight(), 2.0f);
        juce::Path head;
        head.addTriangle (px - 6, (float) getHeight() - 10, px + 6, (float) getHeight() - 10, px, (float) getHeight());
        g.fillPath (head);
        g.restoreState();

        // header column
        g.setColour (t.header);
        g.fillRect (getLocalBounds().withWidth (mapping.laneX));
        g.setColour (t.text);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("Scenes", 8, 0, mapping.laneX - 16, sceneBarHeight, juce::Justification::centredLeft);
        g.drawText (formatTime (playPosition / (double) rate) + "." + juce::String ((playPosition % rate) * 10 / rate),
                    8, sceneBarHeight, mapping.laneX - 16, getHeight() - sceneBarHeight, juce::Justification::centredLeft);
        g.setColour (t.outline);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
    }

    void TimelineRuler::mouseMove (const juce::MouseEvent& e)
    {
        Zone zone;
        sceneAt (e.position, zone);
        setMouseCursor (zone == Zone::resizeStart || zone == Zone::resizeEnd ? juce::MouseCursor::LeftRightResizeCursor
                      : zone == Zone::move ? juce::MouseCursor::DraggingHandCursor
                      : juce::MouseCursor::NormalCursor);
    }

    void TimelineRuler::mouseDown (const juce::MouseEvent& e)
    {
        if (e.x < mapping.laneX)
            return;

        drag = Drag::none;
        repositioned.clear();
        newScene = {};
        lastMouseX = e.position.x;

        if (e.y < sceneBarHeight)
        {
            Zone zone;
            if (auto* s = sceneAt (e.position, zone))
            {
                const auto path = s->path;

                if (e.mods.isPopupMenu())
                {
                    juce::PopupMenu menu;
                    juce::PopupMenu modes;
                    for (auto mode : { "hold", "play", "loop" })
                        modes.addItem (mode, true, s->mode == mode, [this, path, mode]
                        {
                            if (auto* scene = context.model.get().findScene (path))
                                context.controller.setScene (path, scene->start, scene->length, mode);
                        });
                    menu.addSubMenu ("Mode", modes);
                    menu.addItem ("Rename...", [this, path]
                    {
                        askForName ("Rename scene", lastElement (path), [this, path] (juce::String name)
                        {
                            auto r2 = context.controller.rename (path, name);
                            if (r2.failed())
                                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Rename", r2.getErrorMessage());
                        });
                    });
                    menu.addItem ("Key binding...", [this, path]
                    {
                        auto* scene = context.model.get().findScene (path);
                        askForName ("Scene key (0-9 or A-Z, empty for none)", scene ? scene->userData.keybinding : juce::String(),
                                    [this, path] (juce::String key) { context.controller.setSceneKeybinding (path, key); });
                    });
                    menu.addSeparator();
                    menu.addItem ("Delete scene", [this, path] { context.controller.remove (path); });
                    menu.showMenuAsync (juce::PopupMenu::Options());
                    return;
                }

                // Timeline.startSceneRepositionDrag
                auto r = sceneBounds (s->start, s->end());
                dragScene = path;
                if (zone == Zone::resizeEnd)
                {
                    drag = Drag::resizeSceneEnd;
                    dragPixelOffset = r.getRight() - e.position.x;
                }
                else
                {
                    drag = zone == Zone::resizeStart ? Drag::resizeSceneStart : Drag::moveScene;
                    dragPixelOffset = e.position.x - r.getX();
                }

                context.controller.activateScene (path);
                if (onSceneSelected)
                    onSceneSelected (path);
                startTimer (50);
                return;
            }

            if (e.mods.isPopupMenu())
                return;

            // drag in empty scene bar to create a scene; the selected scene is deselected
            drag = Drag::newScene;
            dragClickX = juce::jmax ((float) mapping.laneX, e.position.x);
            if (context.model.get().player.currentScene.isNotEmpty())
                context.controller.activateScene ({});
            startTimer (50);
            return;
        }

        // ruler: drag the playhead (deselecting the scene, as Timeline.startPlayPositionDrag)
        if (context.model.get().player.currentScene.isNotEmpty())
            context.controller.activateScene ({});
        drag = Drag::playhead;
        startTimer (50);
        updateDrag (e.position.x);
    }

    void TimelineRuler::updateDrag (float mouseX)
    {
        lastMouseX = mouseX;
        const float x = juce::jlimit ((float) mapping.laneX, (float) getWidth(), mouseX);
        auto toTicks = [this] (float px) { return juce::jmax (0, juce::roundToInt (mapping.xToTick (px))); };
        std::vector<int> snapLines;

        switch (drag)
        {
            case Drag::playhead:
            {
                const int tick = toTicks (x);
                context.controller.setPlayPosition (tick);
                playPosition = tick;
                break;
            }

            case Drag::newScene:
            {
                // Timeline.setSceneCreationToMousePosition
                auto clickSnap = snap (toTicks (dragClickX));
                auto dragSnap = snap (toTicks (x));
                const int start = juce::jmin (clickSnap.value, dragSnap.value);
                const int end = juce::jmax (clickSnap.value, dragSnap.value);
                newScene = { start, end };

                repositioned.clear();
                if (end > start)
                    reschedule (start, end, dragSnap.value > clickSnap.value);

                context.controller.setPlayPosition (start);
                playPosition = start;
                if (clickSnap.snapped) snapLines.push_back (clickSnap.value);
                if (dragSnap.snapped) snapLines.push_back (dragSnap.value);
                break;
            }

            case Drag::moveScene:
            case Drag::resizeSceneStart:
            case Drag::resizeSceneEnd:
            {
                // Timeline.repositionSceneToMousePosition
                auto* scene = context.model.get().findScene (dragScene);
                if (scene == nullptr)
                {
                    drag = Drag::none;
                    break;
                }

                int start = scene->start, length = scene->length;
                bool movingRight = false;

                if (drag == Drag::moveScene)
                {
                    start = toTicks (x - dragPixelOffset);
                    auto startSnap = snap (start);
                    auto endSnap = snap (start + length);
                    if (startSnap.snapped && (! endSnap.snapped || startSnap.distance <= endSnap.distance))
                    {
                        start = startSnap.value;
                        snapLines.push_back (startSnap.value);
                        if (endSnap.snapped && endSnap.distance == startSnap.distance)
                            snapLines.push_back (endSnap.value);
                    }
                    else if (endSnap.snapped)
                    {
                        start = endSnap.value - length;
                        snapLines.push_back (endSnap.value);
                    }
                    start = juce::jmax (0, start);
                    movingRight = start > scene->start;
                }
                else if (drag == Drag::resizeSceneStart)
                {
                    const int previousEnd = scene->end();
                    auto s = snap (juce::jmin (previousEnd - 1, juce::roundToInt (mapping.xToTick (x - dragPixelOffset))));
                    start = s.value;
                    length = previousEnd - start;
                    movingRight = start > scene->start;
                    if (s.snapped) snapLines.push_back (s.value);
                }
                else
                {
                    auto s = snap (juce::jmax (scene->start + 1, juce::roundToInt (mapping.xToTick (x + dragPixelOffset))));
                    length = juce::jmax (1, s.value - start);
                    movingRight = start + length > scene->end();
                    if (s.snapped) snapLines.push_back (s.value);
                }

                repositioned.clear();
                repositioned[dragScene] = { start, start + length };
                reschedule (start, start + length, movingRight);
                break;
            }

            case Drag::none:
                break;
        }

        if (snapLines != mapping.snapLines)
        {
            mapping.snapLines = std::move (snapLines);
            if (onSnapLinesChanged)
                onSnapLinesChanged();
        }
        repaint();
    }

    void TimelineRuler::reschedule (int zoneStart, int zoneEnd, bool movingRight)
    {
        // Timeline.doSceneRescheduling: nudge the other scenes out of the way, keeping their order
        struct Span { juce::String path; int start, length; int end() const { return start + length; } };
        std::vector<Span> others;
        for (auto& s : context.model.get().scenes)
            if (repositioned.find (s.path) == repositioned.end())
                others.push_back ({ s.path, s.start, s.length });

        std::stable_sort (others.begin(), others.end(), [] (const Span& a, const Span& b)
        {
            return a.start * 2 + a.length < b.start * 2 + b.length;   // by centre
        });

        int insertion = (int) others.size();
        for (int i = 0; i < (int) others.size(); ++i)
        {
            if (movingRight ? others[(size_t) i].end() > zoneStart : others[(size_t) i].start >= zoneEnd)
            {
                insertion = i;
                break;
            }
        }

        // bump scenes to the right until those on the left fit before the zone
        int durationToLeft = 0;
        for (int i = 0; i < insertion; ++i)
            durationToLeft += others[(size_t) i].length;
        while (durationToLeft > zoneStart && insertion > 0)
            durationToLeft -= others[(size_t) --insertion].length;

        int maximumEnd = zoneStart;
        for (int i = insertion - 1; i >= 0; --i)
        {
            auto& s = others[(size_t) i];
            if (s.end() <= maximumEnd)
                break;
            const int start = maximumEnd - s.length;
            repositioned[s.path] = { start, start + s.length };
            maximumEnd = start;
        }

        int minimumStart = zoneEnd;
        for (int i = insertion; i < (int) others.size(); ++i)
        {
            auto& s = others[(size_t) i];
            if (s.start >= minimumStart)
                break;
            repositioned[s.path] = { minimumStart, minimumStart + s.length };
            minimumStart += s.length;
        }
    }

    void TimelineRuler::mouseDrag (const juce::MouseEvent& e)
    {
        if (drag != Drag::none)
            updateDrag (e.position.x);
    }

    void TimelineRuler::timerCallback()
    {
        // Timeline.onScrollTimer: scroll while dragging near the edges
        if (drag == Drag::none || ! isMouseButtonDown())
        {
            stopTimer();
            return;
        }

        constexpr float scrollMargin = 5.0f, scrollPixels = 25.0f;
        double amount = 0.0;
        if (lastMouseX < (float) mapping.laneX + scrollMargin)
            amount = -scrollPixels / mapping.pixelsPerTick;
        else if (lastMouseX > (float) getWidth() - scrollMargin)
            amount = scrollPixels / mapping.pixelsPerTick;

        if (amount != 0.0 && onAutoScroll != nullptr)
        {
            const double before = mapping.scrollTicks;
            onAutoScroll (amount);
            if (mapping.scrollTicks != before)
                updateDrag (lastMouseX);
        }
    }

    void TimelineRuler::finishDrag()
    {
        stopTimer();
        const auto finished = drag;
        drag = Drag::none;

        if (finished == Drag::newScene && ! newScene.isEmpty())
        {
            auto scene = context.controller.addScene (newScene.getStart(), newScene.getLength(), repositioned);
            if (scene.isNotEmpty())
            {
                context.controller.activateScene (scene);
                if (onSceneSelected)
                    onSceneSelected (scene);
            }
        }
        else if (finished == Drag::moveScene || finished == Drag::resizeSceneStart || finished == Drag::resizeSceneEnd)
        {
            context.controller.repositionScenes (repositioned);
        }

        repositioned.clear();
        newScene = {};
        if (! mapping.snapLines.empty())
        {
            mapping.snapLines.clear();
            if (onSnapLinesChanged)
                onSnapLinesChanged();
        }
        repaint();
    }

    void TimelineRuler::mouseUp (const juce::MouseEvent&)
    {
        finishDrag();
    }

    void TimelineRuler::mouseDoubleClick (const juce::MouseEvent& e)
    {
        if (e.y >= sceneBarHeight || e.x < mapping.laneX)
            return;
        // double-click in empty scene bar: a 10-second scene
        Zone zone;
        if (sceneAt (e.position, zone) != nullptr)
            return;
        const int rate = juce::jmax (1, context.model.get().player.rate);
        context.controller.addScene (snapTicks ((int) mapping.xToTick ((float) e.x), rate), rate * 10);
    }

    //==============================================================================
    TrackLanes::TrackLanes (AppContext& c, TimelineMapping& m) : context (c), mapping (m)
    {
        setWantsKeyboardFocus (true);
    }

    std::vector<TrackLanes::TrackRow> TrackLanes::rows() const
    {
        std::vector<TrackRow> result;
        int y = 0;
        for (auto& t : context.model.get().tracks)
        {
            const int h = juce::jlimit (40, 600, t.userData.arrangeViewHeight);
            result.push_back ({ &t, { 0, y, getWidth(), h } });
            y += h;
        }
        return result;
    }

    int TrackLanes::getTotalHeight() const
    {
        int h = 0;
        for (auto& t : context.model.get().tracks)
            h += juce::jlimit (40, 600, t.userData.arrangeViewHeight);
        return h + addTrackHeight;
    }

    const model::Track* TrackLanes::trackAt (int y) const
    {
        for (auto& r : rows())
            if (y >= r.bounds.getY() && y < r.bounds.getBottom())
                return r.track;
        return nullptr;
    }

    juce::Rectangle<float> TrackLanes::blockBounds (const model::Block& b, juce::Rectangle<int> row) const
    {
        const float x1 = mapping.tickToX (b.start), x2 = mapping.tickToX (b.end);
        return { x1, (float) row.getY() + 4.0f, juce::jmax (6.0f, x2 - x1), (float) row.getHeight() - 8.0f };
    }

    const model::Block* TrackLanes::blockAt (juce::Point<int> p, const model::Track** trackOut) const
    {
        for (auto& r : rows())
        {
            if (! r.bounds.contains (p))
                continue;
            if (trackOut != nullptr)
                *trackOut = r.track;
            for (auto& b : r.track->blocks)
                if (blockBounds (b, r.bounds).contains (p.toFloat()) && p.x >= mapping.laneX)
                    return &b;
        }
        return nullptr;
    }

    const model::Envelope* TrackLanes::currentEnvelope (const model::Block*& block) const
    {
        block = context.model.get().findBlock (context.model.selectedBlock);
        if (block == nullptr || editedEnvelope.isEmpty())
            return nullptr;
        for (auto& e : block->envelopes)
            if (e.path == editedEnvelope)
                return &e;
        return nullptr;
    }

    juce::Point<float> TrackLanes::pointPosition (const model::Envelope& env, const model::ControlPoint& cp, const model::Block& block,
                                                  juce::Rectangle<float> r, const EndpointInfo& target) const
    {
        juce::ignoreUnused (env);
        const float x = mapping.tickToX (block.start + cp.tick);
        const float unit = (float) target.toUnit (cp.value);
        return { x, r.getBottom() - 3.0f - unit * (r.getHeight() - 20.0f) };
    }

    bool TrackLanes::isCurveMode() const
    {
        auto* block = context.model.get().findBlock (context.model.selectedBlock);
        return block != nullptr && block->userData.curvatureMode;
    }

    void TrackLanes::toggleCurveMode()
    {
        if (context.model.get().findBlock (context.model.selectedBlock) == nullptr)
            return;
        context.controller.updateBlockUserData (context.model.selectedBlock, [] (BlockUserData& d) { d.curvatureMode = ! d.curvatureMode; });
        repaint();
    }

    juce::Rectangle<float> TrackLanes::getCurveButtonBounds() const
    {
        const model::Block* block = nullptr;
        if (currentEnvelope (block) == nullptr || block == nullptr)
            return {};
        for (auto& r : rows())
            if (r.track->path == parentPath (block->path))
                return blockBounds (*block, r.bounds).removeFromBottom (20.0f).removeFromRight (20.0f).reduced (2.0f);
        return {};
    }

    const model::ControlPoint* TrackLanes::segmentStartAt (const model::Envelope& env, int tick) const
    {
        // EnvelopeView.getPreviousControlPoint: the last point at or before the tick
        // that has a point after it
        const model::ControlPoint* result = nullptr;
        for (size_t i = 0; i + 1 < env.points.size(); ++i)
            if (env.points[i].tick <= tick)
                result = &env.points[i];
        return result;
    }

    float TrackLanes::curvatureFromMouse (juce::Point<float> mouse) const
    {
        // EnvelopeView.getDragCurvatureFromMouse
        const model::Block* block = nullptr;
        auto* env = currentEnvelope (block);
        if (env == nullptr)
            return 0.0f;

        for (auto& r : rows())
        {
            if (r.track->path != parentPath (block->path))
                continue;
            auto bb = blockBounds (*block, r.bounds);
            auto target = EndpointInfo::find (context.session, env->targetPath);
            for (size_t i = 0; i + 1 < env->points.size(); ++i)
            {
                if (env->points[i].path != dragCurvePoint)
                    continue;
                const auto prev = pointPosition (*env, env->points[i], *block, bb, target);
                const auto next = pointPosition (*env, env->points[i + 1], *block, bb, target);
                const float xRange = next.x - prev.x, yRange = next.y - prev.y;
                if (xRange == 0.0f || yRange == 0.0f)
                    return 0.0f;
                const double xProportion = (mouse.x - prev.x) / xRange, yProportion = (mouse.y - prev.y) / yRange;
                constexpr float minimum = -2.0f, maximum = 2.0f;   // ControlPoint.curvature range
                if (xProportion <= 0.0 || yProportion >= 1.0)
                    return maximum;
                if (yProportion <= 0.0 || xProportion >= 1.0)
                    return minimum;
                const double baseXLogOfY = std::log (yProportion) / std::log (xProportion);
                return juce::jlimit (minimum, maximum, (float) (-std::log (baseXLogOfY) / std::log (2.0)));
            }
        }
        return 0.0f;
    }

    std::optional<juce::Point<float>> TrackLanes::getControlPointPosition (const juce::String& controlPointPath) const
    {
        for (auto& r : rows())
            for (auto& b : r.track->blocks)
                for (auto& env : b.envelopes)
                    for (auto& cp : env.points)
                        if (cp.path == controlPointPath)
                            return pointPosition (env, cp, b, blockBounds (b, r.bounds), EndpointInfo::find (context.session, env.targetPath));
        return std::nullopt;
    }

    void TrackLanes::paintEnvelopes (juce::Graphics& g, const model::Block& block, juce::Rectangle<float> r)
    {
        auto& t = Theme::get();

        for (auto& env : block.envelopes)
        {
            if (env.points.empty() || env.targetPath.isEmpty())
                continue;

            auto target = EndpointInfo::find (context.session, env.targetPath);
            const bool edited = env.path == editedEnvelope && block.path == context.model.selectedBlock;

            // the point being dragged is drawn at its preview position until it's dropped
            auto points = env.points;
            if (drag == Drag::controlPoint)
                for (auto& cp : points)
                    if (cp.path == dragPoint)
                        cp.tick = dragPointTick, cp.value = dragPointValue;

            if (drag == Drag::curve)
                for (auto& cp : points)
                    if (cp.path == dragCurvePoint)
                        cp.curvature = dragCurvature;

            juce::Path path;
            for (size_t i = 0; i < points.size(); ++i)
            {
                auto pt = pointPosition (env, points[i], block, r, target);
                if (i == 0)
                {
                    path.startNewSubPath (r.getX(), pt.y);
                    path.lineTo (pt);
                    continue;
                }

                // EnvelopeView.drawCurvedTransition: y follows t^(2^-curvature) of the previous point
                const auto prev = pointPosition (env, points[i - 1], block, r, target);
                const float curvature = points[i - 1].curvature;
                if (curvature != 0.0f && pt.x > prev.x + 1.0f && pt.y != prev.y)
                {
                    const double exponent = std::pow (2.0, -curvature);
                    for (float x = prev.x + 1.0f; x < pt.x; x += 1.0f)
                    {
                        const double t = std::pow ((x - prev.x) / (pt.x - prev.x), exponent);
                        path.lineTo (x, prev.y + (float) t * (pt.y - prev.y));
                    }
                }
                path.lineTo (pt);
            }
            auto last = pointPosition (env, points.back(), block, r, target);
            path.lineTo (r.getRight(), last.y);

            g.setColour (t.envelope.withAlpha (edited ? 1.0f : 0.45f));
            g.strokePath (path, juce::PathStrokeType (edited ? 2.0f : 1.2f));

            if (edited)
            {
                for (auto& cp : points)
                {
                    auto pt = pointPosition (env, cp, block, r, target);
                    g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (pt));
                }
                g.setFont (juce::FontOptions (11.0f));
                g.drawText (lastElement (parentPath (env.targetPath)) + "." + lastElement (env.targetPath),
                            r.reduced (6, 2).withTrimmedRight (22).toNearestInt(), juce::Justification::bottomRight);

                // curve mode button (BlockView._curvatureModeButton)
                auto button = getCurveButtonBounds();
                g.setColour (isCurveMode() ? t.accent : t.panel.withAlpha (0.8f));
                g.fillRoundedRectangle (button, 3.0f);
                g.setColour (isCurveMode() ? t.accentText : t.text);
                juce::Path curve;
                auto c = button.reduced (4.0f);
                curve.startNewSubPath (c.getBottomLeft());
                curve.quadraticTo (c.getBottomRight(), c.getTopRight());
                g.strokePath (curve, juce::PathStrokeType (1.5f));
            }
        }
    }

    void TrackLanes::paint (juce::Graphics& g)
    {
        auto& t = Theme::get();
        auto& model = context.model;
        g.fillAll (t.background);

        const int rate = juce::jmax (1, model.get().player.rate);
        const double gridSeconds = gridSecondsFor (mapping.pixelsPerTick * rate);

        for (auto& row : rows())
        {
            auto& track = *row.track;
            const bool trackSelected = model.selectedTrack == track.path;
            auto colour = track.userData.getColour();

            // lane background and grid
            auto lane = row.bounds.withTrimmedLeft (mapping.laneX);
            g.setColour (trackSelected ? t.panel : t.panel.interpolatedWith (t.background, 0.5f));
            g.fillRect (lane);
            g.setColour (t.gridLine);
            for (double s = std::floor (mapping.scrollTicks / rate / gridSeconds) * gridSeconds;; s += gridSeconds)
            {
                const float x = mapping.tickToX (s * rate);
                if (x > (float) getWidth())
                    break;
                if (x >= (float) mapping.laneX)
                    g.drawVerticalLine ((int) x, (float) row.bounds.getY(), (float) row.bounds.getBottom());
            }

            // blocks
            g.saveState();
            g.reduceClipRegion (lane);
            const bool draggingBlocks = drag == Drag::moveBlock || drag == Drag::resizeStart || drag == Drag::resizeEnd;
            std::vector<const model::Block*> shown;
            for (auto& b : track.blocks)
                if (! (draggingBlocks && b.path == dragBlock && dragTargetTrack != track.path))
                    shown.push_back (&b);
            if (draggingBlocks && dragTargetTrack == track.path && parentPath (dragBlock) != track.path)
                if (auto* dragged = model.get().findBlock (dragBlock))
                    shown.push_back (dragged);   // dragged here from another track

            for (auto* bp : shown)
            {
                auto& b = *bp;
                const auto range = displayedRange (b);
                const float x1 = mapping.tickToX (range.getStart()), x2 = mapping.tickToX (range.getEnd());
                auto r = juce::Rectangle<float> (x1, (float) row.bounds.getY() + 4.0f, juce::jmax (6.0f, x2 - x1), (float) row.bounds.getHeight() - 8.0f);
                const bool selected = model.selectedBlock == b.path;
                const bool active = playPosition >= b.start && playPosition < b.end;

                g.setColour (colour.withAlpha (active ? 0.55f : 0.32f).overlaidWith (t.blockFill.withAlpha (0.4f)));
                g.fillRoundedRectangle (r, 4.0f);
                g.setColour (selected ? t.accent : colour.darker (0.3f));
                g.drawRoundedRectangle (r, 4.0f, selected ? 2.5f : 1.2f);

                g.setColour (t.text);
                g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
                g.drawFittedText (b.name, r.reduced (6, 3).toNearestInt(), juce::Justification::topLeft, 1);

                if (draggingBlocks && range != juce::Range<int> (b.start, b.end))
                    continue;   // envelopes are drawn again once the block is dropped
                paintEnvelopes (g, b, r);
            }
            g.restoreState();

            // header
            auto header = row.bounds.withWidth (mapping.laneX);
            g.setColour (trackSelected ? t.header.brighter (0.1f) : t.header);
            g.fillRect (header);
            g.setColour (colour);
            g.fillRect (header.removeFromLeft (8));
            g.setColour (t.text);
            g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
            g.drawFittedText (track.name, header.reduced (8, 6).removeFromTop (20), juce::Justification::centredLeft, 1);
            g.setColour (t.textDim);
            g.setFont (juce::FontOptions (11.0f));
            g.drawFittedText (juce::String ((int) track.blocks.size()) + (track.blocks.size() == 1 ? " block" : " blocks"),
                              header.reduced (8, 6).withTrimmedTop (20), juce::Justification::topLeft, 1);

            g.setColour (t.outline);
            g.drawHorizontalLine (row.bounds.getBottom() - 1, 0.0f, (float) getWidth());
        }

        // snap lines while dragging scenes or blocks
        g.setColour (t.accent);
        for (int tick : mapping.snapLines)
        {
            const float x = mapping.tickToX (tick);
            if (x >= (float) mapping.laneX)
                g.drawVerticalLine ((int) x, 0.0f, (float) getHeight());
        }

        // playhead
        const float px = mapping.tickToX (playPosition);
        if (px >= (float) mapping.laneX)
        {
            g.setColour (t.playhead);
            g.drawLine (px, 0.0f, px, (float) getHeight(), 1.5f);
        }

        // "add track" row
        auto addRow = juce::Rectangle<int> (0, getTotalHeight() - addTrackHeight, mapping.laneX, addTrackHeight).reduced (8, 5);
        g.setColour (t.panelAlt);
        g.fillRoundedRectangle (addRow.toFloat(), 4.0f);
        g.setColour (t.text);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText ("+ Add track", addRow, juce::Justification::centred);

        g.setColour (t.outline);
        g.drawVerticalLine (mapping.laneX - 1, 0.0f, (float) getHeight());

        if (model.get().tracks.empty())
        {
            g.setColour (t.textDim);
            g.drawText ("No tracks - click \"Add track\"", getLocalBounds().withTrimmedLeft (mapping.laneX), juce::Justification::centred);
        }
        else if (model.selectedBlock.isNotEmpty() && editedEnvelope.isEmpty())
        {
            g.setColour (t.textDim);
            g.setFont (juce::FontOptions (11.0f));
        }

        if (! dropOutline.isEmpty())
        {
            g.setColour (juce::Colour (0xff808080));
            g.drawRect (dropOutline, 1.0f);
        }
    }

    //==============================================================================
    bool TrackLanes::isInterestedInDragSource (const SourceDetails& d)
    {
        return d.description.toString().startsWith (BlockLibrary::dragPrefix);
    }

    void TrackLanes::itemDropped (const SourceDetails& d)
    {
        updateDropOutline (std::nullopt);
        juce::File file (d.description.toString().fromFirstOccurrenceOf (BlockLibrary::dragPrefix, false, false));
        auto result = importBlockAt (file, d.localPosition);
        if (result.failed())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Import block", result.getErrorMessage());
    }

    bool TrackLanes::isInterestedInFileDrag (const juce::StringArray& files)
    {
        for (auto& f : files)
            if (juce::File (f).hasFileExtension ("integra"))
                return true;
        return false;
    }

    void TrackLanes::filesDropped (const juce::StringArray& files, int x, int y)
    {
        updateDropOutline (std::nullopt);
        for (auto& f : files)
        {
            if (! juce::File (f).hasFileExtension ("integra"))
                continue;
            auto result = importBlockAt (juce::File (f), { x, y });
            if (result.failed())
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Import block", result.getErrorMessage());
            break;
        }
    }

    void TrackLanes::updateDropOutline (std::optional<juce::Point<int>> position)
    {
        // ArrangeViewTrack.onDragEnter: a new block's length, centred on the mouse
        juce::Rectangle<float> outline;
        if (position.has_value())
        {
            for (auto& r : rows())
            {
                if (position->y >= r.bounds.getY() && position->y < r.bounds.getBottom() && position->x >= mapping.laneX)
                {
                    const float width = (float) (ProjectController::newBlockSeconds * context.model.get().player.rate * mapping.pixelsPerTick);
                    outline = { (float) position->x - width / 2.0f, (float) r.bounds.getY(), width, (float) r.bounds.getHeight() };
                }
            }
        }

        if (outline != dropOutline)
        {
            dropOutline = outline;
            repaint();
        }
    }

    juce::Result TrackLanes::importBlockAt (const juce::File& file, juce::Point<int> position)
    {
        // ArrangeViewTrack.onDragDrop
        auto* track = trackAt (position.y);
        if (track == nullptr || position.x < mapping.laneX)
            return juce::Result::fail ("Drop the block onto a track");

        const int halfLength = ProjectController::newBlockSeconds * context.model.get().player.rate / 2;
        const int start = juce::jmax (0, (int) std::round (mapping.xToTick ((float) position.x)) - halfLength);
        auto result = context.controller.importBlock (file, track->path, start);
        if (result.wasOk() && onSelectionChanged)
            onSelectionChanged();
        return result;
    }

    void TrackLanes::mouseMove (const juce::MouseEvent& e)
    {
        const model::Track* track = nullptr;
        auto* block = blockAt (e.getPosition(), &track);
        if (block != nullptr && track != nullptr)
        {
            for (auto& r : rows())
                if (r.track == track)
                {
                    auto b = blockBounds (*block, r.bounds);
                    if (e.position.x > b.getRight() - blockResizeAreaWidth || e.position.x < b.getX() + blockResizeAreaWidth)
                    {
                        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
                        return;
                    }
                }
        }
        setMouseCursor (juce::MouseCursor::NormalCursor);
    }

    void TrackLanes::mouseDown (const juce::MouseEvent& e)
    {
        grabKeyboardFocus();
        drag = Drag::none;
        auto& model = context.model;

        // add track
        if (e.y >= getTotalHeight() - addTrackHeight && e.x < mapping.laneX)
        {
            context.controller.addTrack();
            if (onSelectionChanged) onSelectionChanged();
            return;
        }

        // envelope editing inside the selected block
        if (getCurveButtonBounds().contains (e.position))
        {
            toggleCurveMode();
            return;
        }

        const model::Block* editedBlock = nullptr;
        if (auto* env = currentEnvelope (editedBlock); env != nullptr && isCurveMode() && ! e.mods.isPopupMenu())
        {
            for (auto& r : rows())
            {
                if (r.track->path != parentPath (editedBlock->path) || ! blockBounds (*editedBlock, r.bounds).contains (e.position))
                    continue;
                const int tick = juce::roundToInt (mapping.xToTick (e.position.x)) - editedBlock->start;
                if (auto* cp = segmentStartAt (*env, tick))
                {
                    drag = Drag::curve;
                    dragCurvePoint = cp->path;
                    dragCurvature = curvatureFromMouse (e.position);
                    repaint();
                }
                return;
            }
        }

        if (auto* env = currentEnvelope (editedBlock))
        {
            for (auto& r : rows())
            {
                auto bb = editedBlock != nullptr ? blockBounds (*editedBlock, r.bounds) : juce::Rectangle<float>();
                if (r.track->path != parentPath (editedBlock->path) || ! bb.contains (e.position))
                    continue;

                auto target = EndpointInfo::find (context.session, env->targetPath);
                const auto& points = env->points;   // sorted by tick
                for (size_t i = 0; i < points.size(); ++i)
                {
                    auto& cp = points[i];
                    if (pointPosition (*env, cp, *editedBlock, bb, target).getDistanceFrom (e.position) < 7.0f)
                    {
                        if (e.mods.isPopupMenu())
                        {
                            if (i > 0)   // the first control point can't be deleted (EnvelopeView)
                                context.controller.remove (cp.path);
                            return;
                        }
                        // EnvelopeView.handleDragPointMouseDown: a point stays between its
                        // neighbours, and the first one stays at tick 0
                        drag = Drag::controlPoint;
                        dragPoint = cp.path;
                        dragBlock = editedBlock->path;
                        dragPointLimits = i == 0 ? juce::Range<int> (0, 0)
                                                 : juce::Range<int> (points[i - 1].tick + 1,
                                                                     i + 1 < points.size() ? points[i + 1].tick - 1 : editedBlock->length());
                        dragPointTick = cp.tick;
                        dragPointValue = cp.value;
                        return;
                    }
                }
            }
        }

        const model::Track* track = nullptr;
        auto* block = blockAt (e.getPosition(), &track);
        if (track == nullptr)
            track = trackAt (e.y);

        if (track != nullptr)
            model.selectedTrack = track->path;

        if (e.x < mapping.laneX && track != nullptr)
        {
            model.selectedBlock = {};
            if (e.mods.isPopupMenu())
                showTrackMenu (*track);
            if (onSelectionChanged) onSelectionChanged();
            repaint();
            return;
        }

        if (block != nullptr)
        {
            if (model.selectedBlock != block->path)
                editedEnvelope = {};
            model.selectedBlock = block->path;
            if (onSelectionChanged) onSelectionChanged();

            if (e.mods.isPopupMenu())
            {
                showBlockMenu (*block);
                repaint();
                return;
            }

            for (auto& r : rows())
            {
                if (r.track != track)
                    continue;
                auto bb = blockBounds (*block, r.bounds);
                dragBlock = block->path;
                dragStartTick = juce::roundToInt (mapping.xToTick (e.position.x));
                dragTargetTrack = track->path;
                draggedBlocks.clear();
                if (e.position.x > bb.getRight() - blockResizeAreaWidth)
                {
                    drag = Drag::resizeEnd;
                    dragPixelOffset = e.position.x - bb.getRight();
                }
                else if (e.position.x < bb.getX() + blockResizeAreaWidth)
                {
                    drag = Drag::resizeStart;
                    dragPixelOffset = e.position.x - bb.getX();
                }
                else
                {
                    drag = Drag::moveBlock;
                }
            }
        }
        else
        {
            model.selectedBlock = {};
            editedEnvelope = {};
            if (onSelectionChanged) onSelectionChanged();
        }

        repaint();
    }

    void TrackLanes::mouseDrag (const juce::MouseEvent& e)
    {
        if (drag == Drag::curve)
        {
            dragCurvature = curvatureFromMouse (e.position);
            repaint();
            return;
        }

        if (drag == Drag::controlPoint)
        {
            auto* block = context.model.get().findBlock (dragBlock);
            const model::Block* b2 = nullptr;
            auto* env = currentEnvelope (b2);
            if (block == nullptr || env == nullptr)
                return;

            for (auto& r : rows())
            {
                if (r.track->path != parentPath (block->path))
                    continue;
                auto bb = blockBounds (*block, r.bounds);
                auto target = EndpointInfo::find (context.session, env->targetPath);
                const int tick = (int) std::round (mapping.xToTick (e.position.x)) - block->start;
                dragPointTick = juce::jlimit (dragPointLimits.getStart(), juce::jmax (dragPointLimits.getStart(), dragPointLimits.getEnd()), tick);
                const double unit = juce::jlimit (0.0, 1.0, (double) ((bb.getBottom() - 3.0f - e.position.y) / (bb.getHeight() - 20.0f)));
                dragPointValue = (float) target.fromUnit (unit);
            }
            repaint();
            return;
        }

        if (drag != Drag::none)
            updateBlockDrag (e);
    }

    juce::Range<int> TrackLanes::displayedRange (const model::Block& b) const
    {
        auto it = draggedBlocks.find (b.path);
        return it != draggedBlocks.end() ? it->second : juce::Range<int> (b.start, b.end);
    }

    TrackLanes::Snap TrackLanes::snapBlock (int ticks) const
    {
        // ArrangeView.getSnap: blocks on other tracks that aren't being dragged, and scenes, within 10 pixels
        ticks = juce::jmax (0, ticks);
        const int margin = (int) (10.0 / mapping.pixelsPerTick);
        Snap result { ticks, false, 0 };
        int best = margin + 1;
        auto consider = [&] (int edge)
        {
            const int d = std::abs (edge - ticks);
            if (d <= margin && d < best)
            {
                best = d;
                result = { edge, true, d };
            }
        };

        auto& p = context.model.get();
        const auto ownTrack = parentPath (dragBlock);
        for (auto& track : p.tracks)
        {
            if (track.path == ownTrack || ! track.userData.arrangeViewExpanded)
                continue;
            for (auto& b : track.blocks)
                if (b.path != dragBlock && draggedBlocks.find (b.path) == draggedBlocks.end())
                    consider (b.start), consider (b.end);
        }
        for (auto& s : p.scenes)
            consider (s.start), consider (s.end());
        return result;
    }

    void TrackLanes::updateBlockDrag (const juce::MouseEvent& e)
    {
        // ArrangeView.onDragBlock
        auto* block = context.model.get().findBlock (dragBlock);
        if (block == nullptr)
        {
            drag = Drag::none;
            return;
        }

        draggedBlocks.clear();
        std::vector<int> snapLines;
        int start = block->start, end = block->end;
        bool movingRight = false;

        if (drag == Drag::moveBlock)
        {
            // doBlockMove
            const float x = juce::jlimit ((float) mapping.laneX, (float) getWidth(), e.position.x);
            const int displacement = juce::jmax (-block->start, juce::roundToInt (mapping.xToTick (x)) - dragStartTick);
            auto startSnap = snapBlock (block->start + displacement);
            auto endSnap = snapBlock (block->end + displacement);
            if (startSnap.snapped && (! endSnap.snapped || startSnap.distance <= endSnap.distance))
            {
                start = startSnap.value;
                snapLines.push_back (startSnap.value);
                if (endSnap.snapped && endSnap.distance == startSnap.distance)
                    snapLines.push_back (endSnap.value);
            }
            else if (endSnap.snapped)
            {
                start = juce::jmax (0, endSnap.value - block->length());
                snapLines.push_back (endSnap.value);
            }
            else
            {
                start = startSnap.value;
            }
            start = juce::jmax (0, start);
            end = start + block->length();
            movingRight = start >= block->start;

            if (auto* t = trackAt (e.y))
                dragTargetTrack = t->path;
        }
        else if (drag == Drag::resizeStart)
        {
            auto snap = snapBlock (juce::roundToInt (mapping.xToTick (e.position.x - dragPixelOffset)));
            start = juce::jmax (0, juce::jmin (snap.value, block->end - minimumBlockLength));
            movingRight = start > block->start;
            if (snap.snapped && start == snap.value)
                snapLines.push_back (snap.value);
        }
        else
        {
            auto snap = snapBlock (juce::roundToInt (mapping.xToTick (e.position.x - dragPixelOffset)));
            end = juce::jmax (snap.value, block->start + minimumBlockLength);
            movingRight = end > block->end;
            if (snap.snapped && end == snap.value)
                snapLines.push_back (snap.value);
        }

        draggedBlocks[dragBlock] = { start, end };
        if (auto* target = context.model.get().findTrack (dragTargetTrack))
            rescheduleBlocks (*target, movingRight);

        if (snapLines != mapping.snapLines)
        {
            mapping.snapLines = std::move (snapLines);
            if (onSnapLinesChanged)
                onSnapLinesChanged();
        }
        repaint();
    }

    void TrackLanes::rescheduleBlocks (const model::Track& track, bool movingRight)
    {
        ProjectController::rescheduleBlocks (track, dragBlock, movingRight, draggedBlocks);
    }

    void TrackLanes::mouseUp (const juce::MouseEvent&)
    {
        if (drag == Drag::curve)
        {
            drag = Drag::none;
            context.controller.setControlPointCurvature (dragCurvePoint, dragCurvature);   // onEndDragCurve
        }

        if (drag == Drag::controlPoint)
        {
            // committed once, as RepositionControlPoint in onEndDragPoint
            drag = Drag::none;
            context.controller.moveControlPoint (dragPoint, dragPointTick, dragPointValue);
        }

        if (drag == Drag::moveBlock || drag == Drag::resizeStart || drag == Drag::resizeEnd)
        {
            drag = Drag::none;
            if (! draggedBlocks.empty())
                context.controller.repositionBlocks (dragBlock, dragTargetTrack, draggedBlocks);
        }

        drag = Drag::none;
        draggedBlocks.clear();
        if (! mapping.snapLines.empty())
        {
            mapping.snapLines.clear();
            if (onSnapLinesChanged)
                onSnapLinesChanged();
        }
        repaint();
    }

    void TrackLanes::mouseDoubleClick (const juce::MouseEvent& e)
    {
        if (e.x < mapping.laneX)
        {
            if (auto* track = trackAt (e.y))
                renameNode (track->path, "Rename track");
            return;
        }

        // add a control point when editing an envelope
        const model::Block* editedBlock = nullptr;
        if (auto* env = currentEnvelope (editedBlock); env != nullptr && isCurveMode())
        {
            // handleCurvatureModeDoubleClick: the segment becomes a straight line again
            for (auto& r : rows())
            {
                if (r.track->path != parentPath (editedBlock->path) || ! blockBounds (*editedBlock, r.bounds).contains (e.position))
                    continue;
                if (auto* cp = segmentStartAt (*env, juce::roundToInt (mapping.xToTick (e.position.x)) - editedBlock->start))
                    context.controller.setControlPointCurvature (cp->path, 0.0f);
                return;
            }
        }

        if (auto* env = currentEnvelope (editedBlock))
        {
            for (auto& r : rows())
            {
                if (r.track->path != parentPath (editedBlock->path))
                    continue;
                auto bb = blockBounds (*editedBlock, r.bounds);
                if (! bb.contains (e.position))
                    continue;
                auto target = EndpointInfo::find (context.session, env->targetPath);
                const int tick = juce::jlimit (0, editedBlock->length(), (int) std::round (mapping.xToTick (e.position.x)) - editedBlock->start);
                const double unit = juce::jlimit (0.0, 1.0, (double) ((bb.getBottom() - 3.0f - e.position.y) / (bb.getHeight() - 20.0f)));
                context.controller.addControlPoint (env->path, tick, (float) target.fromUnit (unit));
                return;
            }
        }

        const model::Track* track = nullptr;
        if (auto* block = blockAt (e.getPosition(), &track))
        {
            if (onOpenBlock)
                onOpenBlock (block->path);
            return;
        }

        // double-click on an empty lane: new block, up to the next block
        if (auto* t = trackAt (e.y))
        {
            const int rate = juce::jmax (1, context.model.get().player.rate);
            const int start = juce::jmax (0, snapTicks ((int) mapping.xToTick (e.position.x), rate));
            int end = start + ProjectController::newBlockSeconds * rate;
            for (auto& b : t->blocks)
            {
                if (start >= b.start && start < b.end)
                    return;
                if (b.start > start)
                    end = juce::jmin (end, b.start);
            }
            if (end - start >= rate / 4)
            {
                auto block = context.controller.addBlock (t->path, start, end);
                context.model.selectedBlock = block;
                if (onSelectionChanged) onSelectionChanged();
            }
        }
    }

    bool TrackLanes::keyPressed (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        {
            deleteSelection();
            return true;
        }
        return false;
    }

    void TrackLanes::deleteSelection()
    {
        auto& model = context.model;
        // as ArrangeView.as: Delete/Backspace removes blocks only; tracks are deleted
        // explicitly from their context menu
        if (model.selectedBlock.isEmpty())
            return;
        context.controller.remove (model.selectedBlock);
        model.selectedBlock = {};
        if (onSelectionChanged) onSelectionChanged();
    }

    void TrackLanes::renameNode (const juce::String& path, const juce::String& title)
    {
        askForName (title, lastElement (path), [this, path] (juce::String name)
        {
            auto result = context.controller.rename (path, name);
            if (result.failed())
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Rename", result.getErrorMessage());
        });
    }

    void TrackLanes::showBlockMenu (const model::Block& block)
    {
        const auto path = block.path;
        juce::PopupMenu menu;
        menu.addItem ("Open in Module View", [this, path] { if (onOpenBlock) onOpenBlock (path); });
        menu.addItem ("Rename...", [this, path] { renameNode (path, "Rename block"); });

        juce::PopupMenu envelopes;
        envelopes.addItem ("None", true, editedEnvelope.isEmpty(), [this] { editedEnvelope = {}; repaint(); });
        for (auto& env : block.envelopes)
        {
            const auto envPath = env.path;
            envelopes.addItem (lastElement (parentPath (env.targetPath)) + "." + lastElement (env.targetPath),
                               true, editedEnvelope == envPath, [this, envPath] { editedEnvelope = envPath; repaint(); });
        }
        menu.addSubMenu ("Edit envelope", envelopes, ! block.envelopes.empty());
        menu.addItem ("Curve mode", ! block.envelopes.empty(), block.userData.curvatureMode, [this, path]
        {
            context.model.selectedBlock = path;
            toggleCurveMode();
        });

        menu.addItem ("Export block...", [this, path]
        {
            auto chooser = std::make_shared<juce::FileChooser> ("Export block",
                                                                context.session.getLocations().userBlockLibrary.getChildFile (lastElement (path) + ".integra"), "*.integra");
            chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [this, path, chooser] (const juce::FileChooser& fc)
            {
                if (fc.getResult() != juce::File())
                {
                    auto r = context.controller.exportNode (path, fc.getResult());
                    if (r.failed())
                        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Export", r.getErrorMessage());
                }
            });
        });
        menu.addSeparator();
        menu.addItem ("Delete block", [this, path] { context.controller.remove (path); context.model.selectedBlock = {}; repaint(); });
        menu.showMenuAsync (juce::PopupMenu::Options());
    }

    void TrackLanes::showTrackMenu (const model::Track& track)
    {
        const auto path = track.path;
        const int index = (int) std::distance (context.model.get().tracks.data(), &track);
        juce::PopupMenu menu;
        menu.addItem ("Rename...", [this, path] { renameNode (path, "Rename track"); });

        juce::PopupMenu colours;
        const std::pair<const char*, juce::uint32> palette[] = { { "Blue", 0x0F8AEB }, { "Orange", 0xEE7511 }, { "Purple", 0xD449D0 },
                                                                 { "Olive", 0xAB9800 }, { "Red", 0xFF5848 }, { "Green", 0x2EB429 },
                                                                 { "Grey", 0x808080 } };
        for (auto& [name, rgb] : palette)
        {
            const auto colour = juce::Colour (0xff000000 | rgb);
            colours.addItem (name, [this, path, colour] { context.controller.setTrackColour (path, colour); });
        }
        menu.addSubMenu ("Colour", colours);

        juce::PopupMenu heights;
        for (int h : { 48, 64, 96, 140 })
            heights.addItem (juce::String (h) + " px", true, track.userData.arrangeViewHeight == h,
                             [this, path, h] { context.controller.setTrackHeight (path, h); });
        menu.addSubMenu ("Height", heights);

        menu.addItem ("Move up", index > 0, false, [this, path, index] { context.controller.moveTrack (path, index - 1); });
        menu.addItem ("Move down", index + 1 < (int) context.model.get().tracks.size(), false,
                      [this, path, index] { context.controller.moveTrack (path, index + 1); });
        menu.addItem ("Import block...", [this, path]
        {
            auto chooser = std::make_shared<juce::FileChooser> ("Import block", context.session.getLocations().blockLibrary, "*.integra");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this, path, chooser] (const juce::FileChooser& fc)
            {
                if (fc.getResult() == juce::File())
                    return;
                int start = 0;
                if (auto* t = context.model.get().findTrack (path))
                    for (auto& b : t->blocks)
                        start = juce::jmax (start, b.end);
                auto r = context.controller.importBlock (fc.getResult(), path, start);
                if (r.failed())
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Import", r.getErrorMessage());
            });
        });
        menu.addSeparator();
        menu.addItem ("Delete track", [this, path] { context.controller.remove (path); context.model.selectedTrack = {}; });
        menu.showMenuAsync (juce::PopupMenu::Options());
    }

    //==============================================================================
    ArrangeView::ArrangeView (AppContext& c)
        : context (c), ruler (c, mapping), lanes (c, mapping), blockLibrary (c)
    {
        mapping.laneX = TrackLanes::headerWidth;

        addAndMakeVisible (ruler);
        viewport.setViewedComponent (&lanes, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);

        horizontalScroll.addListener (this);
        horizontalScroll.setAutoHide (false);
        addAndMakeVisible (horizontalScroll);

        zoomIn.onClick = [this] { zoom (1.5, (float) lanes.getWidth() * 0.5f); };
        zoomOut.onClick = [this] { zoom (1.0 / 1.5, (float) lanes.getWidth() * 0.5f); };
        zoomIn.setTooltip ("Zoom in (Ctrl + mouse wheel)");
        zoomOut.setTooltip ("Zoom out");
        addAndMakeVisible (zoomIn);
        addAndMakeVisible (zoomOut);

        if (auto* settings = context.properties.getUserSettings())
            blockLibraryWidth = juce::jlimit ((int) BlockLibrary::minimumWidth, (int) BlockLibrary::maximumWidth,
                                              settings->getIntValue ("blockLibraryWidth", BlockLibrary::defaultWidth));
        blockLibrary.getPlayPosition = [this] { return lanes.playPosition; };
        addAndMakeVisible (blockLibrary);
        addAndMakeVisible (widthHandle);

        lanes.onOpenBlock = [this] (const juce::String& block) { if (onOpenBlock) onOpenBlock (block); };
        lanes.onSelectionChanged = [this] { ruler.repaint(); if (onSelectionChanged) onSelectionChanged(); };
        ruler.onSnapLinesChanged = [this] { lanes.repaint(); };
        lanes.onSnapLinesChanged = [this] { ruler.repaint(); };
        ruler.onAutoScroll = [this] (double deltaTicks)
        {
            mapping.scrollTicks = juce::jmax (0.0, mapping.scrollTicks + deltaTicks);
            updateScrollBar (true);
        };

        context.model.addChangeListener (this);
        changeListenerCallback (nullptr);
        startTimerHz (30);
    }

    ArrangeView::~ArrangeView()
    {
        storeTimelineState();
        context.model.removeChangeListener (this);
    }

    void ArrangeView::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        auto& ud = context.model.get().userData;
        if (! timelineStateDirty)
        {
            mapping.scrollTicks = juce::jmax (0.0, ud.timelineScroll);
            mapping.pixelsPerTick = juce::jlimit (0.04, 25.0, ud.timelineZoom);
        }
        resized();
        repaint();
    }

    void ArrangeView::timerCallback()
    {
        const int tick = context.controller.getPlayPosition();
        if (tick != ruler.playPosition)
        {
            ruler.playPosition = lanes.playPosition = tick;
            ruler.repaint();
            lanes.repaint();

            // follow the playhead while playing
            const float x = mapping.tickToX (tick);
            if (x > (float) lanes.getWidth() - 40.0f && context.controller.isPlaying())
            {
                mapping.scrollTicks = tick - 40.0 / mapping.pixelsPerTick;
                updateScrollBar (true);
            }
        }

        if (timelineStateDirty && juce::Time::getMillisecondCounter() - lastTimelineChange > 2000)
            storeTimelineState();
    }

    void ArrangeView::storeTimelineState()
    {
        if (! timelineStateDirty)
            return;

        timelineStateDirty = false;
        const double scroll = mapping.scrollTicks, zoomLevel = mapping.pixelsPerTick;
        context.controller.updateProjectUserData ([scroll, zoomLevel] (ProjectUserData& d)
        {
            d.timelineScroll = scroll;
            d.timelineZoom = zoomLevel;
        });
    }

    void ArrangeView::updateScrollBar (bool userChanged)
    {
        const int rate = juce::jmax (1, context.model.get().player.rate);
        const double visibleTicks = (lanes.getWidth() - mapping.laneX) / mapping.pixelsPerTick;
        const double total = juce::jmax ((double) context.model.get().getLength() + 30.0 * rate, mapping.scrollTicks + visibleTicks);
        horizontalScroll.setRangeLimits (0.0, total, juce::dontSendNotification);
        horizontalScroll.setCurrentRange (mapping.scrollTicks, visibleTicks, juce::dontSendNotification);
        if (userChanged)
        {
            timelineStateDirty = true;
            lastTimelineChange = juce::Time::getMillisecondCounter();
        }
        ruler.repaint();
        lanes.repaint();
    }

    void ArrangeView::scrollBarMoved (juce::ScrollBar*, double newRangeStart)
    {
        mapping.scrollTicks = juce::jmax (0.0, newRangeStart);
        updateScrollBar (true);
    }

    void ArrangeView::zoom (double factor, float aroundX)
    {
        const double tickAtX = mapping.xToTick (aroundX);
        mapping.pixelsPerTick = juce::jlimit (0.04, 25.0, mapping.pixelsPerTick * factor);
        mapping.scrollTicks = juce::jmax (0.0, tickAtX - (aroundX - mapping.laneX) / mapping.pixelsPerTick);
        updateScrollBar (true);
    }

    void ArrangeView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
    {
        if (e.mods.isCommandDown() || e.mods.isCtrlDown())
        {
            zoom (wheel.deltaY > 0 ? 1.2 : 1.0 / 1.2, (float) e.getEventRelativeTo (this).x);
            return;
        }

        const float delta = std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? wheel.deltaX : (e.mods.isShiftDown() ? wheel.deltaY : 0.0f);
        if (std::abs (delta) > 0.0f)
        {
            mapping.scrollTicks = juce::jmax (0.0, mapping.scrollTicks - delta * 200.0 / mapping.pixelsPerTick);
            updateScrollBar (true);
        }
        else
        {
            Component::mouseWheelMove (e, wheel);
        }
    }

    void ArrangeView::paint (juce::Graphics& g)
    {
        g.fillAll (Theme::get().background);
    }

    void ArrangeView::WidthHandle::mouseDrag (const juce::MouseEvent& e)
    {
        view.blockLibraryWidth = juce::jlimit ((int) BlockLibrary::minimumWidth, (int) BlockLibrary::maximumWidth,
                                               startWidth - e.getDistanceFromDragStartX());
        view.resized();
    }

    void ArrangeView::WidthHandle::mouseUp (const juce::MouseEvent&)
    {
        if (auto* settings = view.context.properties.getUserSettings())
            settings->setValue ("blockLibraryWidth", view.blockLibraryWidth);
    }

    void ArrangeView::resized()
    {
        auto area = getLocalBounds();

        // the Block Library sits at the right, as in the Flex Arrange view
        blockLibrary.setBounds (area.removeFromRight (juce::jmin (blockLibraryWidth, juce::jmax (0, getWidth() - 300))));
        widthHandle.setBounds (area.removeFromRight (4));
        auto rulerArea = area.removeFromTop (TimelineRuler::sceneBarHeight + 24);
        ruler.setBounds (rulerArea);
        zoomOut.setBounds (TrackLanes::headerWidth - 58, TimelineRuler::sceneBarHeight + 2, 24, 20);
        zoomIn.setBounds (TrackLanes::headerWidth - 30, TimelineRuler::sceneBarHeight + 2, 24, 20);

        horizontalScroll.setBounds (area.removeFromBottom (14).withTrimmedLeft (TrackLanes::headerWidth));
        viewport.setBounds (area);
        lanes.setSize (viewport.getMaximumVisibleWidth(), juce::jmax (lanes.getTotalHeight(), viewport.getMaximumVisibleHeight()));
        updateScrollBar();
    }
}
