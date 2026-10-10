#include "LiveView.h"
#include "IntegraLookAndFeel.h"

namespace integra
{
    namespace
    {
        class ResizeCorner  : public juce::ResizableCornerComponent
        {
        public:
            using juce::ResizableCornerComponent::ResizableCornerComponent;
            std::function<void()> onResizeEnd;

            void mouseUp (const juce::MouseEvent& e) override
            {
                juce::ResizableCornerComponent::mouseUp (e);
                if (onResizeEnd)
                    onResizeEnd();
            }
        };

        /** A live-view control: the widget plus a grab strip for moving and a resize corner. */
        class LiveControl  : public juce::Component
        {
        public:
            LiveControl (AppContext& c, const juce::String& modulePath, const WidgetSpec& spec)
                : context (c), module (modulePath), label (spec.label)
            {
                widget = IntegraWidget::create (c, modulePath, spec);
                if (widget != nullptr)
                    addAndMakeVisible (*widget);
                addAndMakeVisible (resizer);
                resizer.onResizeEnd = [this] { store(); };
                constrainer.setMinimumSize (50, 60);
            }

            void paint (juce::Graphics& g) override
            {
                auto& t = Theme::get();
                auto r = getLocalBounds().toFloat().reduced (1.0f);
                g.setColour (t.panel);
                g.fillRoundedRectangle (r, 5.0f);
                g.setColour (t.outline.withAlpha (0.7f));
                g.drawRoundedRectangle (r, 5.0f, 1.0f);

                auto strip = getLocalBounds().removeFromTop (stripHeight).reduced (6, 0);
                g.setColour (t.textDim);
                g.setFont (juce::FontOptions (10.0f));
                g.drawText (lastElement (module), strip, juce::Justification::centredLeft);
            }

            void resized() override
            {
                auto area = getLocalBounds();
                area.removeFromTop (stripHeight);
                if (widget != nullptr)
                    widget->setBounds (area);
                resizer.setBounds (getWidth() - 12, getHeight() - 12, 12, 12);
            }

            void mouseDown (const juce::MouseEvent& e) override
            {
                dragStartBounds = getBounds();
                dragStartInParent = getParentComponent() != nullptr ? e.getEventRelativeTo (getParentComponent()).position : e.position;
            }

            void mouseDrag (const juce::MouseEvent& e) override
            {
                if (getParentComponent() == nullptr)
                    return;
                const auto delta = (e.getEventRelativeTo (getParentComponent()).position - dragStartInParent).roundToInt();
                // keep the control inside the block area (LiveViewBlock clamps to >= 0)
                setTopLeftPosition (juce::jmax (0, dragStartBounds.getX() + delta.x), juce::jmax (topMargin, dragStartBounds.getY() + delta.y));
            }
            void mouseUp (const juce::MouseEvent&) override         { store(); }

            static constexpr int stripHeight = 14;

            /** y offset of the block area within the track panel (its header). */
            int topMargin = 0;

        private:
            void store()
            {
                // stored relative to the block area, as placed by TrackPanel
                auto bounds = getBounds().translated (0, -topMargin).toFloat();
                const auto moduleName = lastElement (module);
                const auto widgetLabel = label;
                context.controller.updateBlockUserData (parentPath (module), [&] (BlockUserData& d)
                {
                    for (auto& c : d.liveViewControls)
                        if (c.moduleName == moduleName && c.widgetLabel == widgetLabel)
                            c.bounds = bounds;
                });
            }

            AppContext& context;
            juce::String module, label;
            std::unique_ptr<IntegraWidget> widget;
            juce::Rectangle<int> dragStartBounds;
            juce::Point<float> dragStartInParent;
            juce::ComponentBoundsConstrainer constrainer;
            ResizeCorner resizer { this, &constrainer };
        };
    }

    //==============================================================================
    class LiveView::TrackPanel  : public juce::Component
    {
    public:
        TrackPanel (AppContext& c, const model::Track& track, const juce::String& blockPath)
            : context (c), trackName (track.name), colour (track.userData.getColour()), block (blockPath)
        {
            auto* b = context.model.get().findBlock (blockPath);
            if (b == nullptr)
                return;

            blockName = b->name;
            for (auto& lvc : b->userData.liveViewControls)
            {
                const auto modulePath = blockPath + "." + lvc.moduleName;
                for (auto& spec : WidgetSpec::forModule (context.session, modulePath))
                {
                    if (spec.label != lvc.widgetLabel)
                        continue;

                    auto* control = controls.add (new LiveControl (c, modulePath, spec));
                    auto bounds = lvc.bounds;
                    if (bounds.isEmpty())
                        bounds = { 10.0f, 10.0f, spec.position.getWidth(), spec.position.getHeight() + LiveControl::stripHeight };
                    control->topMargin = headerHeight;
                    control->setBounds (bounds.toNearestInt().translated (0, headerHeight));
                    addAndMakeVisible (control);
                }
            }
        }

