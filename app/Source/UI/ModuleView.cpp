#include "ModuleView.h"
#include "TrackColourPicker.h"
#include "IntegraLookAndFeel.h"
#include "InlineRename.h"
#include "Widgets/IntegraWidgets.h"

namespace integra
{
    //==============================================================================
    /** Flex TagCloud: toggle buttons for every tag, laid out like words in a paragraph. */
    class ModuleLibrary::TagCloud  : public juce::Component
    {
    public:
        explicit TagCloud (ModuleLibrary& l) : library (l) {}

        void setTags (const juce::StringArray& t)
        {
            tags = t;
            layout();
        }

        int getIdealHeight (int width)
        {
            setSize (width, getHeight());
            layout();
            return idealHeight;
        }

        void paint (juce::Graphics& g) override
        {
            auto& t = Theme::get();
            g.setFont (juce::FontOptions (12.0f));
            for (size_t i = 0; i < boxes.size(); ++i)
            {
                const bool on = library.selectedTags.contains (tags[(int) i]);
                auto r = boxes[i].toFloat();
                g.setColour (on ? t.accent : t.controlBackground);
                g.fillRoundedRectangle (r, 3.0f);
                g.setColour (on ? t.accentText : t.textDim);
                g.drawText (tags[(int) i], boxes[i], juce::Justification::centred, false);
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            for (size_t i = 0; i < boxes.size(); ++i)
                if (boxes[i].contains (e.getPosition()))
                    return library.toggleTag (tags[(int) i]);
        }

        void resized() override     { layout(); }

    private:
        void layout()
        {
            // TagCloud.positionTagButtons
            constexpr int edgeMargin = 2, innerMargin = 2, rowHeight = 18;
            boxes.clear();
            const juce::Font font (juce::FontOptions (12.0f));
            int x = edgeMargin, y = edgeMargin;
            for (auto& tag : tags)
            {
                const int w = juce::GlyphArrangement::getStringWidthInt (font, tag) + 10;
                if (x + w > getWidth() - edgeMargin && x > edgeMargin)
                {
                    x = edgeMargin;
                    y += rowHeight + innerMargin;
                }
                boxes.push_back ({ x, y, w, rowHeight });
                x += w + innerMargin;
            }
            idealHeight = boxes.empty() ? 0 : boxes.back().getBottom() + edgeMargin;
        }

        ModuleLibrary& library;
        juce::StringArray tags;
        std::vector<juce::Rectangle<int>> boxes;
        int idealHeight = 0;
    };

    //==============================================================================
    namespace
    {
        bool hasAudioEndpoints (const ModuleCatalogue::Entry& e)
        {
            return e.numAudioInputs > 0 || e.numAudioOutputs > 0;
        }

        /** ModuleLibraryListEntry.compare: in development, shipped, 3rd party, embedded. */
        int libraryPriority (IInterfaceDefinition::module_source source)
        {
            switch (source)
            {
                case IInterfaceDefinition::MODULE_IN_DEVELOPMENT:        return 3;
                case IInterfaceDefinition::MODULE_SHIPPED_WITH_INTEGRA:  return 2;
                case IInterfaceDefinition::MODULE_3RD_PARTY:             return 1;
                case IInterfaceDefinition::MODULE_EMBEDDED:
                default:                                                 return 0;
            }
        }
    }

    ModuleLibrary::ModuleLibrary (AppContext& c) : context (c)
    {
        search.setTextToShowWhenEmpty ("Search modules", Theme::get().textDim);
        search.onTextChange = [this]
        {
            // ModuleLibrary.onSearchChange: searching clears the tag selection
            if (search.getText().isNotEmpty())
                selectedTags.clear();
            refresh();
        };
        search.onEscapeKey = [this] { setSearchText ({}); };
        addAndMakeVisible (search);

        clearSearch.setTooltip ("Clear the search");
        clearSearch.onClick = [this] { setSearchText ({}); };
        addChildComponent (clearSearch);

        list.setRowHeight (22);
        list.setMultipleSelectionEnabled (false);
        list.addKeyListener (this);
        addAndMakeVisible (list);

        tagCloud = std::make_unique<TagCloud> (*this);
        addAndMakeVisible (*tagCloud);
        refresh();
    }

    ModuleLibrary::~ModuleLibrary()
    {
        list.removeKeyListener (this);
    }

    juce::String ModuleLibrary::getSourceHeading (IInterfaceDefinition::module_source source)
    {
        // ModuleLibraryListLabel
        switch (source)
        {
            case IInterfaceDefinition::MODULE_SHIPPED_WITH_INTEGRA:  return "LATEST OFFICIAL";
            case IInterfaceDefinition::MODULE_3RD_PARTY:             return "THIRD PARTY";
            case IInterfaceDefinition::MODULE_EMBEDDED:              return "EMBEDDED";
            case IInterfaceDefinition::MODULE_IN_DEVELOPMENT:        return "IN DEVELOPMENT";
            default:                                                 return "UNKNOWN";
        }
    }

    juce::Colour ModuleLibrary::getSourceTint (IInterfaceDefinition::module_source source)
    {
        // ModuleLibraryListEntry tints: blue for 3rd party, purple embedded, red in development
        switch (source)
        {
            case IInterfaceDefinition::MODULE_3RD_PARTY:        return juce::Colour (0xff3060c0).withAlpha (0.18f);
            case IInterfaceDefinition::MODULE_EMBEDDED:         return juce::Colour (0xffa040a0).withAlpha (0.18f);
            case IInterfaceDefinition::MODULE_IN_DEVELOPMENT:   return juce::Colour (0xffd03030).withAlpha (0.22f);
            default:                                            return juce::Colours::transparentBlack;
        }
    }

    juce::String ModuleLibrary::getInfoMarkdown (const ModuleCatalogue::Entry& e, bool isDefaultEntry)
    {
        // ModuleLibraryListEntry.makeInfo / InterfaceDefinition.makeExtendedInfoMarkdown
        if (isDefaultEntry && e.source == IInterfaceDefinition::MODULE_SHIPPED_WITH_INTEGRA)
            return e.description;

        juce::String markdown = "#" + e.label + "\n##" + e.getSourceLabel() + "\n\n";
        if (! isDefaultEntry)
        {
            markdown << "**Last modified:** " << (e.modified.toMilliseconds() > 0 ? e.modified.toString (true, false) : juce::String ("<unknown>")) << "\n\n";
            markdown << "**Author:** " << (e.author.isEmpty() ? juce::String ("<unknown>") : e.author) << "\n\n";
        }
        return markdown + e.description;
    }

    juce::StringArray ModuleLibrary::getTags() const
    {
        // ModuleLibrary.updateTagList: tags of modules with audio endpoints, most used first
        std::map<juce::String, int> uses;
        for (auto& e : context.session.getCatalogue().getEntries())
            if (hasAudioEndpoints (e))
                for (auto& tag : e.tags)
                    if (tag.isNotEmpty())
                        ++uses[tag];

        std::vector<std::pair<juce::String, int>> sorted (uses.begin(), uses.end());
        std::stable_sort (sorted.begin(), sorted.end(), [] (auto& a, auto& b)
        {
            return a.second != b.second ? a.second > b.second : a.first.compare (b.first) < 0;
        });

        juce::StringArray tags;
        for (auto& [tag, count] : sorted)
            tags.add (tag);
        return tags;
    }

