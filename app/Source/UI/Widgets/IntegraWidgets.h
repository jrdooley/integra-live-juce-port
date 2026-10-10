#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <set>
#include "Core/AppContext.h"
#include "Core/EndpointInfo.h"

namespace integra
{
    /** One widget from a module's interface definition (IID <WidgetInfo>). */
    struct WidgetSpec
    {
        juce::String type;                          // "Slider", "Knob", ... (18 types)
        juce::String label;                         // unique within the module; used as live-view key
        juce::Rectangle<float> position;            // design-time pixels in the module properties panel
        std::map<juce::String, juce::String> mapping;   // widget attribute ("value", "min", "max", "x", "y", "level") -> endpoint name

        static std::vector<WidgetSpec> forModule (IntegraSession&, const juce::String& modulePath);
    };

    /**
        Base class of all module control widgets. Binds each widget attribute to an
        endpoint of one module node, follows value changes through the EndpointWatcher,
        and writes user edits through ProjectController (so they're undoable).

        The JUCE port of the Flex controlSDK (components/controls/ in the Flex GUI).
    */
    class IntegraWidget  : public juce::Component,
                           public juce::SettableTooltipClient,
                           private juce::ChangeListener,
                           private juce::ComponentListener
    {
    public:
        IntegraWidget (AppContext&, const juce::String& modulePath, const WidgetSpec&);
        ~IntegraWidget() override;

        const WidgetSpec& getSpec() const noexcept          { return spec; }
        const juce::String& getModulePath() const noexcept  { return modulePath; }

        /** Path of the endpoint mapped to an attribute ("" if unmapped). */
        juce::String endpointPath (const juce::String& attribute) const;
        const EndpointInfo& info (const juce::String& attribute) const;

        /** Label + value text under the control; true by default. */
        void setShowLabel (bool shouldShow)                 { showLabel = shouldShow; resized(); repaint(); }

        void paint (juce::Graphics&) override;
        void resized() override;

        static std::unique_ptr<IntegraWidget> create (AppContext&, const juce::String& modulePath, const WidgetSpec&);

        /** Smallest sensible size for this widget type. */
        static juce::Point<int> minimumSize (const juce::String& type);

        /** True when an envelope, MIDI or a routing drives the attribute's endpoint; the user
            can't change it then (Flex: ControlContainer padlock). */
        bool isLocked (const juce::String& attribute) const   { return lockedAttributes.count (attribute) > 0; }
        bool isFullyLocked() const;
        /** Shows the padlock menu (delete the envelope / routing). */
        void showPadlockMenu();

    protected:
        juce::var get (const juce::String& attribute) const;
        double getUnit (const juce::String& attribute) const;
        void set (const juce::String& attribute, const juce::var& value);
        void setUnit (const juce::String& attribute, double unit);
        void bang (const juce::String& attribute);

        /** Called when a mapped endpoint changes (and once after construction). */
        virtual void valuesChanged() = 0;
        /** Area for the control itself (everything except the label strip). */
        juce::Rectangle<int> getControlArea() const;
        int labelHeight() const;
        virtual juce::String getValueText() const;

        AppContext& context;
        const juce::String modulePath;
        const WidgetSpec spec;

    private:
        class Padlock;
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void componentMovedOrResized (juce::Component&, bool, bool) override;
        void updateLocks();
        void layoutLock();

        std::map<juce::String, EndpointInfo> infos;
        std::set<juce::String> lockedAttributes;
        std::vector<std::pair<juce::String, model::Driver>> drivers;   // attribute, driver
        std::unique_ptr<juce::Component> padlock, lockOverlay;
        std::map<juce::String, juce::var> values;
        bool showLabel = true;
        static const EndpointInfo missingInfo;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IntegraWidget)
    };
}
