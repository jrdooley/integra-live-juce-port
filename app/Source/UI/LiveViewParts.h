#pragma once

// The Live view's track panels and controls; a separate header so the GUI test can use them.

#include "LiveView.h"
#include "IntegraLookAndFeel.h"

namespace integra
{
    //==============================================================================
    /** A live-view control: the widget, a title strip for moving it and borders for resizing. */
    class LiveView::LiveControl  : public juce::Component
    {
    public:
        LiveControl (AppContext& c, const juce::String& modulePath, const WidgetSpec& spec)
            : context (c), module (modulePath), label (spec.label)
        {
            widget = IntegraWidget::create (c, modulePath, spec);
            if (widget != nullptr)
                addAndMakeVisible (*widget);
            constrainer.setMinimumSize (40, 40);
            border.setBorderThickness (juce::BorderSize<int> (5));
            addAndMakeVisible (border);
        }

        const juce::String& getModule() const noexcept  { return module; }
        const juce::String& getLabel() const noexcept   { return label; }

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
            auto area = getLocalBounds().reduced (3, 0);
            area.removeFromTop (stripHeight);
            area.removeFromBottom (3);
            if (widget != nullptr)
                widget->setBounds (area);
            border.setBounds (getLocalBounds());
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            toFront (false);
            dragStartBounds = getBounds();
            dragStartInParent = getParentComponent() != nullptr ? e.getEventRelativeTo (getParentComponent()).position : e.position;
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (getParentComponent() == nullptr)
                return;
            const auto delta = (e.getEventRelativeTo (getParentComponent()).position - dragStartInParent).roundToInt();
            setBounds (snapped (dragStartBounds + delta, true, true, true, true, true));
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            clearSnapLines();
            store();
        }

        /** Keeps the control inside the block area and snaps its moving edges to the
            other controls' edges (LiveViewBlock.onRepositionControl). */
        juce::Rectangle<int> snapped (juce::Rectangle<int> r, bool move, bool left, bool right, bool top, bool bottom);

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

        void clearSnapLines();

        static constexpr int stripHeight = 14;

        /** y offset of the block area within the track panel (its header). */
        int topMargin = 0;

    private:
        struct Constrainer  : public juce::ComponentBoundsConstrainer
        {
            explicit Constrainer (LiveControl& c) : owner (c) {}

            void checkBounds (juce::Rectangle<int>& bounds, const juce::Rectangle<int>& previous, const juce::Rectangle<int>& limits,
                              bool top, bool left, bool bottom, bool right) override
            {
                juce::ComponentBoundsConstrainer::checkBounds (bounds, previous, limits, top, left, bottom, right);
                bounds = owner.snapped (bounds, false, left, right, top, bottom);
            }

            LiveControl& owner;
        };

        struct Border  : public juce::ResizableBorderComponent
        {
            Border (LiveControl& c, juce::ComponentBoundsConstrainer* k) : juce::ResizableBorderComponent (&c, k), owner (c) {}

            void mouseUp (const juce::MouseEvent& e) override
            {
                juce::ResizableBorderComponent::mouseUp (e);
                owner.clearSnapLines();
                owner.store();
            }

            LiveControl& owner;
        };

