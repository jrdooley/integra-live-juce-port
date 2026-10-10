#include "ModuleProperties.h"
#include "InlineRename.h"
#include "IntegraLookAndFeel.h"

namespace integra
{
    namespace
    {
        /** Tiny toggle drawn as a labelled pill. */
        class StripButton  : public juce::Button
        {
        public:
            StripButton (const juce::String& name, const juce::String& tip) : juce::Button (name)
            {
                setTooltip (tip);
                setClickingTogglesState (false);
            }

            void paintButton (juce::Graphics& g, bool over, bool down) override
            {
                auto& t = Theme::get();
                auto area = getLocalBounds().toFloat().reduced (1.0f);
                g.setColour (getToggleState() ? t.accent : (over || down ? t.panelAlt.contrasting (0.1f) : t.panelAlt));
                g.fillRoundedRectangle (area, 3.0f);
                g.setColour (getToggleState() ? t.accentText : t.textDim);
                g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
                // narrow widgets only have room for the initial
                auto caption = getWidth() < 28 ? getButtonText().substring (0, 1) : getButtonText();
                g.drawText (caption, area, juce::Justification::centred);
            }
        };
    }

    //==============================================================================
    class ModuleProperties::WidgetFrame  : public juce::Component
    {
    public:
        WidgetFrame (AppContext& c, const juce::String& modulePath, const WidgetSpec& spec)
            : context (c),
              live ("LIVE", "Show this control in the Live view"),
              midi ("MIDI", "MIDI learn: move a MIDI controller to control this"),
              envelope ("ENV", "Automate this control with an envelope in the block")
        {
            widget = IntegraWidget::create (c, modulePath, spec);
            if (widget != nullptr)
                addAndMakeVisible (*widget);

            addAndMakeVisible (live);
            addAndMakeVisible (midi);
            addAndMakeVisible (envelope);

            live.onClick = [this] { toggleLive(); };
            midi.onClick = [this] { learnMidi(); };
            envelope.onClick = [this] { toggleEnvelope(); };

            const auto mainEndpoint = primaryEndpoint();
            const auto epInfo = mainEndpoint.isNotEmpty() ? EndpointInfo::find (context.session, mainEndpoint) : EndpointInfo();
            midi.setVisible (epInfo.canBeTarget);
            envelope.setVisible (epInfo.isNumeric() && epInfo.canBeTarget);

            refresh();
        }

        void refresh()
        {
            if (widget == nullptr)
                return;

            auto& model = context.model.get();
            const auto blockPath = parentPath (widget->getModulePath());
            const auto moduleName = lastElement (widget->getModulePath());
            auto* block = model.findBlock (blockPath);

            live.setToggleState (block != nullptr && block->userData.findLiveViewControl (moduleName, widget->getSpec().label) != nullptr,
                                 juce::dontSendNotification);

            const auto endpoint = primaryEndpoint();
            bool hasEnvelope = block != nullptr && block->findEnvelopeForTarget (endpoint) != nullptr;
            envelope.setToggleState (hasEnvelope, juce::dontSendNotification);

            midi.setToggleState (! learnedEndpoints().isEmpty(), juce::dontSendNotification);
        }

        void resized() override
        {
            auto area = getLocalBounds();
            auto strip = area.removeFromTop (16).reduced (2, 0);
            const int w = juce::jmin (32, strip.getWidth() / 3);
            live.setBounds (strip.removeFromLeft (w));
            envelope.setBounds (strip.removeFromRight (w));
            midi.setBounds (strip.removeFromRight (w));
            if (widget != nullptr)
                widget->setBounds (area);
        }