    void ModuleLibrary::setSearchText (const juce::String& text)
    {
        search.setText (text, false);
        if (text.isNotEmpty())
            selectedTags.clear();
        refresh();
    }

    void ModuleLibrary::toggleTag (const juce::String& tag)
    {
        // TagCloud.onClickTagButton; ModuleLibrary.onTagSelectionChanged clears the search
        if (selectedTags.contains (tag))
            selectedTags.removeString (tag);
        else
            selectedTags.add (tag);
        search.setText ({}, false);
        refresh();
    }

    void ModuleLibrary::clearTags()
    {
        selectedTags.clear();
        refresh();
    }

    void ModuleLibrary::setExpanded (const GUID& origin, bool shouldExpand)
    {
        auto it = std::find_if (expandedOrigins.begin(), expandedOrigins.end(), [&] (const GUID& g) { return guidsEqual (g, origin); });
        if (shouldExpand && it == expandedOrigins.end())
            expandedOrigins.push_back (origin);
        else if (! shouldExpand && it != expandedOrigins.end())
            expandedOrigins.erase (it);
        else
            return;

        // keep the selection on the same module line
        const ModuleCatalogue::Entry* selected = nullptr;
        if (auto row = list.getSelectedRow(); row >= 0 && row < (int) rows.size())
            selected = rows[(size_t) row].entry;
        refresh();
        for (size_t i = 0; i < rows.size(); ++i)
            if (rows[i].kind == Row::Kind::module && rows[i].entry != nullptr && guidsEqual (rows[i].entry->originGuid, origin))
                if (selected == nullptr || ! shouldExpand || rows[i].entry == selected)
                    { list.selectRow ((int) i); break; }
    }

    void ModuleLibrary::refresh()
    {
        auto& catalogue = context.session.getCatalogue();
        const auto text = search.getText().trim();

        // prune tags that no longer exist (e.g. after uninstalling a module)
        const auto tags = getTags();
        for (int i = selectedTags.size(); --i >= 0;)
            if (! tags.contains (selectedTags[i]))
                selectedTags.remove (i);

        auto include = [&] (const ModuleCatalogue::Entry& e)
        {
            if (! hasAudioEndpoints (e))
                return false;
            if (text.isNotEmpty() && ! e.label.containsIgnoreCase (text))
                return false;
            for (auto& tag : selectedTags)
                if (! e.tags.contains (tag))
                    return false;
            return true;
        };

        // ModuleLibrary.updateLibrary: one group per origin, its first version as default
        std::vector<std::vector<const ModuleCatalogue::Entry*>> groups;
        std::vector<GUID> seen;
        for (auto& e : catalogue.getEntries())
        {
            if (! hasAudioEndpoints (e))
                continue;
            if (std::any_of (seen.begin(), seen.end(), [&] (const GUID& g) { return guidsEqual (g, e.originGuid); }))
                continue;
            seen.push_back (e.originGuid);

            std::vector<const ModuleCatalogue::Entry*> group;
            for (auto* v : catalogue.getVersions (e.originGuid))
                if (include (*v))
                    group.push_back (v);
            if (! group.empty())
                groups.push_back (std::move (group));
        }

        std::stable_sort (groups.begin(), groups.end(), [] (auto& ga, auto& gb)
        {
            auto* a = ga.front();
            auto* b = gb.front();
            if (libraryPriority (a->source) != libraryPriority (b->source))
                return libraryPriority (a->source) > libraryPriority (b->source);
            const auto la = a->label.toUpperCase(), lb = b->label.toUpperCase();
            if (la != lb)
                return la < lb;
            return a->modified > b->modified;
        });

        const ModuleCatalogue::Entry* selected = nullptr;
        if (auto row = list.getSelectedRow(); row >= 0 && row < (int) rows.size())
            selected = rows[(size_t) row].entry;

        rows.clear();
        for (auto& group : groups)
        {
            Row top;
            top.entry = group.front();
            top.source = top.entry->source;
            top.numVersions = (int) group.size();
            top.expanded = group.size() > 1
                           && std::any_of (expandedOrigins.begin(), expandedOrigins.end(), [&] (const GUID& g) { return guidsEqual (g, top.entry->originGuid); });
            rows.push_back (top);

            if (top.expanded)
            {
                // ModuleLibrary.insertLabels: a heading before each run of one source
                bool first = true;
                IInterfaceDefinition::module_source previous {};
                for (auto* v : group)
                {
                    if (first || v->source != previous)
                    {
                        Row label;
                        label.kind = Row::Kind::sourceLabel;
                        label.source = v->source;
                        rows.push_back (label);
                    }
                    first = false;
                    previous = v->source;

                    Row version;
                    version.kind = Row::Kind::version;
                    version.entry = v;
                    version.source = v->source;
                    rows.push_back (version);
                }
            }
        }

        tagCloud->setTags (tags);
        clearSearch.setVisible (search.getText().isNotEmpty());
        list.updateContent();

        int newSelection = -1;
        for (size_t i = 0; i < rows.size() && selected != nullptr; ++i)
            if (rows[i].entry == selected)
                { newSelection = (int) i; break; }
        if (newSelection >= 0)
            list.selectRow (newSelection, true, true);
        else
            list.deselectAllRows();

        list.repaint();
        resized();
        repaint();
    }

