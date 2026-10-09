#include "ModuleView.h"
#include "IntegraLookAndFeel.h"

namespace integra
{
    //==============================================================================
    ModuleLibrary::ModuleLibrary (AppContext& c) : context (c)
    {
        search.setTextToShowWhenEmpty ("Search modules", Theme::get().textDim);
        search.onTextChange = [this] { refresh(); };
        addAndMakeVisible (search);

        tagFilter.onChange = [this] { refresh(); };
        addAndMakeVisible (tagFilter);

        list.setRowHeight (24);
        list.setMultipleSelectionEnabled (false);
        addAndMakeVisible (list);
        refresh();
    }

    void ModuleLibrary::refresh()
    {
        if (tagFilter.getNumItems() == 0)
        {
            tagFilter.addItem ("All modules", 1);
            int id = 2;
            for (auto& tag : context.session.getCatalogue().getAllTags())
                tagFilter.addItem (tag, id++);
            tagFilter.setSelectedId (1, juce::dontSendNotification);
        }

        const auto text = search.getText().trim();
        const auto tag = tagFilter.getSelectedId() > 1 ? tagFilter.getText() : juce::String();

        shown.clear();
        for (auto* e : context.session.getCatalogue().getUserModules())
        {
            if (tag.isNotEmpty() && ! e->tags.contains (tag))
                continue;
            if (text.isNotEmpty() && ! e->label.containsIgnoreCase (text) && ! e->description.containsIgnoreCase (text)
                 && ! e->tags.joinIntoString (" ").containsIgnoreCase (text))
                continue;
            shown.push_back (e);
        }

        list.updateContent();
        list.repaint();
    }

    void ModuleLibrary::paint (juce::Graphics& g)
    {
        g.fillAll (Theme::get().panelAlt);
    }

    void ModuleLibrary::resized()
    {
        auto area = getLocalBounds().reduced (4);
        search.setBounds (area.removeFromTop (26));
        area.removeFromTop (4);
        tagFilter.setBounds (area.removeFromTop (24));
        area.removeFromTop (4);
        list.setBounds (area);
    }

    int ModuleLibrary::getNumRows()     { return (int) shown.size(); }

    void ModuleLibrary::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
    {
        if (row < 0 || row >= (int) shown.size())
            return;

        auto& t = Theme::get();
        if (selected)
            g.fillAll (t.selection);

        auto* e = shown[(size_t) row];
        g.setColour (t.text);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (e->label, 8, 0, width - 60, height, juce::Justification::centredLeft, true);

        g.setColour (t.textDim);
        g.setFont (juce::FontOptions (11.0f));
        juce::String pins;
        if (e->numAudioInputs > 0 || e->numAudioOutputs > 0)
            pins << e->numAudioInputs << " > " << e->numAudioOutputs;
        g.drawText (pins, width - 56, 0, 50, height, juce::Justification::centredRight);
    }

    void ModuleLibrary::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
    {
        if (row >= 0 && row < (int) shown.size() && onModuleChosen)
            onModuleChosen (*shown[(size_t) row]);
    }

    juce::var ModuleLibrary::getDragSourceDescription (const juce::SparseSet<int>& rows)
    {
        if (rows.isEmpty() || rows[0] >= (int) shown.size())
            return {};
        return juce::String (dragPrefix) + toString (shown[(size_t) rows[0]]->guid);
    }

    juce::String ModuleLibrary::getTooltipForRow (int row)
    {
        if (row < 0 || row >= (int) shown.size())
            return {};
        auto* e = shown[(size_t) row];
        return e->label + "\n" + e->description + "\nTags: " + e->tags.joinIntoString (", ");
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
        selectedModule = selectedLink = {};
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
            g.setColour (l.connectionPath == selectedLink ? t.linkSelected : t.link);
            g.strokePath (linkPath (l.start, l.end), juce::PathStrokeType (l.connectionPath == selectedLink ? 3.0f : 2.0f));
        }

        if (dragPin != nullptr)
        {
            g.setColour (t.accent);
            auto from = dragPin->isInput ? dragPinEnd : dragPin->position;
            auto to = dragPin->isInput ? dragPin->position : dragPinEnd;
            g.strokePath (linkPath (from, to), juce::PathStrokeType (2.0f));
        }