        void paint (juce::Graphics& g) override
        {
            auto& t = Theme::get();
            g.setColour (t.panel);
            g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 5.0f);
            g.setColour (t.outline.withAlpha (0.6f));
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 5.0f, 1.0f);
        }

    private:
        juce::String primaryEndpoint() const
        {
            if (widget == nullptr)
                return {};
            for (auto attribute : { "value", "x", "min", "level" })
                if (auto p = widget->endpointPath (attribute); p.isNotEmpty())
                    return p;
            return {};
        }

        void toggleLive()
        {
            auto& spec = widget->getSpec();
            const auto blockPath = parentPath (widget->getModulePath());
            const bool show = ! live.getToggleState();

            juce::Rectangle<float> bounds;
            if (show)
            {
                // place below the existing live controls of this block
                float bottom = 0;
                if (auto* block = context.model.get().findBlock (blockPath))
                    for (auto& c : block->userData.liveViewControls)
                        bottom = juce::jmax (bottom, c.bounds.getBottom());
                bounds = { 10.0f, bottom + 10.0f, spec.position.getWidth(), spec.position.getHeight() };
            }

            context.controller.setLiveViewControl (widget->getModulePath(), spec.label, show, bounds);
            refresh();
        }

        /** Endpoints of this control that MIDI can drive (ControlContainer: unlocked ones). */
        juce::StringArray learnableEndpoints() const
        {
            juce::StringArray result;
            for (auto attribute : { "value", "x", "y", "min", "max" })
                if (auto p = widget->endpointPath (attribute); p.isNotEmpty() && ! widget->isLocked (attribute)
                     && EndpointInfo::find (context.session, p).canBeTarget)
                    result.addIfNotAlreadyThere (p);
            return result;
        }

        juce::StringArray learnedEndpoints() const
        {
            juce::StringArray result;
            if (auto* block = context.model.get().findBlock (parentPath (widget->getModulePath())))
                for (auto attribute : { "value", "x", "y", "min", "max", "level" })
                    if (auto p = widget->endpointPath (attribute); p.isNotEmpty())
                        for (auto& s : block->scalers)
                            if (s.targetPath == p && s.sourcePath.endsWith (".value"))
                                result.addIfNotAlreadyThere (p);
            return result;
        }

        void learnMidi()
        {
            // ControlContainer.onClickMidiLearnButton: learning again removes it; with several
            // endpoints a menu asks which one to learn
            const auto blockPath = parentPath (widget->getModulePath());
            if (midi.getToggleState())
            {
                juce::StringArray toRemove;
                if (auto* block = context.model.get().findBlock (blockPath))
                    for (auto& s : block->scalers)
                        if (learnedEndpoints().contains (s.targetPath) && s.sourcePath.endsWith (".value"))
                            toRemove.add (parentPath (s.sourcePath)), toRemove.add (s.path);
                context.controller.remove (toRemove);
                refresh();
                return;
            }

            const auto endpoints = learnableEndpoints();
            if (endpoints.size() == 1)
            {
                context.controller.addMidiControl (blockPath, endpoints[0], true);
                refresh();
            }
            else if (endpoints.size() > 1)
            {
                juce::PopupMenu menu;
                for (auto& endpoint : endpoints)
                {
                    auto i = EndpointInfo::find (context.session, endpoint);
                    menu.addItem (i.label.isNotEmpty() ? i.label : lastElement (endpoint),
                                  [this, blockPath, endpoint] { context.controller.addMidiControl (blockPath, endpoint, true); refresh(); });
                }
                menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&midi));
            }
        }

        void toggleEnvelope()
        {
            const auto blockPath = parentPath (widget->getModulePath());
            const auto endpoint = primaryEndpoint();
            auto* block = context.model.get().findBlock (blockPath);
            if (block == nullptr)
                return;

            if (auto* env = block->findEnvelopeForTarget (endpoint))
                context.controller.remove (env->path);
            else
                context.controller.addEnvelope (blockPath, endpoint);
            refresh();
        }

        AppContext& context;
        std::unique_ptr<IntegraWidget> widget;
        StripButton live, midi, envelope;
    };

    //==============================================================================
    ModuleProperties::ModuleProperties (AppContext& c) : context (c)
    {
        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (true, true);
        addAndMakeVisible (viewport);
        context.model.addChangeListener (this);
    }

    ModuleProperties::~ModuleProperties()
    {
        context.model.removeChangeListener (this);
    }

    void ModuleProperties::setModule (const juce::String& path)
    {
        if (path == modulePath && ! frames.isEmpty())
            return;

        modulePath = path;
        rebuild();
    }

    void ModuleProperties::rebuild()
    {
        frames.clear();
        title = {};
        description = {};

        if (modulePath.isNotEmpty() && context.session.nodeExists (modulePath))
        {
            auto* block = context.model.get().findBlock (parentPath (modulePath));
            const model::Module* module = block != nullptr ? block->findModule (lastElement (modulePath)) : nullptr;
            if (module != nullptr && module->entry != nullptr)
            {
                title = lastElement (modulePath) + "  (" + module->entry->label + ")";
                description = module->entry->description;
            }

            int right = 0, bottom = 0;
            for (auto& spec : WidgetSpec::forModule (context.session, modulePath))
            {
                // Keep the IID geometry exactly: module designers tiled widgets edge to edge,
                // so growing one would overlap its neighbours.
                auto* frame = frames.add (new WidgetFrame (context, modulePath, spec));
                auto bounds = spec.position.toNearestInt().translated (8, 8);
                frame->setBounds (bounds);
                content.addAndMakeVisible (frame);
                right = juce::jmax (right, bounds.getRight());
                bottom = juce::jmax (bottom, bounds.getBottom());
            }
            content.setSize (right + 8, bottom + 8);
        }
        else
        {
            content.setSize (0, 0);
        }

        resized();
        repaint();
    }

    void ModuleProperties::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        if (modulePath.isNotEmpty() && ! context.session.nodeExists (modulePath))
        {
            modulePath = {};
            rebuild();
            return;
        }

        for (auto* f : frames)
            f->refresh();
    }

    void ModuleProperties::paint (juce::Graphics& g)
    {
        auto& t = Theme::get();
        g.fillAll (t.panelAlt);

        auto header = getLocalBounds().removeFromTop (40).reduced (8, 2);
        if (modulePath.isEmpty())
        {
            g.setColour (t.textDim);
            g.setFont (juce::FontOptions (14.0f));
            g.drawText ("Select a module to see its controls", getLocalBounds(), juce::Justification::centred);
            return;
        }

        g.setColour (t.text);
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText (title, header.removeFromTop (20), juce::Justification::centredLeft);
        g.setColour (t.textDim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (description, header, juce::Justification::centredLeft, true);
    }

    Info ModuleProperties::getInfoAt (juce::Point<int> p)
    {
        // ControlContainer.getInfoToDisplay: the strip's buttons, else the control's endpoint
        for (auto* c = getComponentAt (p); c != nullptr && c != this; c = c->getParentComponent())
        {
            if (auto* button = dynamic_cast<juce::Button*> (c))
            {
                if (button->getName() == "LIVE")  return InfoTexts::forView ("Widget/LiveViewButton");
                if (button->getName() == "MIDI")  return InfoTexts::forView ("Widget/MidiLearnButton");
            }
            if (auto* widget = dynamic_cast<IntegraWidget*> (c))
            {
                const auto& spec = widget->getSpec();
                juce::String markdown;
                for (auto& [attribute, endpoint] : spec.mapping)
                {
                    auto& i = widget->info (attribute);
                    markdown << "**" << (i.label.isNotEmpty() ? i.label : endpoint) << "**";
                    if (i.hasRange && i.isNumeric())
                        markdown << " (" << i.format (i.minimum) << " to " << i.format (i.maximum) << ")";
                    markdown << "\n\n" << i.description << "\n\n";
                }
                return { spec.label, markdown.trim(), {} };
            }
        }
        return InfoTexts::forView ("ModuleProperties");
    }

    void ModuleProperties::mouseDoubleClick (const juce::MouseEvent& e)
    {
        if (e.y < 24 && modulePath.isNotEmpty())
            renameModule();
    }

    void ModuleProperties::renameModule()
    {
        const auto path = modulePath;
        if (path.isEmpty())
            return;
        startInlineRename (renameEditor, *this, { 6, 2, juce::jmin (getWidth() - 12, 240), 22 }, lastElement (path),
                           [this, path] (const juce::String& name)
                           {
                               if (context.controller.rename (path, name).wasOk() && onRenamed != nullptr)
                                   onRenamed (parentPath (path) + "." + name);
                           });
    }

    void ModuleProperties::resized()
    {
        viewport.setBounds (getLocalBounds().withTrimmedTop (40));
    }
}
