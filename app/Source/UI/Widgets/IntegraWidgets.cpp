#include "IntegraWidgets.h"
#include "UI/IntegraLookAndFeel.h"

namespace integra
{
    const EndpointInfo IntegraWidget::missingInfo;

    //==============================================================================
    std::vector<WidgetSpec> WidgetSpec::forModule (IntegraSession& session, const juce::String& modulePath)
    {
        std::vector<WidgetSpec> specs;
        auto server = session.lock();
        auto* node = server->find_node (toPath (modulePath));
        if (node == nullptr)
            return specs;

        for (auto* w : node->get_interface_definition().get_widget_definitions())
        {
            WidgetSpec s;
            s.type = toString (w->get_type());
            s.label = toString (w->get_label());
            auto& pos = w->get_position();
            s.position = { pos.get_x(), pos.get_y(), pos.get_width(), pos.get_height() };
            for (auto& m : w->get_attribute_mappings())
                s.mapping[toString (m.first)] = toString (m.second);
            specs.push_back (s);
        }

        return specs;
    }

    //==============================================================================
    /** The padlock shown on a driven control; click for the menu that removes the driver. */
    class IntegraWidget::Padlock  : public juce::Component,
                                    public juce::SettableTooltipClient
    {
    public:
        explicit Padlock (IntegraWidget& w) : widget (w) {}

        void paint (juce::Graphics& g) override
        {
            auto& t = Theme::get();
            auto r = getLocalBounds().toFloat().reduced (1.5f);
            g.setColour (t.panel.withAlpha (0.85f));
            g.fillRoundedRectangle (r, 3.0f);

            auto body = r.reduced (r.getWidth() * 0.18f, 0).withTrimmedTop (r.getHeight() * 0.45f).withTrimmedBottom (r.getHeight() * 0.08f);
            juce::Path shackle;
            shackle.addCentredArc (body.getCentreX(), body.getY(), body.getWidth() * 0.32f, r.getHeight() * 0.3f,
                                   0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
            g.setColour (widget.isFullyLocked() ? t.text : t.textDim);
            g.strokePath (shackle, juce::PathStrokeType (1.6f));
            g.fillRoundedRectangle (body, 1.5f);
        }

        void mouseDown (const juce::MouseEvent&) override   { widget.showPadlockMenu(); }

    private:
        IntegraWidget& widget;
    };

    namespace
    {
        /** Covers a fully locked control so it can't be changed by the mouse. */
        class LockOverlay  : public juce::Component,
                             public juce::SettableTooltipClient
        {
        public:
            LockOverlay()   { setInterceptsMouseClicks (true, false); }
        };
    }

    //==============================================================================
    IntegraWidget::IntegraWidget (AppContext& c, const juce::String& path, const WidgetSpec& s)
        : context (c), modulePath (path), spec (s)
    {
        for (auto& [attribute, endpoint] : spec.mapping)
        {
            const auto fullPath = modulePath + "." + endpoint;
            infos[attribute] = EndpointInfo::find (context.session, fullPath);
            values[attribute] = context.session.getValue (fullPath);

            context.watcher.watch (fullPath, this, [this, attribute = attribute, fullPath] (const juce::String&, CCommandSource)
            {
                values[attribute] = context.session.getValue (fullPath);
                valuesChanged();
                repaint();
            });
        }

        auto& primary = info (spec.mapping.count ("value") ? "value" : spec.mapping.begin() != spec.mapping.end() ? spec.mapping.begin()->first : "value");
        setTooltip (primary.description.isNotEmpty() ? spec.label + ": " + primary.description : spec.label);

        addComponentListener (this);
        context.model.addChangeListener (this);
        updateLocks();
    }

    IntegraWidget::~IntegraWidget()
    {
        context.model.removeChangeListener (this);
        removeComponentListener (this);
        context.watcher.removeAll (this);
    }

    void IntegraWidget::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        updateLocks();
    }

    void IntegraWidget::updateLocks()
    {
        // ControlContainer.setControlWritableFlags
        lockedAttributes.clear();
        drivers.clear();
        auto& project = context.model.get();
        for (auto& [attribute, endpoint] : spec.mapping)
        {
            for (auto& d : project.driversOf (modulePath + "." + endpoint))
            {
                lockedAttributes.insert (attribute);
                drivers.push_back ({ attribute, d });
            }
        }

        if (lockedAttributes.empty())
        {
            padlock.reset();
            lockOverlay.reset();
            return;
        }

        juce::StringArray explanation;
        for (auto& [attribute, d] : drivers)
            explanation.add ("'" + attribute + "' is set by "
                             + (d.kind == model::Driver::Kind::envelope ? juce::String ("an envelope")
                                : d.kind == model::Driver::Kind::midi ? d.description
                                : "a routing from " + d.description));
        const auto tip = explanation.joinIntoString ("\n") + "\nClick the padlock to remove it.";

        if (padlock == nullptr)
        {
            padlock = std::make_unique<Padlock> (*this);
            addAndMakeVisible (*padlock);
        }
        static_cast<Padlock*> (padlock.get())->setTooltip (tip);

        if (isFullyLocked())
        {
            if (lockOverlay == nullptr)
            {
                lockOverlay = std::make_unique<LockOverlay>();
                addAndMakeVisible (*lockOverlay);
            }
            static_cast<LockOverlay*> (lockOverlay.get())->setTooltip (tip);
        }
        else
        {
            lockOverlay.reset();
        }

        layoutLock();
        repaint();
    }