    void ModuleLibrary::paint (juce::Graphics& g)
    {
        auto& t = Theme::get();
        g.fillAll (t.panelAlt);

        // "Tags..." header with the clear button (TagCloud)
        auto header = juce::Rectangle<int> (4, tagCloud->getY() - 20, getWidth() - 8, 18);
        g.setColour (t.textDim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (selectedTags.isEmpty() ? "Tags..." : "Tags: " + selectedTags.joinIntoString (" + "),
                    header.withTrimmedRight (20), juce::Justification::centredLeft, true);
        if (! selectedTags.isEmpty())
        {
            auto x = header.removeFromRight (14).withSizeKeepingCentre (8, 8).toFloat();
            g.drawLine ({ x.getTopLeft(), x.getBottomRight() }, 1.5f);
            g.drawLine ({ x.getTopRight(), x.getBottomLeft() }, 1.5f);
        }

        // ModuleLibrary.updateSearchBox: show when the filters leave nothing
        if (rows.empty())
        {
            g.setColour (t.textDim);
            g.setFont (juce::FontOptions (12.0f, juce::Font::italic));
            g.drawFittedText ("No modules match the search and tags", list.getBounds().reduced (8).removeFromTop (40),
                              juce::Justification::centred, 2);
        }
    }

    void ModuleLibrary::mouseDown (const juce::MouseEvent& e)
    {
        auto header = juce::Rectangle<int> (4, tagCloud->getY() - 20, getWidth() - 8, 18);
        if (! selectedTags.isEmpty() && header.removeFromRight (18).contains (e.getPosition()))
            clearTags();
        dragStartHeight = getTagCloudHeight();
    }

    juce::Rectangle<int> ModuleLibrary::getTagResizeArea() const
    {
        auto header = juce::Rectangle<int> (4, tagCloud->getY() - 22, getWidth() - 8, 22);
        if (! selectedTags.isEmpty())
            header.removeFromRight (18);     // the clear button
        return header;
    }

    int ModuleLibrary::getTagCloudHeight() const    { return tagCloud->getHeight(); }

    void ModuleLibrary::mouseMove (const juce::MouseEvent& e)
    {
        setMouseCursor (getTagResizeArea().contains (e.getPosition()) ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
    }

    void ModuleLibrary::mouseDrag (const juce::MouseEvent& e)
    {
        // TagCloud.onMouseMove (dragging its title bar)
        if (! getTagResizeArea().contains (e.getMouseDownPosition()))
            return;
        const int maximum = juce::jmax (0, juce::jmin (tagCloud->getIdealHeight (tagCloud->getWidth()), getHeight() / 2));
        tagCloudHeight = juce::jlimit (0, maximum, dragStartHeight - e.getDistanceFromDragStartY());
        resized();
        repaint();
    }

    void ModuleLibrary::resized()
    {
        auto area = getLocalBounds().reduced (4);
        auto searchRow = area.removeFromTop (26);
        if (clearSearch.isVisible())
            clearSearch.setBounds (searchRow.removeFromRight (24).reduced (1));
        search.setBounds (searchRow);
        area.removeFromTop (4);

        // the tag cloud's dragged height, at most its tags' height and half the library
        // (TagCloud: maxHeight = libraryHeight / 2)
        const int cloudHeight = juce::jlimit (0, juce::jmax (0, juce::jmin (tagCloud->getIdealHeight (area.getWidth()), getHeight() / 2)), tagCloudHeight);
        tagCloud->setBounds (area.removeFromBottom (cloudHeight));
        area.removeFromBottom (22);
        list.setBounds (area);
    }

    juce::Rectangle<int> ModuleLibrary::expanderBounds (int width, int height) const
    {
        return { width - 22, 0, 22, height };
    }

    int ModuleLibrary::getNumRows()     { return (int) rows.size(); }

    void ModuleLibrary::paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height, bool selected)
    {
        if (rowNumber < 0 || rowNumber >= (int) rows.size())
            return;

        auto& t = Theme::get();
        auto& row = rows[(size_t) rowNumber];

        if (row.kind == Row::Kind::sourceLabel)
        {
            g.setColour (t.textDim);
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText (getSourceHeading (row.source), 18, 0, width - 20, height, juce::Justification::bottomLeft, true);
            return;
        }

        g.fillAll (getSourceTint (row.source));
        if (selected)
            g.fillAll (t.selection);

        auto* e = row.entry;
        const int indent = row.kind == Row::Kind::version ? 24 : 8;
        const bool hasExpander = row.kind == Row::Kind::module && row.numVersions > 1;
        const int right = hasExpander ? 22 : 4;

        juce::String text = e->label;
        if (row.kind == Row::Kind::version)
            text << "  (" << (e->modified.toMilliseconds() > 0 ? e->modified.toString (true, false) : e->getSourceLabel()) << ")";

        g.setColour (t.text);
        g.setFont (juce::FontOptions (row.kind == Row::Kind::version ? 12.0f : 13.0f));
        g.drawText (text, indent, 0, width - indent - right - 40, height, juce::Justification::centredLeft, true);

        g.setColour (t.textDim);
        g.setFont (juce::FontOptions (11.0f));
        juce::String pins;
        pins << e->numAudioInputs << " > " << e->numAudioOutputs;
        g.drawText (pins, width - right - 40, 0, 38, height, juce::Justification::centredRight);

        if (hasExpander)
        {
            // "Show Alternative Versions"
            auto r = expanderBounds (width, height).toFloat().withSizeKeepingCentre (8.0f, 8.0f);
            juce::Path p;
            if (row.expanded)
                p.addTriangle (r.getTopLeft(), r.getTopRight(), { r.getCentreX(), r.getBottom() });
            else
                p.addTriangle (r.getTopLeft(), r.getBottomLeft(), { r.getRight(), r.getCentreY() });
            g.fillPath (p);
        }
    }

    void ModuleLibrary::listBoxItemClicked (int row, const juce::MouseEvent& e)
    {
        if (row < 0 || row >= (int) rows.size())
            return;
        auto& r = rows[(size_t) row];
        if (r.kind == Row::Kind::module && r.numVersions > 1 && expanderBounds (list.getVisibleRowWidth(), list.getRowHeight()).contains (e.getPosition()))
            setExpanded (r.entry->originGuid, ! r.expanded);
    }

    void ModuleLibrary::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
    {
        selectRow (row);
        chooseSelected();
    }

    void ModuleLibrary::returnKeyPressed (int)
    {
        chooseSelected();
    }

    void ModuleLibrary::selectRow (int row)
    {
        list.selectRow (row);
    }

    void ModuleLibrary::chooseSelected()
    {
        const int row = list.getSelectedRow();
        if (row >= 0 && row < (int) rows.size() && rows[(size_t) row].entry != nullptr && onModuleChosen)
            onModuleChosen (*rows[(size_t) row].entry);
    }

    void ModuleLibrary::selectedRowsChanged (int row)
    {
        // source headings aren't selectable (Library.getNextSelectableLine)
        if (row >= 0 && row < (int) rows.size() && rows[(size_t) row].kind == Row::Kind::sourceLabel)
        {
            const int step = row >= lastSelectedRow ? 1 : -1;
            int next = row + step;
            if (next < 0 || next >= (int) rows.size())
                next = row - step;
            list.selectRow (next);
            return;
        }
        lastSelectedRow = row;
    }

    bool ModuleLibrary::keyPressed (const juce::KeyPress& key, juce::Component*)
    {
        return handleKey (key);
    }

    bool ModuleLibrary::handleKey (const juce::KeyPress& key)
    {
        // Library.onKeyDown: Left/Right expand or collapse the selected module
        if (key.getKeyCode() != juce::KeyPress::leftKey && key.getKeyCode() != juce::KeyPress::rightKey)
            return false;

        const int row = list.getSelectedRow();
        if (row < 0 || row >= (int) rows.size() || rows[(size_t) row].entry == nullptr)
            return false;

        auto* entry = rows[(size_t) row].entry;
        int top = row;
        while (top > 0 && rows[(size_t) top].kind != Row::Kind::module)
            --top;
        if (rows[(size_t) top].numVersions < 2)
            return true;

        const bool expand = key.getKeyCode() == juce::KeyPress::rightKey;
        setExpanded (entry->originGuid, expand);
        return true;
    }

    juce::var ModuleLibrary::getDragSourceDescription (const juce::SparseSet<int>& selection)
    {
        if (selection.isEmpty() || selection[0] >= (int) rows.size() || rows[(size_t) selection[0]].entry == nullptr)
            return {};
        return juce::String (dragPrefix) + toString (rows[(size_t) selection[0]].entry->guid);
    }

    Info ModuleLibrary::getInfoAt (juce::Point<int> p)
    {
        // ModuleLibrary.getInfoToDisplay
        if (clearSearch.isVisible() && clearSearch.getBounds().contains (p))
            return InfoTexts::forView ("ModuleLibrary/ClearSearchButton");
        if (search.getBounds().contains (p))
            return InfoTexts::forView ("ModuleLibrary/SearchBox");
        if (p.y >= tagCloud->getY() - 20)
            return InfoTexts::forView (! selectedTags.isEmpty() && p.y < tagCloud->getY() && p.x > getWidth() - 24 ? "ModuleLibrary/ClearTagsButton" : "ModuleLibrary/Tags");

        const auto inList = list.getLocalPoint (this, p);
        const int row = list.getRowContainingPosition (inList.x, inList.y);
        if (list.getLocalBounds().contains (inList) && row >= 0 && row < (int) rows.size())
        {
            auto& r = rows[(size_t) row];
            if (r.kind == Row::Kind::sourceLabel)
                return InfoTexts::forView (r.source == IInterfaceDefinition::MODULE_3RD_PARTY ? "ModuleLibrary/ThirdParty"
                                           : r.source == IInterfaceDefinition::MODULE_EMBEDDED ? "ModuleLibrary/Embedded"
                                           : r.source == IInterfaceDefinition::MODULE_IN_DEVELOPMENT ? "ModuleLibrary/InDevelopment"
                                           : "ModuleLibrary/ShippedWithIntegra");
            if (r.kind == Row::Kind::module && r.numVersions > 1 && inList.x >= list.getVisibleRowWidth() - 22)
                return InfoTexts::forView ("ModuleLibrary/ExpandVersions");
            return { r.entry->label, getInfoMarkdown (*r.entry, r.kind == Row::Kind::module), {} };
        }
        return InfoTexts::forView ("ModuleLibrary/ModuleLibrary");
    }

    juce::String ModuleLibrary::getTooltipForRow (int row)
    {
        if (row < 0 || row >= (int) rows.size() || rows[(size_t) row].entry == nullptr)
            return {};
        auto& r = rows[(size_t) row];
        auto text = getInfoMarkdown (*r.entry, r.kind == Row::Kind::module);
        text = text.replace ("**", "").replace ("##", "").replace ("#", "");
        if (! r.entry->tags.isEmpty())
            text << "\n\nTags: " << r.entry->tags.joinIntoString (", ");
        return text.trim();
    }

    //==============================================================================
    ModuleGraph::ModuleGraph (AppContext& c) : context (c)
    {
        setWantsKeyboardFocus (true);
        context.model.addChangeListener (this);
    }

    ModuleGraph::~ModuleGraph()
    {
        context.model.removeChangeListener (this);
    }

    void ModuleGraph::setBlock (const juce::String& path)
    {
        blockPath = path;
        selectedModule = {};
        selectedLinks.clear();
        layout();

        // restore the block's primary selection, as the Flex GUI did
        if (auto* block = context.model.get().findBlock (blockPath))
            if (block->userData.primarySelectedChild.isNotEmpty() && block->findModule (block->userData.primarySelectedChild) != nullptr)
                selectModule (blockPath + "." + block->userData.primarySelectedChild);
    }

    void ModuleGraph::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        if (draggingModule.isEmpty())
            layout();
    }

