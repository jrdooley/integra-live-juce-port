#pragma once

#include <optional>

#include "ModuleProperties.h"
#include "ContainerControls.h"

namespace integra
{
    /**
        The module library (Flex: views/ModuleLibrary/). Lists every module with audio
        endpoints, one line per module (its best version), sorted in-development first,
        then shipped, 3rd party and embedded, then by label. Modules with several loaded
        versions have an expander showing all of them under source headings.

        Filtering: the search box matches labels; the tag cloud underneath, sorted by how
        many modules use each tag, filters on all selected tags. Using one clears the other.
        Double-click, Enter or drag onto the graph adds the module; Left/Right expand.
    */
    class ModuleLibrary  : public juce::Component,
                           private juce::ListBoxModel,
                           private juce::KeyListener
    {
    public:
        explicit ModuleLibrary (AppContext&);
        ~ModuleLibrary() override;

        void refresh();
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;

        /** Called when a module is double-clicked (adds it to the current block). */
        std::function<void (const ModuleCatalogue::Entry&)> onModuleChosen;

        static constexpr const char* dragPrefix = "integra-module:";

        struct Row
        {
            enum class Kind { module, sourceLabel, version } kind = Kind::module;
            const ModuleCatalogue::Entry* entry = nullptr;      // null for source labels
            IInterfaceDefinition::module_source source = IInterfaceDefinition::MODULE_SHIPPED_WITH_INTEGRA;
            int numVersions = 1;                                // module rows: shown versions
            bool expanded = false;
        };

        const std::vector<Row>& getRows() const noexcept        { return rows; }
        void setSearchText (const juce::String&);               // clears the selected tags
        void toggleTag (const juce::String&);                   // clears the search text
        void clearTags();
        juce::StringArray getTags() const;                      // in tag cloud order
        juce::StringArray getSelectedTags() const               { return selectedTags; }
        void setExpanded (const GUID& originGuid, bool);
        void selectRow (int row);
        int getSelectedRow() const                              { return list.getSelectedRow(); }
        void chooseSelected();
        juce::ListBox& getList() noexcept                       { return list; }
        /** Left/Right on the list: collapse or expand the selected module's versions. */
        bool handleKey (const juce::KeyPress&);

        /** The Flex info text: the description for the default line of a shipped module,
            otherwise label, source, (modified date and author for version lines) and description. */
        static juce::String getInfoMarkdown (const ModuleCatalogue::Entry&, bool isDefaultEntry);
        static juce::String getSourceHeading (IInterfaceDefinition::module_source);   // "LATEST OFFICIAL"...
        static juce::Colour getSourceTint (IInterfaceDefinition::module_source);

    private:
        class TagCloud;

        int getNumRows() override;
        void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
        void listBoxItemClicked (int row, const juce::MouseEvent&) override;
        void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
        void returnKeyPressed (int row) override;
        void selectedRowsChanged (int lastRowSelected) override;
        juce::var getDragSourceDescription (const juce::SparseSet<int>&) override;
        juce::String getTooltipForRow (int row) override;
        bool keyPressed (const juce::KeyPress&, juce::Component*) override;
        juce::Rectangle<int> expanderBounds (int width, int height) const;

        AppContext& context;
        juce::TextEditor search;
        juce::TextButton clearSearch { "x" };
        juce::ListBox list { "modules", this };
        std::unique_ptr<TagCloud> tagCloud;
        juce::StringArray selectedTags;
        std::vector<GUID> expandedOrigins;
        std::vector<Row> rows;
        int lastSelectedRow = -1;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModuleLibrary)
    };

    //==============================================================================
    /** The block's signal graph: modules and audio connections (Flex: ModuleGraph.as). */
    class ModuleGraph  : public juce::Component,
                         public juce::DragAndDropTarget,
                         private juce::ChangeListener
    {
    public:
        explicit ModuleGraph (AppContext&);
        ~ModuleGraph() override;

        void setBlock (const juce::String& blockPath);
        const juce::String& getBlock() const noexcept   { return blockPath; }

        /** Re-reads the block from the model, keeping the selection. */
        void refreshLayout()                            { layout(); }

        std::function<void (const juce::String& modulePath)> onModuleSelected;

        static constexpr float gridSize = 18.0f;    // pixels per Flex grid unit (one text row)

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        bool keyPressed (const juce::KeyPress&) override;

        bool isInterestedInDragSource (const SourceDetails&) override;
        void itemDropped (const SourceDetails&) override;

        void deleteSelection();

        /** Pixel position of an audio pin (endpoint path), for tests. */
        std::optional<juce::Point<float>> getPinPosition (const juce::String& endpointPath) const
        {
            for (auto& m : modules)
                for (auto& p : m.pins)
                    if (p.endpoint == endpointPath)
                        return p.position;
            return std::nullopt;
        }

        std::optional<juce::Rectangle<float>> getModuleBounds (const juce::String& modulePath) const
        {
            for (auto& m : modules)
                if (m.path == modulePath)
                    return m.bounds;
            return std::nullopt;
        }

    private:
        struct Pin
        {
            juce::String endpoint;      // absolute endpoint path
            juce::Point<float> position;
            bool isInput;
        };

        struct ModuleBox
        {
            juce::String path, name, label;
            juce::Rectangle<float> bounds;  // pixels
            std::vector<Pin> pins;
        };

        struct Link
        {
            juce::String connectionPath;
            juce::String sourceEndpoint, targetEndpoint;
            juce::Point<float> start, end;
        };

        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void layout();
        const Pin* pinAt (juce::Point<float>) const;
        ModuleBox* moduleAt (juce::Point<float>);
        const Link* linkAt (juce::Point<float>) const;
        static juce::Path linkPath (juce::Point<float> start, juce::Point<float> end);
        void showModuleMenu (const ModuleBox&);
        void selectModule (const juce::String& path);
        void updateSize();
        void updateLinkEnds();

        AppContext& context;
        juce::String blockPath;
        std::vector<ModuleBox> modules;
        std::vector<Link> links;

        juce::String selectedModule, selectedLink;
        juce::String draggingModule;
        juce::Point<float> dragOffset;
        juce::Rectangle<float> dragOriginal;
        std::optional<Pin> dragPin;     // a copy: `modules` is rebuilt whenever the model changes
        juce::Point<float> dragPinEnd;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModuleGraph)
    };

    //==============================================================================
    /** The block editor: library | graph over properties (Flex: the "Module View"). */
    class ModuleView  : public juce::Component,
                        public juce::DragAndDropContainer
    {
    public:
        explicit ModuleView (AppContext&);

        void setBlock (const juce::String& blockPath);
        const juce::String& getBlock() const noexcept       { return graph.getBlock(); }

        std::function<void()> onBack;

        void paint (juce::Graphics&) override;
        void resized() override;

        ModuleGraph& getGraph() noexcept                    { return graph; }
        ModuleProperties& getProperties() noexcept          { return properties; }
        ModuleLibrary& getLibrary() noexcept                { return library; }
        ActiveButton& getBlockActiveButton() noexcept       { return blockActive; }

    private:
        AppContext& context;
        juce::TextButton backButton { "< Arrange" };
        juce::Label breadcrumb;
        ActiveButton blockActive { context };   // Flex: the module graph's title bar
        ContainerMeter blockMeter { context };
        ModuleLibrary library;
        ModuleGraph graph;
        juce::Viewport graphViewport;
        ModuleProperties properties;
        juce::StretchableLayoutManager verticalLayout;
        std::unique_ptr<juce::StretchableLayoutResizerBar> divider;
    };
}
