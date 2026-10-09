#include "ModuleProperties.h"
#include "IntegraLookAndFeel.h"

namespace integra
{
    namespace
    {
        /** Tiny toggle drawn as a labelled pill. */
        class StripButton  : public juce::Button
        {
        public:
            StripButton (const juce::String& text, const juce::String& tip) : juce::Button (text)
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
                auto text = getWidth() < 28 ? getButtonText().substring (0, 1) : getButtonText();
                g.drawText (text, area, juce::Justification::centred);
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

            bool hasMidi = false;
            if (block != nullptr)
                for (auto& s : block->scalers)
                    if (s.targetPath == endpoint && s.sourcePath.endsWith (".value"))
                        hasMidi = true;
            midi.setToggleState (hasMidi, juce::dontSendNotification);
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

        void learnMidi()
        {
            const auto blockPath = parentPath (widget->getModulePath());
            const auto endpoint = primaryEndpoint();

            if (midi.getToggleState())
            {
                // remove the MIDI routing(s) to this endpoint
                juce::StringArray toRemove;
                if (auto* block = context.model.get().findBlock (blockPath))
                    for (auto& s : block->scalers)
                        if (s.targetPath == endpoint && s.sourcePath.endsWith (".value"))
                            toRemove.add (parentPath (s.sourcePath)), toRemove.add (s.path);
                context.controller.remove (toRemove);
            }
            else
            {
                context.controller.addMidiControl (blockPath, endpoint, true);
            }
            refresh();
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

    void ModuleProperties::resized()
    {
        viewport.setBounds (getLocalBounds().withTrimmedTop (40));
    }
}