    void ModuleGraph::layout()
    {
        modules.clear();
        links.clear();

        auto* block = context.model.get().findBlock (blockPath);
        if (block == nullptr)
        {
            repaint();
            return;
        }

        // modules without a stored position get placed in a row below the others
        float nextFreeX = 1.0f, maxY = 0.0f;
        for (auto& m : block->modules)
            if (! m.position.isEmpty())
                maxY = juce::jmax (maxY, m.position.getBottom());

        for (auto& m : block->modules)
        {
            ModuleBox box;
            box.path = m.path;
            box.name = m.name;
            box.label = m.entry != nullptr ? m.entry->label : juce::String();

            auto pos = m.position;
            if (pos.isEmpty())
            {
                pos = { nextFreeX, maxY + 2.0f, ProjectController::moduleWidth, ProjectController::moduleHeight (m.entry) };
                nextFreeX += ProjectController::moduleWidth + 2.0f;
            }

            box.bounds = { pos.getX() * gridSize, pos.getY() * gridSize,
                           ProjectController::moduleWidth * gridSize, ProjectController::moduleHeight (m.entry) * gridSize };

            // audio pins, in the order of the interface definition
            if (m.entry != nullptr && m.entry->definition != nullptr)
            {
                std::vector<juce::String> ins, outs;
                for (auto* ep : m.entry->definition->get_endpoint_definitions())
                {
                    if (! ep->is_audio_stream())
                        continue;
                    (ep->get_stream_info()->get_direction() == IStreamInfo::INPUT ? ins : outs).push_back (toString (ep->get_name()));
                }

                auto addPins = [&] (const std::vector<juce::String>& names, bool input)
                {
                    const float top = box.bounds.getY() + gridSize, available = box.bounds.getHeight() - gridSize;
                    for (size_t i = 0; i < names.size(); ++i)
                    {
                        const float y = top + available * ((float) i + 0.5f) / (float) names.size();
                        box.pins.push_back ({ m.path + "." + names[i], { input ? box.bounds.getX() : box.bounds.getRight(), y }, input });
                    }
                };
                addPins (ins, true);
                addPins (outs, false);
            }

            modules.push_back (box);
        }

        for (auto& c : block->connections)
        {
            const Pin* from = nullptr;
            const Pin* to = nullptr;
            for (auto& m : modules)
                for (auto& p : m.pins)
                {
                    if (! p.isInput && p.endpoint == c.sourcePath) from = &p;
                    if (p.isInput && p.endpoint == c.targetPath)   to = &p;
                }

            if (from != nullptr && to != nullptr)
                links.push_back ({ c.path, c.sourcePath, c.targetPath, from->position, to->position });
        }

        updateSize();
        repaint();
    }

    void ModuleGraph::updateLinkEnds()
    {
        for (auto& l : links)
            for (auto& m : modules)
                for (auto& p : m.pins)
                {
                    if (! p.isInput && p.endpoint == l.sourceEndpoint) l.start = p.position;
                    if (p.isInput && p.endpoint == l.targetEndpoint)   l.end = p.position;
                }
    }

    void ModuleGraph::updateSize()
    {
        float right = 600, bottom = 300;
        for (auto& m : modules)
        {
            right = juce::jmax (right, m.bounds.getRight() + gridSize * 4);
            bottom = juce::jmax (bottom, m.bounds.getBottom() + gridSize * 4);
        }

        if (auto* parent = getParentComponent())
        {
            right = juce::jmax (right, (float) parent->getWidth());
            bottom = juce::jmax (bottom, (float) parent->getHeight());
        }

        setSize ((int) right, (int) bottom);
    }

    juce::Path ModuleGraph::linkPath (juce::Point<float> start, juce::Point<float> end)
    {
        juce::Path p;
        p.startNewSubPath (start);
        const float dx = juce::jmax (30.0f, std::abs (end.x - start.x) * 0.5f);
        p.cubicTo (start.x + dx, start.y, end.x - dx, end.y, end.x, end.y);
        return p;
    }

