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

        /** Flex AttributeLabel: double-click the value under a control to type a value;
            Enter (or clicking elsewhere) sets it, Escape cancels. */
        bool startEditingValue (juce::String attribute = {});
        juce::TextEditor* getValueEditor() const noexcept   { return valueEditor.get(); }
        void finishEditingValue (bool commit);
        /** Sets an attribute from typed text: numbers are clamped to the endpoint's range,
            enumerations match their names or labels. False if the text didn't fit. */
        bool setValueFromText (const juce::String& attribute, const juce::String& text);
        /** The attribute whose value text can be typed into ("" for none). */
        virtual juce::String getEditableAttribute() const;
        /** Every attribute with its own value text (AttributeLabel), left to right: x and y of
            an XY pad, min and max of a range slider, else the editable attribute. */
        virtual juce::StringArray getEditableAttributes() const;
        juce::Rectangle<int> getValueTextArea() const;
        /** Where one attribute's value is typed: its share of the value text. */
        virtual juce::Rectangle<int> getValueTextArea (const juce::String& attribute) const;
        const juce::String& getEditedAttribute() const noexcept   { return editedAttribute; }
        /** ControlManager: a value goes to the nearest allowed value, an integer is rounded. */
        static juce::var quantise (const EndpointInfo&, const juce::var&);

        /** Right-click: Revert to default for this control or all of the module's controls
            (Flex: ModuleProperties context menu). */
        void showContextMenu();
        bool canRevert() const;
        void revertToDefault();
        static bool canRevertAll (AppContext&, const juce::String& modulePath);
        static void revertAllToDefault (AppContext&, const juce::String& modulePath);

        void mouseDown (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;

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
        std::unique_ptr<juce::TextEditor> valueEditor;
        juce::String editedAttribute;
        std::map<juce::String, juce::var> values;
        bool showLabel = true;
        static const EndpointInfo missingInfo;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IntegraWidget)
    };
}