        int getContentHeight() const
        {
            int bottom = headerHeight + 60;
            for (auto* c : controls)
                bottom = juce::jmax (bottom, c->getBottom() + 10);
            return bottom;
        }

        void paint (juce::Graphics& g) override
        {
            auto& t = Theme::get();
            g.fillAll (t.panel.interpolatedWith (colour, 0.06f));
            auto header = getLocalBounds().removeFromTop (headerHeight);
            g.setColour (colour);
            g.fillRect (header.removeFromLeft (8));
            g.setColour (t.text);
            g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
            g.drawText (trackName, header.reduced (8, 0), juce::Justification::centredLeft);
            g.setColour (t.textDim);
            g.setFont (juce::FontOptions (13.0f));
            g.drawText (blockName.isEmpty() ? juce::String ("(no block)") : blockName, header.reduced (8, 0).withTrimmedLeft (160),
                        juce::Justification::centredLeft);

            if (controls.isEmpty())
            {
                g.setFont (juce::FontOptions (12.0f));
                g.drawText (blockName.isEmpty() ? juce::String()
                                                : juce::String ("No live controls - use the LIVE toggle on a module's controls in the Module View"),
                            getLocalBounds().withTrimmedTop (headerHeight), juce::Justification::centred);
            }

            g.setColour (t.outline);
            g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
        }

        static constexpr int headerHeight = 28;

    private:
        AppContext& context;
        juce::String trackName, blockName;
        juce::Colour colour;
        juce::String block;
        juce::OwnedArray<LiveControl> controls;
    };

    //==============================================================================
    LiveView::LiveView (AppContext& c) : context (c)
    {
        setWantsKeyboardFocus (true);
        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (true, true);
        addAndMakeVisible (viewport);
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

    juce::String LiveView::blockToShow (const model::Track& track) const
    {
        // as LiveViewTrack: only the block under the playhead is shown
        for (auto& b : track.blocks)
            if (playPosition >= b.start && playPosition < b.end)
                return b.path;

        return {};
    }

    void LiveView::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        shownSignature = {};
        rebuild();
    }

    void LiveView::timerCallback()
    {
        if (! isShowing())
            return;

        playPosition = context.controller.getPlayPosition();

        // rebuild only when the set of shown blocks changes
        juce::String signature;
        for (auto& t : context.model.get().tracks)
            signature << blockToShow (t) << ";";
        if (signature != shownSignature)
            rebuild();

        // highlight the current scene
        auto& p = context.model.get();
        for (int i = 0; i < sceneButtons.size() && i < (int) p.scenes.size(); ++i)
            sceneButtons[i]->setToggleState (p.player.currentScene == p.scenes[(size_t) i].name, juce::dontSendNotification);
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
            shownSignature = {};
            rebuild();
        }
    }

    void LiveView::rebuild()
    {
        auto& p = context.model.get();
        playPosition = context.controller.getPlayPosition();

        sceneButtons.clear();
        for (auto& s : p.scenes)
        {
            auto* b = sceneButtons.add (new juce::TextButton (s.name + (s.userData.keybinding.isNotEmpty() ? "  [" + s.userData.keybinding + "]" : juce::String())));
            b->setClickingTogglesState (false);
            const auto path = s.path;
            b->onClick = [this, path] { context.controller.activateScene (path); };
            addAndMakeVisible (b);
        }

        panels.clear();
        shownSignature = {};
        int y = 0;
        for (auto& t : p.tracks)
        {
            const auto block = blockToShow (t);
            shownSignature << block << ";";
            auto* panel = panels.add (new TrackPanel (context, t, block));
            panel->setBounds (0, y, juce::jmax (600, viewport.getMaximumVisibleWidth()), panel->getContentHeight());
            content.addAndMakeVisible (panel);
            y += panel->getHeight();
        }
        content.setSize (juce::jmax (600, viewport.getMaximumVisibleWidth()), y);

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
                return true;
            }
        }
        return false;
    }

    void LiveView::paint (juce::Graphics& g)
    {
        g.fillAll (Theme::get().background);
        if (context.model.get().tracks.empty())
        {
            g.setColour (Theme::get().textDim);
            g.drawText ("No tracks", getLocalBounds(), juce::Justification::centred);
        }
    }

    void LiveView::resized()
    {
        auto area = getLocalBounds();
        if (! sceneButtons.isEmpty())
        {
            auto bar = area.removeFromTop (36).reduced (6, 5);
            for (auto* b : sceneButtons)
            {
                b->setBounds (bar.removeFromLeft (juce::jmin (160, juce::jmax (90, bar.getWidth() / juce::jmax (1, sceneButtons.size())))));
                bar.removeFromLeft (6);
            }
        }
        viewport.setBounds (area);
    }
}
