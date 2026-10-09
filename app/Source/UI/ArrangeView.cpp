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

    juce::Rectangle<float> TimelineRuler::sceneBounds (const model::Scene& s) const
    {
        const float x1 = mapping.tickToX (s.start), x2 = mapping.tickToX (s.end());
        return { x1, 2.0f, juce::jmax (4.0f, x2 - x1), (float) sceneBarHeight - 4.0f };
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

        // scenes
        for (auto& s : p.scenes)
        {
            auto r = sceneBounds (s);
            const bool current = p.player.currentScene == s.name;
            g.setColour (t.sceneFill.withAlpha (current ? 1.0f : 0.6f));
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (t.outline);
            g.drawRoundedRectangle (r, 3.0f, current ? 2.0f : 1.0f);
            g.setColour (juce::Colours::black);
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            auto label = s.name + (s.userData.keybinding.isNotEmpty() ? "  [" + s.userData.keybinding + "]" : juce::String());
            g.drawFittedText (label, r.reduced (4, 0).toNearestInt(), juce::Justification::centredLeft, 1);
        }

        if (! pendingScene.isEmpty())
        {
            g.setColour (t.sceneFill.withAlpha (0.4f));
            g.fillRect (juce::Rectangle<float> (mapping.tickToX (pendingScene.getStart()), 2.0f,
                                                mapping.tickToX (pendingScene.getEnd()) - mapping.tickToX (pendingScene.getStart()),
                                                (float) sceneBarHeight - 4.0f));
        }

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

    void TimelineRuler::mouseDown (const juce::MouseEvent& e)
    {
        if (e.x < mapping.laneX)
            return;

        auto& p = context.model.get();
        const int tick = juce::jmax (0, (int) mapping.xToTick ((float) e.x));
        drag = Drag::none;

        if (e.y < sceneBarHeight)
        {
            for (auto& s : p.scenes)
            {
                auto r = sceneBounds (s);
                if (! r.contains (e.position))
                    continue;

                if (e.mods.isPopupMenu())
                {
                    const auto path = s.path;
                    juce::PopupMenu menu;
                    juce::PopupMenu modes;
                    for (auto mode : { "hold", "play", "loop" })
                        modes.addItem (mode, true, s.mode == mode, [this, path, mode]
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
                        askForName ("Scene key (one character, empty for none)", scene ? scene->userData.keybinding : juce::String(),
                                    [this, path] (juce::String key) { context.controller.setSceneKeybinding (path, key.trim().substring (0, 1).toLowerCase()); });
                    });
                    menu.addSeparator();
                    menu.addItem ("Delete scene", [this, path] { context.controller.remove (path); });
                    menu.showMenuAsync (juce::PopupMenu::Options());
                    return;
                }

                context.controller.activateScene (s.path);
                if (onSceneSelected)
                    onSceneSelected (s.path);

                dragScene = s.path;
                dragStartTick = tick;
                dragOriginalStart = s.start;
                dragOriginalLength = s.length;
                drag = (e.x > r.getRight() - 6) ? Drag::resizeScene : Drag::moveScene;
                return;
            }

            // drag in empty scene bar to create a scene
            drag = Drag::newScene;
            dragStartTick = tick;
            pendingScene = { tick, tick };
            return;
        }

        drag = Drag::playhead;
        context.controller.setPlayPosition (tick);
        playPosition = tick;
        repaint();
    }

    void TimelineRuler::mouseDrag (const juce::MouseEvent& e)
    {
        const int tick = juce::jmax (0, (int) mapping.xToTick ((float) e.x));
        const int rate = juce::jmax (1, context.model.get().player.rate);

        switch (drag)
        {
            case Drag::playhead:
                context.controller.setPlayPosition (tick);
                playPosition = tick;
                break;

            case Drag::newScene:
                pendingScene = { snapTicks (juce::jmin (dragStartTick, tick), rate), snapTicks (juce::jmax (dragStartTick, tick), rate) };
                break;

            case Drag::moveScene:
            case Drag::resizeScene:
            {
                const int delta = snapTicks (tick - dragStartTick, rate);
                auto* s = context.model.get().findScene (dragScene);
                if (s == nullptr)
                    break;
                // preview by temporarily adjusting through the model is avoided; draw a pending range instead
                pendingScene = drag == Drag::moveScene ? juce::Range<int> (juce::jmax (0, dragOriginalStart + delta), juce::jmax (0, dragOriginalStart + delta) + dragOriginalLength)
                                                       : juce::Range<int> (dragOriginalStart, dragOriginalStart + juce::jmax (rate / 4, dragOriginalLength + delta));
                break;
            }

            case Drag::none:
                break;
        }

        repaint();
    }

    void TimelineRuler::mouseUp (const juce::MouseEvent&)
    {
        if (drag == Drag::newScene && pendingScene.getLength() > 0)
        {
            auto scene = context.controller.addScene (pendingScene.getStart(), pendingScene.getLength());
            if (scene.isNotEmpty())
                context.controller.activateScene (scene);
        }
        else if ((drag == Drag::moveScene || drag == Drag::resizeScene) && ! pendingScene.isEmpty())
        {
            if (auto* s = context.model.get().findScene (dragScene))
                if (pendingScene.getStart() != s->start || pendingScene.getLength() != s->length)
                    context.controller.setScene (dragScene, pendingScene.getStart(), pendingScene.getLength(), s->mode);
        }

        pendingScene = {};
        drag = Drag::none;
        repaint();
    }

    void TimelineRuler::mouseDoubleClick (const juce::MouseEvent& e)
    {
        if (e.y >= sceneBarHeight || e.x < mapping.laneX)
            return;
        // double-click in empty scene bar: a 10-second scene
        for (auto& s : context.model.get().scenes)
            if (sceneBounds (s).contains (e.position))
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

    void TrackLanes::paintEnvelopes (juce::Graphics& g, const model::Block& block, juce::Rectangle<float> r)
    {
        auto& t = Theme::get();

        for (auto& env : block.envelopes)
        {
            if (env.points.empty() || env.targetPath.isEmpty())
                continue;

            auto target = EndpointInfo::find (context.session, env.targetPath);
            const bool edited = env.path == editedEnvelope && block.path == context.model.selectedBlock;

            juce::Path path;
            for (size_t i = 0; i < env.points.size(); ++i)
            {
                auto pt = pointPosition (env, env.points[i], block, r, target);
                if (i == 0)
                {
                    path.startNewSubPath (r.getX(), pt.y);
                    path.lineTo (pt);
                }
                else
                {
                    path.lineTo (pt);
                }
            }
            auto last = pointPosition (env, env.points.back(), block, r, target);
            path.lineTo (r.getRight(), last.y);

            g.setColour (t.envelope.withAlpha (edited ? 1.0f : 0.45f));
            g.strokePath (path, juce::PathStrokeType (edited ? 2.0f : 1.2f));

            if (edited)
            {
                for (auto& cp : env.points)
                {
                    auto pt = pointPosition (env, cp, block, r, target);
                    g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (pt));
                }
                g.setFont (juce::FontOptions (11.0f));
                g.drawText (lastElement (parentPath (env.targetPath)) + "." + lastElement (env.targetPath),
                            r.reduced (6, 2).toNearestInt(), juce::Justification::bottomRight);
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
            for (auto& b : track.blocks)
            {
                if (drag == Drag::moveBlock && b.path == dragBlock)
                    continue;   // drawn as preview below

                auto r = blockBounds (b, row.bounds);
                const bool selected = model.selectedBlock == b.path;
                const bool active = playPosition >= b.start && playPosition < b.end;

                g.setColour (colour.withAlpha (active ? 0.55f : 0.32f).overlaidWith (t.blockFill.withAlpha (0.4f)));
                g.fillRoundedRectangle (r, 4.0f);
                g.setColour (selected ? t.accent : colour.darker (0.3f));
                g.drawRoundedRectangle (r, 4.0f, selected ? 2.5f : 1.2f);

                g.setColour (t.text);
                g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
                g.drawFittedText (b.name, r.reduced (6, 3).toNearestInt(), juce::Justification::topLeft, 1);

                paintEnvelopes (g, b, r);
            }

            if (drag == Drag::moveBlock || drag == Drag::resizeStart || drag == Drag::resizeEnd)
            {
                if (dragTargetTrack == track.path && ! dragPreview.isEmpty())
                {
                    auto preview = dragPreview.withY ((float) row.bounds.getY() + 4.0f).withHeight ((float) row.bounds.getHeight() - 8.0f);
                    g.setColour (t.accent.withAlpha (0.3f));
                    g.fillRoundedRectangle (preview, 4.0f);
                    g.setColour (t.accent);
                    g.drawRoundedRectangle (preview, 4.0f, 2.0f);
                }
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
                    if (e.position.x > b.getRight() - 6 || e.position.x < b.getX() + 6)
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
        const model::Block* editedBlock = nullptr;
        if (auto* env = currentEnvelope (editedBlock))
        {
            for (auto& r : rows())
            {
                auto bb = editedBlock != nullptr ? blockBounds (*editedBlock, r.bounds) : juce::Rectangle<float>();
                if (r.track->path != parentPath (editedBlock->path) || ! bb.contains (e.position))
                    continue;

                auto target = EndpointInfo::find (context.session, env->targetPath);
                for (auto& cp : env->points)
                {
                    if (pointPosition (*env, cp, *editedBlock, bb, target).getDistanceFrom (e.position) < 7.0f)
                    {
                        if (e.mods.isPopupMenu())
                        {
                            if (env->points.size() > 1)
                                context.controller.remove (cp.path);
                            return;
                        }
                        drag = Drag::controlPoint;
                        dragPoint = cp.path;
                        dragBlock = editedBlock->path;
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
                dragStartTick = (int) mapping.xToTick (e.position.x);
                originalStart = block->start;
                originalEnd = block->end;
                dragTargetTrack = track->path;
                dragPreview = bb;
                if (e.position.x > bb.getRight() - 6)       drag = Drag::resizeEnd;
                else if (e.position.x < bb.getX() + 6)      drag = Drag::resizeStart;
                else                                        drag = Drag::moveBlock;
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
        const int rate = juce::jmax (1, context.model.get().player.rate);

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
                const int tick = juce::jlimit (0, block->length(), (int) std::round (mapping.xToTick (e.position.x)) - block->start);
                const double unit = juce::jlimit (0.0, 1.0, (double) ((bb.getBottom() - 3.0f - e.position.y) / (bb.getHeight() - 20.0f)));
                context.controller.moveControlPoint (dragPoint, tick, (float) target.fromUnit (unit));
            }
            return;
        }

        if (drag == Drag::none)
            return;

        const int delta = snapTicks ((int) mapping.xToTick (e.position.x) - dragStartTick, rate);
        int start = originalStart, end = originalEnd;

        if (drag == Drag::moveBlock)
        {
            start = juce::jmax (0, originalStart + delta);
            end = start + (originalEnd - originalStart);
            if (auto* t = trackAt (e.y))
                dragTargetTrack = t->path;
        }
        else if (drag == Drag::resizeStart)
        {
            start = juce::jlimit (0, originalEnd - rate / 4, originalStart + delta);
        }
        else if (drag == Drag::resizeEnd)
        {
            end = juce::jmax (originalStart + rate / 4, originalEnd + delta);
        }

        dragPreview = { mapping.tickToX (start), 0.0f, mapping.tickToX (end) - mapping.tickToX (start), 1.0f };
        dragStartTickPreview = { start, end };
        repaint();
    }

    void TrackLanes::mouseUp (const juce::MouseEvent&)
    {
        if (drag == Drag::moveBlock || drag == Drag::resizeStart || drag == Drag::resizeEnd)
        {
            auto range = dragStartTickPreview;
            const auto blockPath = dragBlock;
            const auto targetTrack = dragTargetTrack;
            drag = Drag::none;

            auto* block = context.model.get().findBlock (blockPath);
            auto* track = context.model.get().findTrack (targetTrack);

            if (block != nullptr && track != nullptr && ! range.isEmpty())
            {
                // blocks in one track may not overlap
                bool overlaps = false;
                for (auto& other : track->blocks)
                    if (other.path != blockPath && juce::Range<int> (other.start, other.end).intersects (range))
                        overlaps = true;

                if (! overlaps)
                {
                    // the controller rebuilds the model, so don't keep `block` across its calls
                    const int oldStart = block->start, oldEnd = block->end;
                    juce::String path = blockPath;
                    if (targetTrack != parentPath (blockPath))
                        path = context.controller.moveBlockToTrack (blockPath, targetTrack);
                    if (range.getStart() != oldStart || range.getEnd() != oldEnd)
                        context.controller.repositionBlock (path, range.getStart(), range.getEnd());
                }
            }
        }

        drag = Drag::none;
        dragStartTickPreview = {};
        dragPreview = {};
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
        if (model.selectedBlock.isNotEmpty())
        {
            context.controller.remove (model.selectedBlock);
            model.selectedBlock = {};
        }
        else if (model.selectedTrack.isNotEmpty())
        {
            context.controller.remove (model.selectedTrack);
            model.selectedTrack = {};
        }
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
        : context (c), ruler (c, mapping), lanes (c, mapping)
    {
        mapping.laneX = TrackLanes::headerWidth;

        addAndMakeVisible (ruler);
        viewport.setViewedComponent (&lanes, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);

        horizontalScroll.addListener (this);
        horizontalScroll.setAutoHide (false);
        addAndMakeVisible (horizontalScroll);

        zoomIn.onClick = [this] { zoom (1.5, (float) getWidth() * 0.5f); };
        zoomOut.onClick = [this] { zoom (1.0 / 1.5, (float) getWidth() * 0.5f); };
        zoomIn.setTooltip ("Zoom in (Ctrl + mouse wheel)");
        zoomOut.setTooltip ("Zoom out");
        addAndMakeVisible (zoomIn);
        addAndMakeVisible (zoomOut);

        lanes.onOpenBlock = [this] (const juce::String& block) { if (onOpenBlock) onOpenBlock (block); };
        lanes.onSelectionChanged = [this] { ruler.repaint(); if (onSelectionChanged) onSelectionChanged(); };

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
            if (x > (float) getWidth() - 40.0f && context.controller.isPlaying())
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
        const double visibleTicks = (getWidth() - mapping.laneX) / mapping.pixelsPerTick;
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

    void ArrangeView::resized()
    {
        auto area = getLocalBounds();
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
