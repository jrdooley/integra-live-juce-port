#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <vector>

namespace modulecreator
{
    struct CreatorConfig;

    /** The three state types of a control endpoint (Globals.floatType / intType / stringType). */
    enum class StateType { floatType, integerType, stringType };

    juce::String toDisplayName (StateType);         // "Float", "Integer", "String"
    juce::String toXmlName (StateType);             // "float", "integer", "string"

    struct StateLabel
    {
        juce::String value, text;
        bool operator== (const StateLabel& o) const   { return value == o.value && text == o.text; }
    };

    /**
        One endpoint of the interface (Flex: Endpoint + ControlInfo + StateInfo + Range +
        AllowedValues + Scale + StateLabels + StreamInfo, which kept their data in the controls).
        Fields that do not apply to the current kind are kept, so switching kind and back
        loses nothing until the file is saved.
    */
    struct Endpoint
    {
        int uid = 0;                                // stable identity for widget mappings
        juce::String name, label, description;
        bool isControl = true;

        // control
        bool isState = true;                        // false: bang
        StateType stateType = StateType::floatType;
        bool isSavedToFile = true, isInputFile = false;
        bool useRange = true;                       // false: allowed values
        juce::String minimum { "0" }, maximum { "1" };
        juce::StringArray allowedStates;
        juce::String defaultValue { "0" };
        juce::String scale { "linear" };            // linear / exponential / decibel
        std::vector<StateLabel> stateLabels;
        bool canBeSource = true, canBeTarget = true, isSentToHost = true;

        // stream (audio is the only stream type)
        bool isInputStream = true;

        bool isAudioStream (bool input) const       { return ! isControl && isInputStream == input; }
        /** Scale only applies to float ranges (StateInfo.updateScaleControl). */
        bool hasScale() const                       { return isControl && isState && stateType == StateType::floatType && useRange; }
    };

    /** A widget in the module's layout and the endpoints its attributes drive. */
    struct Widget
    {
        int uid = 0;
        juce::String type;                          // a CreatorConfig widget name
        juce::String ownLabel;                      // used when more than one attribute is mapped
        juce::Rectangle<int> bounds;
        std::vector<std::pair<juce::String, int>> mappings;     // widget attribute -> endpoint uid

        int findAttribute (const juce::String& attribute) const;
        int endpointFor (const juce::String& attribute) const;  // 0 if unmapped
        juce::String attributeFor (int endpointUid) const;      // empty if unmapped
    };

    /** A problem that stops the module from being saved (Flex: a control's errorString). */
    struct ValidationIssue
    {
        enum class Page { interfaceInfo, endpoints, widgetLayout, implementation };
        Page page;
        int endpointIndex;                          // -1 unless page == endpoints
        juce::String message;
    };

    /**
        The interface definition of a module (interface_definition.iid in a .module file):
        what the Module Creator edits.
    */
    struct ModuleDefinition
    {
        juce::String moduleGuid, originGuid;

        juce::String name, label, description, author;
        juce::StringArray tags;
        bool implementedInLibIntegra = false;
        juce::Time createdDate, modifiedDate;

        std::vector<Endpoint> endpoints;
        std::vector<Widget> widgets;

        juce::String patchName;                     // main patch in the implementation; empty if none

        //==============================================================================
        /** A fresh interface with new ids and today's dates (MainComponent.newFile). */
        static ModuleDefinition createNew();

        /** Parses an interface definition; fails with the Flex error texts. */
        static juce::Result fromXml (const juce::XmlElement&, const CreatorConfig&, ModuleDefinition& result);
        std::unique_ptr<juce::XmlElement> toXml() const;
        juce::String toXmlString() const;           // with the <?xml version="1.0" ?> header of the Flex version

        /** Every problem with the interface; developer mode allows the libIntegra-only settings. */
        std::vector<ValidationIssue> validate (bool developerMode) const;

        //==============================================================================
        Endpoint* findEndpoint (int uid);
        const Endpoint* findEndpoint (int uid) const;
        const Endpoint* findEndpointNamed (const juce::String& endpointName) const;
        int indexOfEndpoint (int uid) const;
        Endpoint& addEndpoint();                    // "<New Endpoint #n>" style placeholder name
        void removeEndpoint (int uid);              // and its widget mappings

        Widget* widgetFor (int endpointUid);
        const Widget* widgetFor (int endpointUid) const;
        juce::String labelOf (const Widget&) const; // the endpoint name when one attribute is mapped

        /** Maps an endpoint to a widget attribute, on a new widget or shared with an existing one
            (WidgetLayout.assignEndpoint); an empty type removes the endpoint's widget mapping. */
        void assignWidget (int endpointUid, const juce::String& widgetType, const juce::String& attribute,
                           int widgetToShareUid, juce::Point<int> defaultSize);
        bool unassignWidget (int endpointUid);

        /** Streams first, then controls in the left-to-right order of their widgets (EndpointList.onClickSort). */
        void sortEndpoints();
        /** Lines widgets up left to right (WidgetLayout.onAutoArrange). */
        void autoArrangeWidgets (int margin);

        juce::StringArray controlsWithoutWidgets() const;

        void regenerateModuleGuid();
        void touch();                               // updates the modification date

        int nextUid = 1;
        int newEndpointNumber = 0;
    };

    //==============================================================================
    juce::String createGuid();
    juce::String dateToString (juce::Time);         // "2016-02-10 16:34:35.572000"
    juce::Time stringToDate (const juce::String&);

    /** Number parsing as the Flex version did it (AS3 Number(), but empty is not a number). */
    bool parseNumber (const juce::String&, double& result);
    bool doTypesMatch (const juce::String& value, StateType);
    /** Lowercase letters, numbers and spaces (TagEditor.isTagValid). */
    bool isTagValid (const juce::String&);

    inline const juce::StringArray reservedEndpointNames { "init", "fini", "ping" };
    inline const juce::String dataDirectoryName { "dataDirectory" };
    inline const juce::String coreTag { "core" };
    constexpr int maximumNumberOfTags = 20;
    constexpr int maximumNameLength = 32;
}