    void ModuleGraph::paint (juce::Graphics& g)
    {
        auto& t = Theme::get();
        g.fillAll (t.background);

        // grid
        g.setColour (t.gridLine);
        for (float x = 0; x < (float) getWidth(); x += gridSize * 4)
            g.drawVerticalLine ((int) x, 0.0f, (float) getHeight());
        for (float y = 0; y < (float) getHeight(); y += gridSize * 4)
            g.drawHorizontalLine ((int) y, 0.0f, (float) getWidth());

        if (blockPath.isEmpty())
            return;

        for (auto& l : links)
        {
            const bool linkSelected = selectedLinks.count (l.connectionPath) > 0;
            g.setColour (linkSelected ? t.linkSelected : t.link);
            g.strokePath (linkPath (l.start, l.end), juce::PathStrokeType (linkSelected ? 3.0f : 2.0f));
        }

        if (dragPin.has_value())
        {
            g.setColour (t.accent);
            auto from = dragPin->isInput ? dragPinEnd : dragPin->position;
            auto to = dragPin->isInput ? dragPin->position : dragPinEnd;
            g.strokePath (linkPath (from, to), juce::PathStrokeType (2.0f));
        }

        for (auto& m : modules)
        {
            const bool selected = isModuleSelected (m.path);
            g.setColour (juce::Colours::black.withAlpha (0.15f));
            g.fillRoundedRectangle (m.bounds.translated (2, 3), 5.0f);
            g.setColour (t.moduleFill);
            g.fillRoundedRectangle (m.bounds, 5.0f);

            auto header = m.bounds.withHeight (gridSize);
            g.setColour (t.moduleHeader);
            g.fillRoundedRectangle (header, 5.0f);
            g.fillRect (header.withTrimmedTop (gridSize * 0.5f));

            g.setColour (selected ? t.accent : t.outline);
            g.drawRoundedRectangle (m.bounds, 5.0f, selected ? 2.5f : 1.0f);

            g.setColour (t.text);
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawFittedText (m.name, header.reduced (6, 0).withTrimmedRight (18.0f).toNearestInt(), juce::Justification::centredLeft, 1);   // clear of the Live tick
            g.setColour (t.textDim);
            g.setFont (juce::FontOptions (11.0f));
            g.drawFittedText (m.label, m.bounds.withTrimmedTop (gridSize).reduced (10, 2).toNearestInt(), juce::Justification::centred, 2);

            // Live View tick (TickButtonSkin): a disc in the track colour, ticked when any
            // of the module's controls is in the Live view
            {
                auto tick = getLiveTickBounds (m.path);
                const auto track = parentPath (blockPath);
                auto colour = juce::Colour (0xff606060);
                if (auto* tr = context.model.get().findTrack (track))
                    colour = tr->userData.getColour();
                if (! ActiveButton::isEffectivelyActive (context, blockPath))
                    colour = juce::Colour::greyLevel ((colour.getFloatRed() + colour.getFloatGreen() + colour.getFloatBlue()) / 3.0f);
                g.setColour (colour.withAlpha (0.2f));
                g.fillEllipse (tick);
                g.setColour (Theme::isDarkScheme() ? juce::Colour (0xff8c8c8c) : juce::Colour (0xff747474));
                g.drawEllipse (tick, 1.0f);
                if (hasLiveViewControls (m.path))
                {
                    const float r = tick.getWidth() / 2.0f;
                    juce::Path mark;
                    mark.startNewSubPath (tick.getX() + 0.5f * r, tick.getY() + r);
                    mark.lineTo (tick.getX() + r, tick.getY() + 1.5f * r);
                    mark.lineTo (tick.getX() + 1.5f * r, tick.getY() + 0.5f * r);
                    g.setColour (t.text);
                    g.strokePath (mark, juce::PathStrokeType (2.0f));
                }
            }

            for (auto& p : m.pins)
            {
                g.setColour (t.pin);
                g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (p.position));
                g.setColour (t.moduleFill);
                g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre (p.position));
            }
        }

        if (modules.empty())
        {
            g.setColour (t.textDim);
            g.setFont (juce::FontOptions (15.0f));
            g.drawText ("Drag modules here from the library", getLocalBounds(), juce::Justification::centred);
        }

        // the module being dragged in, and the connection it would replace
        if (dropPreview.has_value())
        {
            for (auto& l : links)
                if (l.connectionPath == dropLink)
                {
                    g.setColour (t.linkSelected);
                    g.strokePath (linkPath (l.start, l.end), juce::PathStrokeType (3.5f));
                }
            g.setColour (t.moduleFill.withAlpha (0.5f));
            g.fillRoundedRectangle (*dropPreview, 5.0f);
            g.setColour (t.accent.withAlpha (0.5f));
            g.drawRoundedRectangle (*dropPreview, 5.0f, 1.5f);
        }

        // snap lines while a module is dragged (lineStyle 4, #808080, alpha 0.3)
        g.setColour (juce::Colour (0xff808080).withAlpha (0.3f));
        for (float x : snapLinesX)
            g.fillRect (x - 2.0f, 0.0f, 4.0f, (float) getHeight());
        for (float y : snapLinesY)
            g.fillRect (0.0f, y - 2.0f, (float) getWidth(), 4.0f);

