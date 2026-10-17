#pragma once

#include "Common.h"

namespace modulecreator
{
    /**
        The editor for one endpoint (Endpoint.mxml with ControlInfo, StateInfo, Range,
        AllowedValues, Scale, StateLabels, StreamInfo and WidgetSelection).
    */
    class EndpointEditor  : public juce::Component
    {
    public:
        explicit EndpointEditor (Document&);

        void setEndpoint (int uid);
        int getEndpoint() const                     { return uid; }
        void refresh();
        int layout (int width);                     // returns the height needed
        void resized() override                     { layout (getWidth()); }

        juce::TextEditor& getNameEditor()           { return name; }
        juce::TextButton& getWidgetButton()         { return widgetButton; }
        void showWidgetMenu();

    private:
        Document& document;
        int uid = 0;
        bool showAdvanced = false;

        FormLabel nameLabel { "Endpoint Name" }, labelLabel { "Endpoint Label" }, descriptionLabel { "Endpoint Description" },
                  typeLabel { "Endpoint Type" }, directionLabel { "Stream Direction" }, controlTypeLabel { "Control Type" },
                  stateTypeLabel { "State Type" }, constraintLabel { "Constraint" }, minimumLabel { "Minimum Value" },
                  maximumLabel { "Maximum Value" }, allowedLabel { "Allowed Values" }, defaultLabel { "Default Value" },
                  scaleLabel { "Scale" }, stateLabelsLabel { "State Labels" }, widgetLabel { "Widget" }, widgetNameLabel { "Widget Label" },
                  savedLabel { "Is Saved To File" }, inputFileLabel { "Is Input File" }, sourceLabel { "Can Be Source" },
                  targetLabel { "Can Be Target" }, sentToHostLabel { "Is Sent To Host" };
        juce::TextEditor name, label, description, minimum, maximum, defaultValue, widgetName;
        juce::ComboBox type, direction, controlType, stateType, constraint, scale;
        RowsEditor allowedValues { { "value" }, "Add Value" };
        RowsEditor stateLabels { { "value", "label" }, "Add State Label" };
        juce::TextButton widgetButton;
        juce::ToggleButton advancedButton { "show advanced controls" };
        juce::ToggleButton saved, inputFile, source, target, sentToHost;
        IssueList issues;

        Endpoint* endpoint()                        { return document.definition.findEndpoint (uid); }
        void edit (const std::function<void (Endpoint&)>&);
        void updateRestrictions (const Endpoint&);
    };

    /** The "Endpoints" tab (EndpointList.mxml): the list of endpoints and the selected one's editor. */
    class EndpointsPage  : public Page, private juce::ListBoxModel
    {
    public:
        explicit EndpointsPage (Document&);

        void refresh() override;
        void resized() override;

        void selectEndpoint (int index);
        int getSelectedIndex() const                { return list.getSelectedRow(); }
        EndpointEditor& getEditor()                 { return editor; }
        juce::TextButton& getAddButton()            { return addButton; }

    private:
        juce::Label endpointsHeading { {}, "Endpoints" }, withoutWidgetsHeading { {}, "Controls without widgets" };
        juce::ListBox list { "endpoints", this };
        juce::TextButton addButton { "Add" }, removeButton { "Remove" }, upButton { "Up" }, downButton { "Down" }, sortButton { "Sort" };
        juce::ListBox withoutWidgets { "controls without widgets" };
        IssueList listIssues;
        EndpointEditor editor;
        juce::Viewport viewport;

        struct WithoutWidgetsModel  : public juce::ListBoxModel
        {
            EndpointsPage& owner;
            juce::StringArray names;
            explicit WithoutWidgetsModel (EndpointsPage& o) : owner (o) {}
            int getNumRows() override       { return names.size(); }
            void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
            void listBoxItemClicked (int row, const juce::MouseEvent&) override;
        } withoutWidgetsModel { *this };

        int getNumRows() override;
        void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
        void selectedRowsChanged (int lastRowSelected) override;

        void updateButtons();
        void moveSelected (int delta);
    };
}