        AppContext& context;
        juce::String module, label;
        std::unique_ptr<IntegraWidget> widget;
        juce::Rectangle<int> dragStartBounds;
        juce::Point<float> dragStartInParent;
        Constrainer constrainer { *this };
        Border border { *this, &constrainer };
    };

    //==============================================================================
    class LiveView::TrackPanel  : public juce::Component
    {
    public:
        TrackPanel (AppContext& c, const model::Track& track, const juce::String& blockPath)
            : context (c), trackPath (track.path), trackName (track.name), colour (track.userData.getColour()), block (blockPath)
        {
            trackActive.setContainer (track.path);
            trackActive.setColour (colour);
            trackActive.onChange = [this] { repaint(); };
            trackMeter.setContainer (track.path);
            trackMeter.setColour (colour);
            addAndMakeVisible (trackActive);
            addAndMakeVisible (trackMeter);

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
                    // clamped to the block area (LiveViewBlock)
                    control->setBounds (bounds.toNearestInt().translated (0, headerHeight)
                                              .withPosition (juce::jmax (0, juce::roundToInt (bounds.getX())),
                                                             headerHeight + juce::jmax (0, juce::roundToInt (bounds.getY()))));
                    addAndMakeVisible (control);
                }
            }
        }

        const juce::String& getTrackPath() const noexcept   { return trackPath; }
        const juce::String& getBlock() const noexcept       { return block; }
        const juce::OwnedArray<LiveControl>& getControls() const noexcept  { return controls; }
        ActiveButton& getActiveButton() noexcept            { return trackActive; }
        ContainerMeter& getMeter() noexcept                 { return trackMeter; }

        int getContentHeight() const
        {
            int bottom = headerHeight + 60;
            for (auto* c : controls)
                bottom = juce::jmax (bottom, c->getBottom() + 10);
            return bottom;
        }

        void resized() override
        {
            auto header = getLocalBounds().removeFromTop (headerHeight).reduced (8, 0);
            trackMeter.setBounds (header.removeFromRight (128).withSizeKeepingCentre (128, 6));
            header.removeFromRight (8);
            trackActive.setBounds (header.removeFromRight (20).withSizeKeepingCentre (20, 20));
        }

        void paint (juce::Graphics& g) override
        {
            auto& t = Theme::get();
            const bool active = ActiveButton::isEffectivelyActive (context, trackPath);
            const auto c = active ? colour : juce::Colour (0xff9a9a9a);
            g.fillAll (t.panel.interpolatedWith (c, 0.06f));
            auto header = getLocalBounds().removeFromTop (headerHeight);
            g.setColour (c);
            g.fillRect (header.removeFromLeft (8));
            g.setColour (active ? t.text : t.textDim);
            g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
            g.drawText (trackName, header.reduced (8, 0), juce::Justification::centredLeft);
            g.setColour (t.textDim);
            g.setFont (juce::FontOptions (13.0f));
            g.drawText (blockName.isEmpty() ? juce::String ("(no block)") : blockName, header.reduced (8, 0).withTrimmedLeft (160).withTrimmedRight (170),
                        juce::Justification::centredLeft);

            if (controls.isEmpty())
            {
                g.setFont (juce::FontOptions (12.0f));
                g.drawText (blockName.isEmpty() ? juce::String()
                                                : juce::String ("No live controls - right-click here, or use the LIVE toggle on a module's controls in the Module View"),
                            getLocalBounds().withTrimmedTop (headerHeight), juce::Justification::centred);
            }

            g.setColour (t.outline);
            g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
        }

        void paintOverChildren (juce::Graphics& g) override
        {
            // snap lines while a control is moved or resized
            g.setColour (Theme::get().accent);
            for (int x : snapX)
                g.drawVerticalLine (x, (float) headerHeight, (float) getHeight());
            for (int y : snapY)
                g.drawHorizontalLine (y, 0.0f, (float) getWidth());
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu() && e.y >= headerHeight)
                showControlMenu (e.getPosition());
        }

        /** LiveViewBlock.onUpdateContextMenu: each module's controls, ticked if shown;
            choosing one adds it at the click point, or removes it. */
        void showControlMenu (juce::Point<int> position)
        {
            auto* b = context.model.get().findBlock (block);
            if (b == nullptr)
                return;

            juce::PopupMenu menu;
            for (auto& m : b->modules)
            {
                juce::PopupMenu sub;
                const auto modulePath = b->path + "." + m.name;
                for (auto& spec : WidgetSpec::forModule (context.session, modulePath))
                {
                    const bool shown = isShown (modulePath, spec.label);
                    sub.addItem (spec.label, true, shown, [this, modulePath, label = spec.label, position]
                    {
                        toggleControl (modulePath, label, position);
                    });
                }
                if (sub.getNumItems() > 0)
                    menu.addSubMenu (m.name, sub);
            }
            if (menu.getNumItems() == 0)
                menu.addItem ("This block has no modules", false, false, nullptr);
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea ({ localPointToGlobal (position), localPointToGlobal (position) }));
        }

        bool isShown (const juce::String& modulePath, const juce::String& widgetLabel) const
        {
            if (auto* b = context.model.get().findBlock (block))
                for (auto& c : b->userData.liveViewControls)
                    if (c.moduleName == lastElement (modulePath) && c.widgetLabel == widgetLabel)
                        return true;
            return false;
        }

        /** Shows a control at a point of this panel, or removes it if it's shown. */
        void toggleControl (const juce::String& modulePath, const juce::String& widgetLabel, juce::Point<int> position)
        {
            const bool shown = isShown (modulePath, widgetLabel);
            juce::Rectangle<float> bounds;
            if (! shown)
                for (auto& spec : WidgetSpec::forModule (context.session, modulePath))
                    if (spec.label == widgetLabel)
                        bounds = { (float) juce::jmax (0, position.x), (float) juce::jmax (0, position.y - headerHeight),
                                   spec.position.getWidth(), spec.position.getHeight() + (float) LiveControl::stripHeight };
            context.controller.setLiveViewControl (modulePath, widgetLabel, ! shown, bounds);
        }

        static constexpr int headerHeight = 28;

        std::vector<int> snapX, snapY;

    private:
        AppContext& context;
        juce::String trackPath, trackName, blockName;
        juce::Colour colour;
        juce::String block;
        ActiveButton trackActive { context };
        ContainerMeter trackMeter { context };
        juce::OwnedArray<LiveControl> controls;
    };

}