    bool IntegraWidget::isFullyLocked() const
    {
        for (auto& [attribute, endpoint] : spec.mapping)
            if (! isLocked (attribute))
                return false;
        return ! spec.mapping.empty();
    }

    void IntegraWidget::componentMovedOrResized (juce::Component&, bool, bool wasResized)
    {
        if (wasResized)
            layoutLock();
    }

    void IntegraWidget::layoutLock()
    {
        if (lockOverlay != nullptr)
        {
            lockOverlay->setBounds (getLocalBounds());
            lockOverlay->toFront (false);
        }
        if (padlock != nullptr)
        {
            padlock->setBounds (2, 2, 16, 16);
            padlock->toFront (false);
        }
    }

    void IntegraWidget::showPadlockMenu()
    {
        // ControlContainer.onClickPadlock
        juce::PopupMenu menu;
        const bool multiple = spec.mapping.size() > 1;
        for (auto& [attribute, d] : drivers)
        {
            juce::String label = d.kind == model::Driver::Kind::envelope ? juce::String ("Delete Envelope") + (multiple ? " for " + attribute : juce::String())
                                                                         : "Delete Routing from " + d.description + (multiple ? " to " + attribute : juce::String());
            const auto nodes = d.nodesToRemove;
            menu.addItem (label, [this, nodes] { context.controller.remove (nodes); });
        }
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (padlock.get()));
    }

    juce::String IntegraWidget::endpointPath (const juce::String& attribute) const
    {
        auto it = spec.mapping.find (attribute);
        return it != spec.mapping.end() ? modulePath + "." + it->second : juce::String();
    }

    const EndpointInfo& IntegraWidget::info (const juce::String& attribute) const
    {
        auto it = infos.find (attribute);
        return it != infos.end() ? it->second : missingInfo;
    }

    juce::var IntegraWidget::get (const juce::String& attribute) const
    {
        auto it = values.find (attribute);
        return it != values.end() ? it->second : juce::var();
    }

    double IntegraWidget::getUnit (const juce::String& attribute) const
    {
        auto v = get (attribute);
        return info (attribute).toUnit (v.isVoid() ? 0.0 : (double) v);
    }

    void IntegraWidget::set (const juce::String& attribute, const juce::var& value)
    {
        auto path = endpointPath (attribute);
        if (isLocked (attribute))
        {
            valuesChanged();   // put the control back where the driver has it
            repaint();
            return;
        }
        if (path.isNotEmpty())
        {
            values[attribute] = value;
            context.controller.setValue (path, value);
        }
    }

    void IntegraWidget::setUnit (const juce::String& attribute, double unit)
    {
        set (attribute, info (attribute).fromUnit (unit));
    }

    void IntegraWidget::bang (const juce::String& attribute)
    {
        auto path = endpointPath (attribute);
        if (path.isNotEmpty() && ! isLocked (attribute))
            context.controller.bang (path);
    }

    juce::Rectangle<int> IntegraWidget::getControlArea() const
    {
        auto area = getLocalBounds().reduced (4);
        if (showLabel)
            area.removeFromBottom (labelHeight());
        return area;
    }

    int IntegraWidget::labelHeight() const
    {
        // two lines (name, value) when there's room, else squeeze
        return juce::jlimit (16, 30, (getHeight() - 8) * 2 / 5);
    }

    juce::String IntegraWidget::getValueText() const
    {
        auto it = spec.mapping.find ("value");
        if (it == spec.mapping.end())
            return {};
        return info ("value").format (get ("value"));
    }

    void IntegraWidget::paint (juce::Graphics& g)
    {
        if (! showLabel)
            return;

        auto& t = Theme::get();
        auto area = getLocalBounds().reduced (4);
        auto labelArea = area.removeFromBottom (labelHeight());

        g.setColour (t.text);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawFittedText (spec.label, labelArea.removeFromTop (labelArea.getHeight() / 2), juce::Justification::centred, 1);
        g.setColour (t.textDim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawFittedText (getValueText(), labelArea, juce::Justification::centred, 1);
    }

    void IntegraWidget::resized() {}

    //==============================================================================
    juce::String IntegraWidget::getEditableAttribute() const
    {
        // the value shown under the control, if it can be typed
        if (spec.mapping.count ("value") == 0)
            return {};
        auto& i = info ("value");
        return (i.isNumeric() || i.kind == EndpointInfo::Kind::text) && i.canBeTarget && ! i.isInputFile ? juce::String ("value") : juce::String();
    }

    juce::Rectangle<int> IntegraWidget::getValueTextArea() const
    {
        if (! showLabel)
            return {};
        auto labelArea = getLocalBounds().reduced (4).removeFromBottom (labelHeight());
        return labelArea.withTrimmedTop (labelArea.getHeight() / 2);
    }

    bool IntegraWidget::startEditingValue()
    {
        const auto attribute = getEditableAttribute();
        if (attribute.isEmpty() || isLocked (attribute) || getValueTextArea().isEmpty())
            return false;

        editedAttribute = attribute;
        valueEditor = std::make_unique<juce::TextEditor>();
        valueEditor->setJustification (juce::Justification::centred);
        valueEditor->setFont (juce::FontOptions (11.0f));
        valueEditor->setText (info (attribute).format (get (attribute)), false);
        valueEditor->setInputRestrictions (256);
        // (the pointer is made here: MSVC takes `this` in a nested lambda's capture as the outer lambda)
        auto finishLater = [safe = juce::Component::SafePointer<IntegraWidget> (this)] (bool save)
        {
            juce::MessageManager::callAsync ([safe, save] { if (safe) safe->finishEditingValue (save); });
        };
        valueEditor->onReturnKey = [finishLater] { finishLater (true); };
        valueEditor->onEscapeKey = [finishLater] { finishLater (false); };
        valueEditor->onFocusLost = [finishLater] { finishLater (true); };
        addAndMakeVisible (*valueEditor);
        valueEditor->setBounds (getValueTextArea().expanded (2, 1));
        valueEditor->selectAll();
        valueEditor->grabKeyboardFocus();
        repaint();
        return true;
    }

    void IntegraWidget::finishEditingValue (bool commit)
    {
        if (valueEditor == nullptr)
            return;
        auto editor = std::move (valueEditor);    // the editor goes before the value is set
        if (commit)
            setValueFromText (editedAttribute, editor->getText());
        editor.reset();
        repaint();
    }

    bool IntegraWidget::setValueFromText (const juce::String& attribute, const juce::String& rawText)
    {
        auto& i = info (attribute);
        const auto text = rawText.trim();
        if (endpointPath (attribute).isEmpty() || isLocked (attribute))
            return false;

        for (auto& [label, value] : i.stateLabels)
        {
            if (label.equalsIgnoreCase (text))
            {
                set (attribute, value);
                return true;
            }
        }

        if (! i.allowedStates.isEmpty())
        {
            for (auto& state : i.allowedStates)
                if (state.equalsIgnoreCase (text))
                {
                    set (attribute, i.kind == EndpointInfo::Kind::text ? juce::var (state)
                                  : i.kind == EndpointInfo::Kind::integer ? juce::var (state.getIntValue()) : juce::var (state.getDoubleValue()));
                    return true;
                }
            if (i.kind == EndpointInfo::Kind::text)
                return false;
        }

        if (i.kind == EndpointInfo::Kind::text)
        {
            set (attribute, text);
            return true;
        }

        if (! i.isNumeric() || ! text.containsAnyOf ("0123456789"))
            return false;

        double value = text.retainCharacters ("0123456789.-+eE").getDoubleValue();
        if (i.hasRange)
            value = juce::jlimit (i.minimum, i.maximum, value);
        if (i.kind == EndpointInfo::Kind::integer)
            set (attribute, juce::roundToInt (value));
        else
            set (attribute, value);
        return true;
    }

    void IntegraWidget::mouseDoubleClick (const juce::MouseEvent& e)
    {
        if (getValueTextArea().contains (e.getPosition()))
            startEditingValue();
    }

    void IntegraWidget::mouseDown (const juce::MouseEvent& e)
    {
        if (e.mods.isPopupMenu())
            showContextMenu();
    }

    //==============================================================================
    namespace
    {
        bool isRevertable (const EndpointInfo& i)
        {
            // ModuleProperties: stateful endpoints that can be targets
            return (i.isNumeric() || i.kind == EndpointInfo::Kind::text) && i.canBeTarget && ! i.defaultValue.isVoid();
        }

        bool isAtDefault (const EndpointInfo& i, const juce::var& value)
        {
            if (i.isNumeric())
                return std::abs ((double) value - (double) i.defaultValue) < 1e-6;
            return value.toString() == i.defaultValue.toString();
        }
    }

    bool IntegraWidget::canRevert() const
    {
        for (auto& [attribute, endpoint] : spec.mapping)
            if (isRevertable (info (attribute)) && ! isLocked (attribute) && ! isAtDefault (info (attribute), get (attribute)))
                return true;
        return false;
    }

    void IntegraWidget::revertToDefault()
    {
        context.controller.transaction ("Revert to Default", [&]
        {
            for (auto& [attribute, endpoint] : spec.mapping)
                if (isRevertable (info (attribute)) && ! isLocked (attribute))
                    context.controller.setValue (endpointPath (attribute), info (attribute).defaultValue);
        });
    }

    bool IntegraWidget::canRevertAll (AppContext& context, const juce::String& modulePath)
    {
        auto& project = context.model.get();
        for (auto& s : WidgetSpec::forModule (context.session, modulePath))
            for (auto& [attribute, endpoint] : s.mapping)
            {
                const auto path = modulePath + "." + endpoint;
                auto i = EndpointInfo::find (context.session, path);
                if (isRevertable (i) && project.driversOf (path).empty() && ! isAtDefault (i, context.session.getValue (path)))
                    return true;
            }
        return false;
    }

    void IntegraWidget::revertAllToDefault (AppContext& context, const juce::String& modulePath)
    {
        // ModuleProperties.revertAll: every unlocked control of the module
        auto& project = context.model.get();
        context.controller.transaction ("Revert All to Default", [&]
        {
            juce::StringArray done;
            for (auto& s : WidgetSpec::forModule (context.session, modulePath))
                for (auto& [attribute, endpoint] : s.mapping)
                {
                    const auto path = modulePath + "." + endpoint;
                    auto i = EndpointInfo::find (context.session, path);
                    if (done.contains (path) || ! isRevertable (i) || ! project.driversOf (path).empty())
                        continue;
                    done.add (path);
                    context.controller.setValue (path, i.defaultValue);
                }
        });
    }

    void IntegraWidget::showContextMenu()
    {
        juce::PopupMenu revert;
        juce::StringArray labels;
        bool anyRevertable = false;
        for (auto& [attribute, endpoint] : spec.mapping)
            if (isRevertable (info (attribute)) && ! isLocked (attribute))
            {
                anyRevertable = true;
                labels.add (info (attribute).label.isNotEmpty() ? info (attribute).label : endpoint);
            }
        if (anyRevertable)
        {
            revert.addItem (labels.joinIntoString (", "), canRevert(), false, [safe = juce::Component::SafePointer<IntegraWidget> (this)]
            {
                if (safe != nullptr)
                    safe->revertToDefault();
            });
            revert.addSeparator();
        }
        revert.addItem ("All Controls", canRevertAll (context, modulePath), false, [&ctx = context, path = modulePath]
        {
            revertAllToDefault (ctx, path);
        });

        juce::PopupMenu menu;
        menu.addSubMenu ("Revert to default", revert);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

    //==============================================================================
    namespace
    {
        /** A child control that leaves right-clicks to its widget's context menu. */
        template <typename Base>
        class Guarded  : public Base
        {
        public:
            template <typename... Args>
            explicit Guarded (IntegraWidget& w, Args&&... args) : Base (std::forward<Args> (args)...), widget (w) {}

            void mouseDown (const juce::MouseEvent& e) override
            {
                ignoring = e.mods.isPopupMenu();
                if (ignoring)
                    widget.showContextMenu();
                else
                    Base::mouseDown (e);
            }
            void mouseDrag (const juce::MouseEvent& e) override         { if (! ignoring) Base::mouseDrag (e); }
            void mouseUp (const juce::MouseEvent& e) override           { if (! ignoring) Base::mouseUp (e); }
            void mouseDoubleClick (const juce::MouseEvent& e) override  { if (! ignoring) Base::mouseDoubleClick (e); }

        private:
            IntegraWidget& widget;
            bool ignoring = false;
        };

        /** Slider/Knob/NumberBox: a juce::Slider working in 0..1 control units. */
        class SliderWidget  : public IntegraWidget
        {
        public:
            enum class Style { automatic, knob, numberBox };

            SliderWidget (AppContext& c, const juce::String& p, const WidgetSpec& s, Style style)
                : IntegraWidget (c, p, s)
            {
                const bool vertical = s.position.getHeight() >= s.position.getWidth();
                switch (style)
                {
                    case Style::knob:       slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag); break;
                    case Style::numberBox:  slider.setSliderStyle (juce::Slider::LinearBar); break;
                    case Style::automatic:  slider.setSliderStyle (vertical ? juce::Slider::LinearVertical : juce::Slider::LinearHorizontal); break;
                }

                slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
                slider.setRange (0.0, 1.0);
                slider.setVelocityModeParameters (0.6, 1, 0.0, true, juce::ModifierKeys::shiftModifier);
                slider.onValueChange = [this] { if (! updating) setUnit ("value", slider.getValue()); };
                slider.textFromValueFunction = [this] (double unit) { return info ("value").format (info ("value").fromUnit (unit)); };
                slider.valueFromTextFunction = [this] (const juce::String& text) { return info ("value").toUnit (text.getDoubleValue()); };

                if (style == Style::numberBox)
                {
                    // drag vertically to change; the value is shown in the label strip
                    slider.setSliderSnapsToMousePosition (false);
                    slider.setSliderStyle (juce::Slider::LinearBar);
                }

                addAndMakeVisible (slider);
                valuesChanged();
            }

            void valuesChanged() override
            {
                const juce::ScopedValueSetter<bool> svs (updating, true);
                slider.setValue (getUnit ("value"), juce::dontSendNotification);
            }

            void resized() override
            {
                auto area = getControlArea();
                if (slider.getSliderStyle() == juce::Slider::LinearBar)
                    area = area.withSizeKeepingCentre (area.getWidth(), juce::jmin (area.getHeight(), 28));
                slider.setBounds (area);
            }

        private:
            Guarded<juce::Slider> slider { *this };
            bool updating = false;
        };

        //==========================================================================
        class CheckboxWidget  : public IntegraWidget
        {
        public:
            CheckboxWidget (AppContext& c, const juce::String& p, const WidgetSpec& s) : IntegraWidget (c, p, s) {}

            void valuesChanged() override {}

            void paint (juce::Graphics& g) override
            {
                IntegraWidget::paint (g);
                auto& t = Theme::get();
                auto box = getControlArea().toFloat().withSizeKeepingCentre (26.0f, 26.0f);
                g.setColour (t.controlBackground);
                g.fillRoundedRectangle (box, 4.0f);
                g.setColour (t.outline);
                g.drawRoundedRectangle (box, 4.0f, 1.0f);

                if (isOn())
                {
                    juce::Path tick;
                    tick.startNewSubPath (box.getX() + 6, box.getCentreY());
                    tick.lineTo (box.getCentreX() - 1, box.getBottom() - 7);
                    tick.lineTo (box.getRight() - 5, box.getY() + 6);
                    g.setColour (t.accent);
                    g.strokePath (tick, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                }
            }

            void mouseDown (const juce::MouseEvent& e) override
            {
                if (e.mods.isPopupMenu() || ! getControlArea().contains (e.getPosition()))
                    return IntegraWidget::mouseDown (e);
                auto& i = info ("value");
                set ("value", isOn() ? (i.hasRange ? i.minimum : 0.0) : (i.hasRange ? i.maximum : 1.0));
            }

            juce::String getValueText() const override  { return isOn() ? "on" : "off"; }

        private:
            bool isOn() const
            {
                auto& i = info ("value");
                const double v = get ("value");
                return v > (i.hasRange ? (i.minimum + i.maximum) * 0.5 : 0.5);
            }
        };

        //==========================================================================
        /** Trigger (bang), Play, Stop and Record buttons. */
        class ButtonWidget  : public IntegraWidget
        {
        public:
            enum class Kind { trigger, play, stop, record };

            ButtonWidget (AppContext& c, const juce::String& p, const WidgetSpec& s, Kind k) : IntegraWidget (c, p, s), kind (k) {}

            void valuesChanged() override {}

            void paint (juce::Graphics& g) override
            {
                IntegraWidget::paint (g);
                auto& t = Theme::get();
                auto box = getControlArea().toFloat().withSizeKeepingCentre (34.0f, 34.0f);
                const bool lit = flash || isOn();

                g.setColour (lit ? (kind == Kind::record ? t.meterHigh : t.accent) : t.controlBackground);
                g.fillEllipse (box);
                g.setColour (t.outline);
                g.drawEllipse (box, 1.0f);

                g.setColour (lit ? t.accentText : t.text);
                auto icon = box.reduced (10.0f);
                juce::Path p;
                switch (kind)
                {
                    case Kind::play:    p.addTriangle (icon.getX() + 2, icon.getY(), icon.getX() + 2, icon.getBottom(), icon.getRight() + 1, icon.getCentreY()); break;
                    case Kind::stop:    p.addRectangle (icon); break;
                    case Kind::record:  p.addEllipse (icon); break;
                    case Kind::trigger: p.addEllipse (icon.reduced (3.0f)); break;
                }
                g.fillPath (p);
            }

            void mouseDown (const juce::MouseEvent& e) override
            {
                if (e.mods.isPopupMenu() || ! getControlArea().contains (e.getPosition()))
                    return IntegraWidget::mouseDown (e);
                if (kind == Kind::trigger || kind == Kind::stop || info ("value").kind == EndpointInfo::Kind::bang)
                {
                    bang ("value");
                    flash = true;
                    repaint();
                    juce::Timer::callAfterDelay (150, [safe = juce::Component::SafePointer<ButtonWidget> (this)]
                    {
                        if (safe != nullptr) { safe->flash = false; safe->repaint(); }
                    });
                }
                else
                {
                    set ("value", isOn() ? 0 : 1);
                }
            }

            juce::String getValueText() const override
            {
                if (kind == Kind::trigger || kind == Kind::stop)
                    return {};
                return isOn() ? "on" : "off";
            }

        private:
            bool isOn() const   { return info ("value").kind != EndpointInfo::Kind::bang && (double) get ("value") > 0.5; }

            Kind kind;
            bool flash = false;
        };

        //==========================================================================
        class RadioGroupWidget  : public IntegraWidget
        {
        public:
            RadioGroupWidget (AppContext& c, const juce::String& p, const WidgetSpec& s) : IntegraWidget (c, p, s)
            {
                auto& i = info ("value");
                if (! i.allowedStates.isEmpty())
                {
                    for (auto& state : i.allowedStates)
                        options.push_back ({ state, i.format (i.isNumeric() ? juce::var (state.getDoubleValue()) : juce::var (state)) });
                }
                else if (i.hasRange && i.kind == EndpointInfo::Kind::integer && i.maximum - i.minimum <= 16)
                {
                    for (int v = (int) i.minimum; v <= (int) i.maximum; ++v)
                        options.push_back ({ juce::String (v), i.format (v) });
                }
            }

            void valuesChanged() override {}

            void paint (juce::Graphics& g) override
            {
                IntegraWidget::paint (g);
                auto& t = Theme::get();
                auto area = getControlArea();
                const int rowHeight = options.empty() ? 20 : juce::jmin (22, area.getHeight() / (int) options.size());
                const auto current = get ("value").toString();

                for (size_t i = 0; i < options.size(); ++i)
                {
                    auto row = area.removeFromTop (rowHeight);
                    auto dot = row.removeFromLeft (rowHeight).toFloat().reduced (4.0f);
                    const bool selected = sameValue (options[i].first, current);
                    g.setColour (t.controlBackground);
                    g.fillEllipse (dot);
                    g.setColour (t.outline);
                    g.drawEllipse (dot, 1.0f);
                    if (selected)
                    {
                        g.setColour (t.accent);
                        g.fillEllipse (dot.reduced (3.0f));
                    }
                    g.setColour (t.text);
                    g.setFont (juce::FontOptions (12.0f));
                    g.drawFittedText (options[i].second, row.withTrimmedLeft (2), juce::Justification::centredLeft, 1);
                }
            }

            void mouseDown (const juce::MouseEvent& e) override
            {
                auto area = getControlArea();
                if (e.mods.isPopupMenu() || options.empty() || ! area.contains (e.getPosition()))
                    return IntegraWidget::mouseDown (e);
                const int rowHeight = juce::jmin (22, area.getHeight() / (int) options.size());
                const int index = (e.y - area.getY()) / juce::jmax (1, rowHeight);
                if (index >= 0 && index < (int) options.size())
                {
                    auto& i = info ("value");
                    set ("value", i.isNumeric() ? juce::var (options[(size_t) index].first.getDoubleValue()) : juce::var (options[(size_t) index].first));
                }
            }

            juce::String getValueText() const override { return {}; }

        private:
            static bool sameValue (const juce::String& a, const juce::String& b)
            {
                return a == b || (a.containsOnly ("0123456789.-") && b.containsOnly ("0123456789.-")
                                  && std::abs (a.getDoubleValue() - b.getDoubleValue()) < 1.0e-6);
            }

            std::vector<std::pair<juce::String, juce::String>> options;   // value, label
        };

        //==========================================================================
        class RangeSliderWidget  : public IntegraWidget
        {
        public:
            RangeSliderWidget (AppContext& c, const juce::String& p, const WidgetSpec& s) : IntegraWidget (c, p, s)
            {
                slider.setSliderStyle (juce::Slider::TwoValueHorizontal);
                slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
                slider.setRange (0.0, 1.0);
                slider.onValueChange = [this]
                {
                    if (updating)
                        return;
                    setUnit ("min", slider.getMinValue());
                    setUnit ("max", slider.getMaxValue());
                };
                addAndMakeVisible (slider);
                valuesChanged();
            }

            void valuesChanged() override
            {
                const juce::ScopedValueSetter<bool> svs (updating, true);
                slider.setMinAndMaxValues (getUnit ("min"), juce::jmax (getUnit ("min"), getUnit ("max")), juce::dontSendNotification);
            }

            void resized() override
            {
                slider.setBounds (getControlArea().withSizeKeepingCentre (getControlArea().getWidth(), 30));
            }

            juce::String getValueText() const override
            {
                return info ("min").format (get ("min")) + " - " + info ("max").format (get ("max"));
            }

        private:
            Guarded<juce::Slider> slider { *this };
            bool updating = false;
        };

        //==========================================================================
        class VuMeterWidget  : public IntegraWidget
        {
        public:
            VuMeterWidget (AppContext& c, const juce::String& p, const WidgetSpec& s) : IntegraWidget (c, p, s) {}

            void valuesChanged() override {}

            void paint (juce::Graphics& g) override
            {
                IntegraWidget::paint (g);
                auto& t = Theme::get();
                auto area = getControlArea().toFloat();
                auto bar = area.withSizeKeepingCentre (juce::jmin (18.0f, area.getWidth()), area.getHeight());
                g.setColour (t.controlBackground);
                g.fillRect (bar);

                const float level = (float) getUnit ("level");
                auto filled = bar.withTop (bar.getBottom() - bar.getHeight() * level);
                g.setGradientFill (juce::ColourGradient (t.meterHigh, bar.getX(), bar.getY(), t.meterLow, bar.getX(), bar.getBottom(), false));
                g.fillRect (filled);
                g.setColour (t.outline);
                g.drawRect (bar);
            }

            juce::String getValueText() const override  { return info ("level").format (get ("level")); }
        };

        //==========================================================================
        class FileDialogWidget  : public IntegraWidget
        {
        public:
            /** SoundFileLoadDialog, FileLoadDialog, FileSaveDialog, LocateFolderDialog (Flex controls/*DialogControl.as) */
            enum class Mode { loadSound, load, save, folder };

            FileDialogWidget (AppContext& c, const juce::String& p, const WidgetSpec& s, Mode m)
                : IntegraWidget (c, p, s), mode (m)
            {
                button.setButtonText (mode == Mode::save ? "Save..." : mode == Mode::folder ? "Choose folder..." : "Load...");
                button.onClick = [this] { choose(); };
                addAndMakeVisible (button);
            }

            void valuesChanged() override {}

            void resized() override
            {
                button.setBounds (getControlArea().withSizeKeepingCentre (juce::jmin (100, getControlArea().getWidth()), 26));
            }

            juce::String getValueText() const override
            {
                auto path = get ("value").toString();
                return path.isEmpty() ? juce::String ("(none)") : juce::File::createFileWithoutCheckingPath (path).getFileName();
            }

        private:
            void choose()
            {
                const juce::String patterns = mode == Mode::loadSound ? "*.wav;*.aif;*.aiff" : "*";
                auto start = juce::File::createFileWithoutCheckingPath (get ("value").toString());
                if (! start.exists())
                    start = juce::File::getSpecialLocation (mode == Mode::loadSound ? juce::File::userMusicDirectory : juce::File::userDocumentsDirectory);
                chooser = std::make_unique<juce::FileChooser> (spec.label, start, patterns);
                int chooserFlags = mode == Mode::save ? (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting
                                                         | juce::FileBrowserComponent::canSelectFiles)
                                 : mode == Mode::folder ? (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories)
                                 : (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles);
                chooser->launchAsync (chooserFlags, [this] (const juce::FileChooser& fc)
                {
                    auto f = fc.getResult();
                    if (f != juce::File())
                        set ("value", f.getFullPathName());
                });
            }

            Mode mode;
            Guarded<juce::TextButton> button { *this };
            std::unique_ptr<juce::FileChooser> chooser;
        };

        //==========================================================================
        /** XYPanPad / XYScratchPad: two endpoints on a 2D surface. */
        class XYPadWidget  : public IntegraWidget
        {
        public:
            XYPadWidget (AppContext& c, const juce::String& p, const WidgetSpec& s, bool panner) : IntegraWidget (c, p, s), isPanner (panner) {}

            void valuesChanged() override {}

            void paint (juce::Graphics& g) override
            {
                IntegraWidget::paint (g);
                auto& t = Theme::get();
                auto area = padArea().toFloat();
                g.setColour (t.controlBackground);
                g.fillRect (area);
                g.setColour (t.gridLine);
                g.drawLine (area.getCentreX(), area.getY(), area.getCentreX(), area.getBottom());
                g.drawLine (area.getX(), area.getCentreY(), area.getRight(), area.getCentreY());
                if (isPanner)
                    g.drawEllipse (area.reduced (area.getWidth() * 0.15f), 1.0f);
                g.setColour (t.outline);
                g.drawRect (area);

                const float x = area.getX() + (float) getUnit ("x") * area.getWidth();
                const float y = area.getBottom() - (float) getUnit ("y") * area.getHeight();
                g.setColour (t.accent);
                g.fillEllipse (x - 7, y - 7, 14, 14);
            }

            void mouseDown (const juce::MouseEvent& e) override
            {
                popup = e.mods.isPopupMenu() || ! getControlArea().contains (e.getPosition());
                if (popup)
                    return IntegraWidget::mouseDown (e);
                update (e);
            }
            void mouseDrag (const juce::MouseEvent& e) override  { if (! popup) update (e); }
            bool popup = false;

            juce::String getValueText() const override
            {
                return info ("x").format (get ("x")) + ", " + info ("y").format (get ("y"));
            }

        private:
            juce::Rectangle<int> padArea() const
            {
                auto area = getControlArea();
                const int side = juce::jmin (area.getWidth(), area.getHeight());
                return area.withSizeKeepingCentre (side, side);
            }

            void update (const juce::MouseEvent& e)
            {
                auto area = padArea().toFloat();
                setUnit ("x", juce::jlimit (0.0, 1.0, (double) ((e.position.x - area.getX()) / area.getWidth())));
                setUnit ("y", juce::jlimit (0.0, 1.0, (double) ((area.getBottom() - e.position.y) / area.getHeight())));
                repaint();
            }

            bool isPanner;
        };

        //==========================================================================
        /** DryWetBalance / EarlyLateBalance / PanoramaBalance: a labelled crossfader. */
        class BalanceWidget  : public IntegraWidget
        {
        public:
            BalanceWidget (AppContext& c, const juce::String& p, const WidgetSpec& s, juce::String leftText, juce::String rightText)
                : IntegraWidget (c, p, s), left (leftText), right (rightText)
            {
                slider.setSliderStyle (juce::Slider::LinearHorizontal);
                slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
                slider.setRange (0.0, 1.0);
                slider.onValueChange = [this] { if (! updating) setUnit ("value", slider.getValue()); };
                addAndMakeVisible (slider);
                valuesChanged();
            }

            void valuesChanged() override
            {
                const juce::ScopedValueSetter<bool> svs (updating, true);
                slider.setValue (getUnit ("value"), juce::dontSendNotification);
            }

            void paint (juce::Graphics& g) override
            {
                IntegraWidget::paint (g);
                auto& t = Theme::get();
                auto area = getControlArea();
                auto labels = area.removeFromTop (18);
                g.setColour (t.textDim);
                g.setFont (juce::FontOptions (12.0f));
                g.drawText (left, labels, juce::Justification::centredLeft);
                g.drawText (right, labels, juce::Justification::centredRight);
            }

            void resized() override
            {
                auto area = getControlArea();
                area.removeFromTop (18);
                slider.setBounds (area.withSizeKeepingCentre (area.getWidth(), juce::jmin (36, area.getHeight())));
            }

        private:
            juce::String left, right;
            Guarded<juce::Slider> slider { *this };
            bool updating = false;
        };
    }

    //==============================================================================
    std::unique_ptr<IntegraWidget> IntegraWidget::create (AppContext& c, const juce::String& p, const WidgetSpec& s)
    {
        const auto& type = s.type;

        if (type == "Slider")               return std::make_unique<SliderWidget> (c, p, s, SliderWidget::Style::automatic);
        if (type == "Knob")                 return std::make_unique<SliderWidget> (c, p, s, SliderWidget::Style::knob);
        if (type == "NumberBox")            return std::make_unique<SliderWidget> (c, p, s, SliderWidget::Style::numberBox);
        if (type == "Checkbox")             return std::make_unique<CheckboxWidget> (c, p, s);
        if (type == "Trigger")              return std::make_unique<ButtonWidget> (c, p, s, ButtonWidget::Kind::trigger);
        if (type == "PlayButton")           return std::make_unique<ButtonWidget> (c, p, s, ButtonWidget::Kind::play);
        if (type == "StopButton")           return std::make_unique<ButtonWidget> (c, p, s, ButtonWidget::Kind::stop);
        if (type == "RecButton")            return std::make_unique<ButtonWidget> (c, p, s, ButtonWidget::Kind::record);
        if (type == "RadioGroup")           return std::make_unique<RadioGroupWidget> (c, p, s);
        if (type == "RangeSlider")          return std::make_unique<RangeSliderWidget> (c, p, s);
        if (type == "VuMeter")              return std::make_unique<VuMeterWidget> (c, p, s);
        if (type == "SoundFileLoadDialog")  return std::make_unique<FileDialogWidget> (c, p, s, FileDialogWidget::Mode::loadSound);
        if (type == "FileLoadDialog")       return std::make_unique<FileDialogWidget> (c, p, s, FileDialogWidget::Mode::load);
        if (type == "FileSaveDialog")       return std::make_unique<FileDialogWidget> (c, p, s, FileDialogWidget::Mode::save);
        if (type == "LocateFolderDialog")   return std::make_unique<FileDialogWidget> (c, p, s, FileDialogWidget::Mode::folder);
        if (type == "XYPanPad")             return std::make_unique<XYPadWidget> (c, p, s, true);
        if (type == "XYScratchPad")         return std::make_unique<XYPadWidget> (c, p, s, false);
        if (type == "DryWetBalance")        return std::make_unique<BalanceWidget> (c, p, s, "dry", "wet");
        if (type == "EarlyLateBalance")     return std::make_unique<BalanceWidget> (c, p, s, "early", "late");
        if (type == "PanoramaBalance")      return std::make_unique<BalanceWidget> (c, p, s, "left", "right");

        // unknown widget types (3rd party modules) fall back to a slider if numeric
        if (s.mapping.count ("value") != 0)
            return std::make_unique<SliderWidget> (c, p, s, SliderWidget::Style::automatic);
        return {};
    }

    juce::Point<int> IntegraWidget::minimumSize (const juce::String& type)
    {
        if (type == "Slider" || type == "VuMeter")                          return { 50, 120 };
        if (type == "Knob")                                                 return { 70, 90 };
        if (type.endsWith ("Balance") || type.startsWith ("XY"))            return { 140, 140 };
        if (type == "RadioGroup")                                           return { 100, 100 };
        return { 60, 70 };
    }
}