        // rubber band (ModuleGraph: fill alpha 0.1, border alpha 0.2)
        if (rubberBand.has_value())
        {
            g.setColour (t.text.withAlpha (0.1f));
            g.fillRect (*rubberBand);
            g.setColour (t.text.withAlpha (0.2f));
            g.drawRect (*rubberBand, 1.0f);
        }
    }

    std::vector<juce::String> ModuleGraph::getSelectedModules() const
    {
        // the Ctrl/Cmd-click or rubber-band group, while it still contains the primary selection
        std::vector<juce::String> result;
        auto exists = [this] (const juce::String& path)
        {
            return std::any_of (modules.begin(), modules.end(), [&] (auto& m) { return m.path == path; });
        };
        if (selectedModule.isEmpty() || ! exists (selectedModule))
            return result;
        if (groupSelection.count (selectedModule) > 0)
        {
            for (auto& path : groupSelection)
                if (exists (path))
                    result.push_back (path);
        }
        else
        {
            result.push_back (selectedModule);
        }
        return result;
    }

    bool ModuleGraph::isModuleSelected (const juce::String& path) const
    {
        auto selected = getSelectedModules();
        return std::find (selected.begin(), selected.end(), path) != selected.end();
    }

    const ModuleGraph::Pin* ModuleGraph::pinAt (juce::Point<float> p) const
    {
        for (auto& m : modules)
            for (auto& pin : m.pins)
                if (pin.position.getDistanceFrom (p) < 8.0f)
                    return &pin;
        return nullptr;
    }

    ModuleGraph::ModuleBox* ModuleGraph::moduleAt (juce::Point<float> p)
    {
        for (auto it = modules.rbegin(); it != modules.rend(); ++it)
            if (it->bounds.contains (p))
                return &*it;
        return nullptr;
    }

    const ModuleGraph::Link* ModuleGraph::linkAt (juce::Point<float> p) const
    {
        for (auto& l : links)
        {
            juce::Point<float> nearest;
            juce::Path path = linkPath (l.start, l.end);
            path.getNearestPoint (p, nearest);
            if (nearest.getDistanceFrom (p) < 5.0f)
                return &l;
        }
        return nullptr;
    }

    void ModuleGraph::selectModule (const juce::String& path)
    {
        selectedModule = path;
        context.model.selectedModule = path;
        if (onModuleSelected)
            onModuleSelected (path);
        repaint();

        // persist as the block's primarySelectedChild (GUI state, not an undo step)
        if (auto* block = context.model.get().findBlock (blockPath))
        {
            const auto name = path.isEmpty() ? juce::String() : lastElement (path);
            if (block->userData.primarySelectedChild != name)
            {
                auto data = block->userData;
                data.primarySelectedChild = name;
                context.session.setValue (blockPath + ".userData", data.toString());
            }
        }
    }

    juce::Rectangle<float> ModuleGraph::getLiveTickBounds (const juce::String& modulePath) const
    {
        for (auto& m : modules)
            if (m.path == modulePath)
                return { m.bounds.getRight() - 4.0f - 14.0f, m.bounds.getY() + 2.0f, 14.0f, 14.0f };
        return {};
    }

    bool ModuleGraph::hasLiveViewControls (const juce::String& modulePath) const
    {
        if (auto* block = context.model.get().findBlock (blockPath))
            for (auto& c : block->userData.liveViewControls)
                if (c.moduleName == lastElement (modulePath))
                    return true;
        return false;
    }

    void ModuleGraph::toggleLiveViewControls (const juce::String& modulePath)
    {
        std::vector<std::pair<juce::String, juce::Rectangle<float>>> widgets;
        for (auto& spec : WidgetSpec::forModule (context.session, modulePath))
            widgets.push_back ({ spec.label, spec.position });
        context.controller.setModuleLiveViewControls (modulePath, widgets, ! hasLiveViewControls (modulePath));
        repaint();
    }

    void ModuleGraph::selectLinksOf (const juce::String& modulePath, bool select)
    {
        for (auto& l : links)
        {
            const auto from = parentPath (l.sourceEndpoint), to = parentPath (l.targetEndpoint);
            if (from != modulePath && to != modulePath)
                continue;
            if (select)
                selectedLinks.insert (l.connectionPath);
            else if (! isModuleSelected (from == modulePath ? to : from))
                selectedLinks.erase (l.connectionPath);     // unless the other end stays selected
        }
    }

    void ModuleGraph::showGraphMenu (juce::Point<int> at)
    {
        // ModuleGraph context menu
        const auto track = parentPath (blockPath);
        const bool anything = ! getSelectedModules().empty() || ! selectedLinks.empty();
        juce::PopupMenu menu;
        menu.addItem ("Remove Selection", anything, false, [this] { deleteSelection(); });
        menu.addSeparator();
        menu.addItem ("Import Module...", [this, at] { importModule (at); });
        const auto selected = selectedModule;
        menu.addItem ("Export Module...", selected.isNotEmpty(), false, [this, selected]
        {
            chooser = std::make_unique<juce::FileChooser> ("Export Module", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                                                                .getChildFile (lastElement (selected) + ".integra"), "*.integra");
            chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [this, selected] (const juce::FileChooser& fc)
            {
                if (fc.getResult() != juce::File())
                    context.controller.exportNode (selected, fc.getResult());
            });
        });
        menu.addSeparator();
        menu.addItem ("Change Track Colour...", context.model.get().findTrack (track) != nullptr && ActiveButton::isEffectivelyActive (context, track), false,
                      [this, track, at] { TrackColourPicker::show (context, track, *this, at); });
        menu.showMenuAsync (juce::PopupMenu::Options());
    }

    void ModuleGraph::importModule (juce::Point<int> position)
    {
        chooser = std::make_unique<juce::FileChooser> ("Import Module", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.integra;*.mixd");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, position] (const juce::FileChooser& fc)
        {
            if (fc.getResult() == juce::File())
                return;
            juce::String module;
            auto r = context.controller.importModule (fc.getResult(), blockPath, position.toFloat() / gridSize, &module);
            if (r.failed())
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Import Module", r.getErrorMessage());
            else
            {
                groupSelection = { module };
                selectModule (module);
            }
        });
    }

    void ModuleGraph::mouseDown (const juce::MouseEvent& e)
    {
        grabKeyboardFocus();
        dragPin.reset();
        draggingModule = {};

        if (auto* pin = pinAt (e.position))
        {
            dragPin = *pin;
            dragPinEnd = e.position;
            return;
        }

        collapseGroupOnUp = false;
        rubberBand.reset();

        if (auto* module = moduleAt (e.position))
        {
            if (! e.mods.isPopupMenu() && getLiveTickBounds (module->path).expanded (2.0f).contains (e.position))
            {
                toggleLiveViewControls (module->path);
                return;
            }

            // ModuleGraph.onClickModule: Ctrl (Cmd on macOS) adds or removes a module; a
            // plain click on a module of a group keeps the group for dragging. Selecting a
            // module selects its connections too.
            auto group = getSelectedModules();
            const bool wasSelected = std::find (group.begin(), group.end(), module->path) != group.end();
            if (e.mods.isCommandDown() && ! e.mods.isPopupMenu())
            {
                groupSelection = { group.begin(), group.end() };
                if (wasSelected)
                {
                    groupSelection.erase (module->path);
                    const auto path = module->path;
                    selectModule (groupSelection.empty() ? juce::String() : *groupSelection.begin());
                    selectLinksOf (path, false);
                    repaint();
                    return;
                }
                groupSelection.insert (module->path);
            }
            else if (! wasSelected || e.mods.isPopupMenu())
            {
                groupSelection = { module->path };
                selectedLinks.clear();
            }
            else
            {
                collapseGroupOnUp = group.size() > 1;
            }
            selectModule (module->path);
            selectLinksOf (module->path, true);

            if (e.mods.isPopupMenu())
            {
                showModuleMenu (*module);
                return;
            }

            draggingModule = module->path;
            dragOffset = e.position - module->bounds.getPosition();
            dragOriginal = module->bounds;
            dragStartPositions.clear();
            for (auto& m : modules)
                if (isModuleSelected (m.path))
                    dragStartPositions[m.path] = m.bounds.getPosition();
            return;
        }

        if (auto* link = linkAt (e.position))
        {
            // ModuleGraph.onClickLink: without Ctrl/Cmd everything else is deselected; the
            // link toggles
            const auto path = link->connectionPath;
            const bool wasSelected = selectedLinks.count (path) > 0;
            if (! e.mods.isCommandDown() && ! e.mods.isPopupMenu())
            {
                groupSelection.clear();
                selectModule ({});
                selectedLinks.clear();
            }
            if (e.mods.isPopupMenu())
                selectedLinks.insert (path);
            else if (wasSelected && e.mods.isCommandDown())
                selectedLinks.erase (path);
            else
                selectedLinks.insert (path);
            repaint();
            if (e.mods.isPopupMenu())
                showGraphMenu (e.getPosition());
            return;
        }

        // empty space: rubber-band selection (with Ctrl/Cmd it toggles modules in and out)
        rubberBandBase.clear();
        if (e.mods.isCommandDown())
        {
            for (auto& path : getSelectedModules())
                rubberBandBase.insert (path);
        }
        else if (! e.mods.isPopupMenu())
        {
            groupSelection.clear();
            selectedLinks.clear();
            selectModule ({});
        }
        if (! e.mods.isPopupMenu())
            rubberBand = juce::Rectangle<float> (e.position, e.position);
        else
            showGraphMenu (e.getPosition());
        repaint();
    }

    void ModuleGraph::updateRubberBand (juce::Rectangle<float> area, bool toggle)
    {
        rubberBand = area;
        auto selection = rubberBandBase;
        juce::String first;
        for (auto& m : modules)
        {
            if (! m.bounds.intersects (area))
                continue;
            if (toggle && rubberBandBase.count (m.path) > 0)
                selection.erase (m.path);
            else
                selection.insert (m.path);
            if (first.isEmpty() && selection.count (m.path) > 0)
                first = m.path;
        }
        groupSelection = selection;
        if (selection.count (selectedModule) == 0)
            selectModule (first.isNotEmpty() ? first : selection.empty() ? juce::String() : *selection.begin());
        // the selected modules' connections are selected with them
        selectedLinks.clear();
        for (auto& path : selection)
            selectLinksOf (path, true);
        repaint();
    }

    void ModuleGraph::mouseDrag (const juce::MouseEvent& e)
    {
        if (rubberBand.has_value())
        {
            updateRubberBand (juce::Rectangle<float> (e.mouseDownPosition, e.position), e.mods.isCommandDown());
            return;
        }

        if (dragPin.has_value())
        {
            dragPinEnd = e.position;
            repaint();
            return;
        }

        if (draggingModule.isNotEmpty())
        {
            // every selected module moves with the dragged one (ModuleGraph.doModuleReposition)
            auto topLeft = e.position - dragOffset;
            topLeft = { (float) (int) juce::jmax (0.0f, topLeft.x), (float) (int) juce::jmax (0.0f, topLeft.y) };

            // ModuleGraph.getModuleRepositionSnap: the dragged module's edges snap to the edges
            // of unselected modules within 10 px, nearest first; left / top win
            snapLinesX.clear();
            snapLinesY.clear();
            const auto size = dragOriginal.getBottomRight() - dragOriginal.getPosition();
            auto snapAxis = [&] (float start, float length, bool horizontal, std::vector<float>& lines)
            {
                std::optional<float> startSnap, endSnap;
                float best = snapThreshold;
                for (auto& other : modules)
                {
                    if (other.path == draggingModule || isModuleSelected (other.path))
                        continue;
                    const float a = horizontal ? other.bounds.getX() : other.bounds.getY();
                    const float b = horizontal ? other.bounds.getRight() : other.bounds.getBottom();
                    for (float edge : { a, b })
                    {
                        if (std::abs (start - edge) < best)          { best = std::abs (start - edge); startSnap = edge; endSnap.reset(); }
                        if (std::abs (start + length - edge) < best) { best = std::abs (start + length - edge); endSnap = edge; startSnap.reset(); }
                    }
                }
                if (startSnap)  { lines.push_back (*startSnap); return *startSnap; }
                if (endSnap)    { lines.push_back (*endSnap); return *endSnap - length; }
                return start;
            };
            topLeft = { snapAxis (topLeft.x, size.x, true, snapLinesX), snapAxis (topLeft.y, size.y, false, snapLinesY) };
            auto delta = topLeft - dragOriginal.getPosition();
            for (auto& [path, start] : dragStartPositions)
                delta = { juce::jmax (delta.x, -start.x), juce::jmax (delta.y, -start.y) };
            collapseGroupOnUp = collapseGroupOnUp && delta.isOrigin();

            for (auto& m : modules)
            {
                auto it = dragStartPositions.find (m.path);
                if (it == dragStartPositions.end())
                    continue;
                const auto newPosition = it->second + delta;
                const auto step = newPosition - m.bounds.getPosition();
                m.bounds.setPosition (newPosition);
                for (auto& p : m.pins)
                    p.position += step;
            }

            updateLinkEnds();
            repaint();
        }
    }

    void ModuleGraph::mouseUp (const juce::MouseEvent& e)
    {
        if (dragPin.has_value())
        {
            auto* target = pinAt (e.position);
            if (target != nullptr && target->isInput != dragPin->isInput
                 && parentPath (target->endpoint) != parentPath (dragPin->endpoint))
            {
                const auto& source = dragPin->isInput ? target->endpoint : dragPin->endpoint;
                const auto& dest = dragPin->isInput ? dragPin->endpoint : target->endpoint;
                const auto s = source, d = dest;
                dragPin.reset();
                context.controller.connect (blockPath, s, d);
            }
            dragPin.reset();
            repaint();
            return;
        }

        if (rubberBand.has_value())
        {
            rubberBand.reset();
            repaint();
            return;
        }

        if (draggingModule.isNotEmpty())
        {
            draggingModule = {};
            snapLinesX.clear();
            snapLinesY.clear();

            // one undo step for the whole group (SetModulePosition for each)
            std::map<juce::String, juce::Rectangle<float>> positions;
            for (auto& m : modules)
            {
                auto it = dragStartPositions.find (m.path);
                if (it != dragStartPositions.end() && m.bounds.getPosition() != it->second)
                    positions[m.name] = { m.bounds.getX() / gridSize, m.bounds.getY() / gridSize,
                                          m.bounds.getWidth() / gridSize, m.bounds.getHeight() / gridSize };
            }
            dragStartPositions.clear();
            if (! positions.empty())
                context.controller.setModulePositions (blockPath, positions);

            if (collapseGroupOnUp)
                groupSelection = { selectedModule };    // a click on a module of a group selects just it
            collapseGroupOnUp = false;
            layout();
        }
    }

    void ModuleGraph::mouseDoubleClick (const juce::MouseEvent& e)
    {
        if (auto* module = moduleAt (e.position))
        {
            // ModuleGraphElement: double-click the name to rename the module
            if (module->bounds.withHeight (gridSize).contains (e.position))
                renameModule (module->path);
            else
                selectModule (module->path);
        }
    }

    Info ModuleGraph::getInfoAt (juce::Point<int> p)
    {
        // ModuleGraph.getInfoToDisplay: pins, links, the module's description, else the block
        if (auto* pin = pinAt (p.toFloat()))
            return InfoTexts::forView (pin->isInput ? "ModuleGraph/InputPin" : "ModuleGraph/OutputPin");
        if (auto* module = moduleAt (p.toFloat()))
        {
            auto* block = context.model.get().findBlock (blockPath);
            auto* m = block != nullptr ? block->findModule (module->name) : nullptr;
            if (m != nullptr && m->entry != nullptr)
                return { module->name + " (" + m->entry->label + ")", ModuleLibrary::getInfoMarkdown (*m->entry, true), {} };
        }
        if (linkAt (p.toFloat()) != nullptr)
            return InfoTexts::forView ("ModuleGraph/Link");
        if (blockPath.isNotEmpty())
            return InfoTexts::forNode (context, blockPath, "ArrangeView/BlockView");
        return {};
    }

    void ModuleGraph::renameModule (const juce::String& path)
    {
        for (auto& m : modules)
        {
            if (m.path != path)
                continue;
            auto bounds = m.bounds.withHeight (gridSize + 4.0f).toNearestInt();
            startInlineRename (renameEditor, *this, bounds.withWidth (juce::jmax (bounds.getWidth(), 100)), m.name,
                               [this, path] (const juce::String& name)
                               {
                                   if (context.controller.rename (path, name).wasOk())
                                       selectModule (parentPath (path) + "." + name);
                               });
            return;
        }
    }

    bool ModuleGraph::keyPressed (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        {
            deleteSelection();
            return true;
        }
        return false;
    }

    void ModuleGraph::deleteSelection()
    {
        // ModuleGraph.removeSelection: the selected connections, then the selected modules,
        // as one undo step
        juce::StringArray paths;
        for (auto& link : selectedLinks)
            paths.add (link);
        for (auto& path : getSelectedModules())
            paths.add (path);
        if (paths.isEmpty())
            return;
        context.controller.remove (paths);
        selectedLinks.clear();
        groupSelection.clear();
        selectModule ({});
    }

    void ModuleGraph::showModuleMenu (const ModuleBox& module)
    {
        juce::PopupMenu menu;
        const auto path = module.path;
        menu.addItem ("Rename", [this, path] { renameModule (path); });
        menu.addItem ("Export module...", [this, path]
        {
            auto chooser = std::make_shared<juce::FileChooser> ("Export module", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                                                    .getChildFile (lastElement (path) + ".integra"), "*.integra");
            chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [this, path, chooser] (const juce::FileChooser& fc)
            {
                if (fc.getResult() != juce::File())
                    context.controller.exportNode (path, fc.getResult());
            });
        });
        menu.addSeparator();
        menu.addItem ("Delete", [this] { deleteSelection(); });
        menu.showMenuAsync (juce::PopupMenu::Options());
    }

    bool ModuleGraph::isInterestedInDragSource (const SourceDetails& details)
    {
        return blockPath.isNotEmpty() && details.description.toString().startsWith (ModuleLibrary::dragPrefix);
    }

    std::optional<GUID> ModuleGraph::draggedModule (const SourceDetails& details)
    {
        auto guidText = details.description.toString().fromFirstOccurrenceOf (ModuleLibrary::dragPrefix, false, false);
        GUID guid;
        if (CGuidHelper::string_to_guid (toStd (guidText), guid) != CError::SUCCESS)
            return std::nullopt;
        return guid;
    }

    juce::Point<float> ModuleGraph::dropGridPosition (juce::Point<int> localPosition)
    {
        auto position = localPosition.toFloat() / gridSize;
        return { juce::jmax (0.0f, std::round (position.x * 2.0f) / 2.0f), juce::jmax (0.0f, std::round (position.y * 2.0f) / 2.0f) };
    }

    void ModuleGraph::updateDropPreview (const SourceDetails& details)
    {
        auto guid = draggedModule (details);
        auto* entry = guid.has_value() ? context.session.getCatalogue().findByGuid (*guid) : nullptr;
        if (entry == nullptr)
            return;

        const auto position = dropGridPosition (details.localPosition) * gridSize;
        const auto preview = juce::Rectangle<float> (position.x, position.y, ProjectController::moduleWidth * gridSize,
                                                     ProjectController::moduleHeight (entry) * gridSize);

        // a connection running through the new module is replaced by links into and out of it
        juce::String link;
        float best = std::numeric_limits<float>::max();
        if (entry->numAudioInputs > 0 || entry->numAudioOutputs > 0)
            for (auto& l : links)
            {
                juce::Point<float> nearest;
                linkPath (l.start, l.end).getNearestPoint (preview.getCentre(), nearest);
                const float distance = nearest.getDistanceFrom (preview.getCentre());
                if (preview.contains (nearest) && distance < best)
                {
                    best = distance;
                    link = l.connectionPath;
                }
            }

        if (dropPreview != preview || dropLink != link)
        {
            dropPreview = preview;
            dropLink = link;
            repaint();
        }
    }

    void ModuleGraph::itemDragExit (const SourceDetails&)
    {
        dropPreview.reset();
        dropLink = {};
        repaint();
    }

    void ModuleGraph::itemDropped (const SourceDetails& details)
    {
        updateDropPreview (details);
        const auto link = dropLink;
        dropPreview.reset();
        dropLink = {};
        repaint();

        auto guid = draggedModule (details);
        if (! guid.has_value())
            return;

        const auto position = dropGridPosition (details.localPosition);
        auto module = link.isNotEmpty() ? context.controller.spliceModule (blockPath, *guid, position, link)
                                        : context.controller.addModule (blockPath, *guid, position);
        if (module.isNotEmpty())
            selectModule (module);
    }

    //==============================================================================
    ModuleView::ModuleView (AppContext& c)
        : context (c), library (c), graph (c), properties (c)
    {
        backButton.onClick = [this] { if (onBack) onBack(); };
        addAndMakeVisible (backButton);
        breadcrumb.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        addAndMakeVisible (breadcrumb);
        addAndMakeVisible (library);

        graphViewport.setViewedComponent (&graph, false);
        graphViewport.setScrollBarsShown (true, true);
        addAndMakeVisible (graphViewport);
        addAndMakeVisible (properties);

        verticalLayout.setItemLayout (0, 120, -1.0, -0.55);   // graph
        verticalLayout.setItemLayout (1, 6, 6, 6);            // divider
        verticalLayout.setItemLayout (2, 120, -1.0, -0.45);   // properties
        divider = std::make_unique<juce::StretchableLayoutResizerBar> (&verticalLayout, 1, false);
        addAndMakeVisible (*divider);

        graph.onModuleSelected = [this] (const juce::String& module) { properties.setModule (module); };
        properties.onRenamed = [this] (const juce::String& renamed) { graph.refreshLayout(); graph.selectModulePath (renamed); };
        addAndMakeVisible (blockActive);
        addAndMakeVisible (blockMeter);
        blockActive.onChange = [this] { graph.repaint(); };

        library.onModuleChosen = [this] (const ModuleCatalogue::Entry& entry)
        {
            if (graph.getBlock().isEmpty())
                return;

            // place to the right of the existing modules
            float x = 1.0f;
            if (auto* block = context.model.get().findBlock (graph.getBlock()))
                for (auto& m : block->modules)
                    x = juce::jmax (x, m.position.getRight() + 2.0f);
            auto module = context.controller.addModule (graph.getBlock(), entry.guid, { x, 2.0f });
            if (module.isNotEmpty())
                properties.setModule (module);
        };
    }

    void ModuleView::setBlock (const juce::String& blockPath)
    {
        properties.setModule ({});
        graph.setBlock (blockPath);     // may restore a selection, which shows its controls
        library.refresh();

        auto crumbs = juce::StringArray::fromTokens (blockPath, ".", {});
        breadcrumb.setText (crumbs.joinIntoString ("  >  "), juce::dontSendNotification);

        blockActive.setContainer (blockPath);
        blockMeter.setContainer (blockPath);
        if (auto* track = context.model.get().findTrackOfBlock (blockPath))
        {
            blockMeter.setColour (track->userData.getColour());
            blockActive.setColour (track->userData.getColour());
        }
    }

    void ModuleView::paint (juce::Graphics& g)
    {
        g.fillAll (Theme::get().background);
    }

    void ModuleView::resized()
    {
        auto area = getLocalBounds();
        auto top = area.removeFromTop (32).reduced (4);
        backButton.setBounds (top.removeFromLeft (100));
        top.removeFromLeft (10);
        blockMeter.setBounds (top.removeFromRight (128).withSizeKeepingCentre (128, 6));
        top.removeFromRight (6);
        blockActive.setBounds (top.removeFromRight (20).withSizeKeepingCentre (20, 20));
        breadcrumb.setBounds (top);

        library.setBounds (area.removeFromRight (220));    // Flex: the library is right of the graph

        juce::Component* comps[] = { &graphViewport, divider.get(), &properties };
        verticalLayout.layOutComponents (comps, 3, area.getX(), area.getY(), area.getWidth(), area.getHeight(), true, true);

        // the Info view sits right of the module properties (Flex: _moduleGraphInfoView)
        infoArea = {};
        if (infoWidth > 0)
        {
            auto bounds = properties.getBounds();
            infoArea = bounds.removeFromRight (juce::jmin (infoWidth, bounds.getWidth() / 2));
            properties.setBounds (bounds.withTrimmedRight (2));
        }
        graph.refreshLayout();
    }
}