        for (auto& m : modules)
        {
            const bool selected = m.path == selectedModule;
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
            g.drawFittedText (m.name, header.reduced (6, 0).toNearestInt(), juce::Justification::centredLeft, 1);
            g.setColour (t.textDim);
            g.setFont (juce::FontOptions (11.0f));
            g.drawFittedText (m.label, m.bounds.withTrimmedTop (gridSize).reduced (10, 2).toNearestInt(), juce::Justification::centred, 2);

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

    void ModuleGraph::mouseDown (const juce::MouseEvent& e)
    {
        grabKeyboardFocus();
        dragPin = nullptr;
        draggingModule = {};

        if (auto* pin = pinAt (e.position))
        {
            dragPin = pin;
            dragPinEnd = e.position;
            return;
        }

        if (auto* module = moduleAt (e.position))
        {
            selectedLink = {};
            selectModule (module->path);

            if (e.mods.isPopupMenu())
            {
                showModuleMenu (*module);
                return;
            }

            draggingModule = module->path;
            dragOffset = e.position - module->bounds.getPosition();
            dragOriginal = module->bounds;
            return;
        }

        if (auto* link = linkAt (e.position))
        {
            selectedLink = link->connectionPath;
            repaint();
            if (e.mods.isPopupMenu())
            {
                juce::PopupMenu menu;
                menu.addItem ("Delete connection", [this] { deleteSelection(); });
                menu.showMenuAsync (juce::PopupMenu::Options());
            }
            return;
        }

        selectedLink = {};
        selectModule ({});
    }

    void ModuleGraph::mouseDrag (const juce::MouseEvent& e)
    {
        if (dragPin != nullptr)
        {
            dragPinEnd = e.position;
            repaint();
            return;
        }

        if (draggingModule.isNotEmpty())
        {
            for (auto& m : modules)
            {
                if (m.path != draggingModule)
                    continue;

                auto topLeft = e.position - dragOffset;
                // snap to half grid units
                topLeft = { std::round (topLeft.x / (gridSize * 0.5f)) * gridSize * 0.5f,
                            std::round (topLeft.y / (gridSize * 0.5f)) * gridSize * 0.5f };
                topLeft = { juce::jmax (0.0f, topLeft.x), juce::jmax (0.0f, topLeft.y) };
                const auto delta = topLeft - m.bounds.getPosition();
                m.bounds.setPosition (topLeft);
                for (auto& p : m.pins)
                    p.position += delta;
            }

            updateLinkEnds();
            repaint();
        }
    }

    void ModuleGraph::mouseUp (const juce::MouseEvent& e)
    {
        if (dragPin != nullptr)
        {
            auto* target = pinAt (e.position);
            if (target != nullptr && target->isInput != dragPin->isInput
                 && parentPath (target->endpoint) != parentPath (dragPin->endpoint))
            {
                const auto& source = dragPin->isInput ? target->endpoint : dragPin->endpoint;
                const auto& dest = dragPin->isInput ? dragPin->endpoint : target->endpoint;
                const auto s = source, d = dest;
                dragPin = nullptr;
                context.controller.connect (blockPath, s, d);
            }
            dragPin = nullptr;
            repaint();
            return;
        }

        if (draggingModule.isNotEmpty())
        {
            const auto moved = draggingModule;
            draggingModule = {};

            for (auto& m : modules)
            {
                if (m.path == moved && m.bounds.getPosition() != dragOriginal.getPosition())
                {
                    std::map<juce::String, juce::Rectangle<float>> positions;
                    positions[m.name] = { m.bounds.getX() / gridSize, m.bounds.getY() / gridSize,
                                          m.bounds.getWidth() / gridSize, m.bounds.getHeight() / gridSize };
                    context.controller.setModulePositions (blockPath, positions);
                }
            }
            layout();
        }
    }

    void ModuleGraph::mouseDoubleClick (const juce::MouseEvent& e)
    {
        if (auto* module = moduleAt (e.position))
            selectModule (module->path);
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
        if (selectedLink.isNotEmpty())
        {
            context.controller.remove (selectedLink);
            selectedLink = {};
        }
        else if (selectedModule.isNotEmpty())
        {
            context.controller.remove (selectedModule);
            selectModule ({});
        }
    }

    void ModuleGraph::showModuleMenu (const ModuleBox& module)
    {
        juce::PopupMenu menu;
        const auto path = module.path;
        menu.addItem ("Rename...", [this, path]
        {
            auto* w = new juce::AlertWindow ("Rename module", "New name:", juce::MessageBoxIconType::NoIcon);
            w->addTextEditor ("name", lastElement (path));
            w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, path] (int result)
            {
                if (result == 1)
                {
                    auto r = context.controller.rename (path, w->getTextEditorContents ("name"));
                    if (r.failed())
                        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Rename", r.getErrorMessage());
                    else
                        selectModule (parentPath (path) + "." + w->getTextEditorContents ("name").trim());
                }
            }), true);
        });
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

    void ModuleGraph::itemDropped (const SourceDetails& details)
    {
        auto guidText = details.description.toString().fromFirstOccurrenceOf (ModuleLibrary::dragPrefix, false, false);
        GUID guid;
        if (CGuidHelper::string_to_guid (toStd (guidText), guid) != CError::SUCCESS)
            return;

        auto position = details.localPosition.toFloat() / gridSize;
        position = { juce::jmax (0.0f, std::round (position.x * 2.0f) / 2.0f), juce::jmax (0.0f, std::round (position.y * 2.0f) / 2.0f) };
        auto module = context.controller.addModule (blockPath, guid, position);
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
        breadcrumb.setBounds (top);

        library.setBounds (area.removeFromLeft (220));

        juce::Component* comps[] = { &graphViewport, divider.get(), &properties };
        verticalLayout.layOutComponents (comps, 3, area.getX(), area.getY(), area.getWidth(), area.getHeight(), true, true);
        graph.refreshLayout();
    }
}
