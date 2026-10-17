#include "EndpointsPage.h"

namespace modulecreator
{
    namespace
    {
        juce::String describe (const Endpoint& e)
        {
            if (! e.isControl)
                return e.isInputStream ? "audio in" : "audio out";
            if (! e.isState)
                return "bang";
            return toXmlName (e.stateType);
        }

        void fillCombo (juce::ComboBox& box, juce::StringArray items)
        {
            box.clear (juce::dontSendNotification);
            box.addItemList (items, 1);
        }

        void setComboIndex (juce::ComboBox& box, int index)
        {
            if (box.getSelectedItemIndex() != index)
                box.setSelectedItemIndex (index, juce::dontSendNotification);
        }

        const juce::StringArray scaleNames { "linear", "exponential", "decibel" };
    }

    //==============================================================================
    EndpointEditor::EndpointEditor (Document& d) : document (d)
    {
        for (auto* c : std::initializer_list<juce::Component*> {
                 &nameLabel, &labelLabel, &descriptionLabel, &typeLabel, &directionLabel, &controlTypeLabel, &stateTypeLabel,
                 &constraintLabel, &minimumLabel, &maximumLabel, &allowedLabel, &defaultLabel, &scaleLabel, &stateLabelsLabel,
                 &widgetLabel, &widgetNameLabel, &savedLabel, &inputFileLabel, &sourceLabel, &targetLabel, &sentToHostLabel,
                 &name, &label, &description, &minimum, &maximum, &defaultValue, &widgetName,
                 &type, &direction, &controlType, &stateType, &constraint, &scale,
                 &allowedValues, &stateLabels, &widgetButton, &advancedButton,
                 &saved, &inputFile, &source, &target, &sentToHost, &issues })
            addChildComponent (c);

        for (auto* e : { &name, &label, &minimum, &maximum, &defaultValue, &widgetName })
            styleEditor (*e);
        styleEditor (description, true);

        // ids for the GUI test
        for (auto [c, id] : std::initializer_list<std::pair<juce::Component*, const char*>> {
                 { &name, "name" }, { &label, "label" }, { &description, "description" }, { &minimum, "minimum" }, { &maximum, "maximum" },
                 { &defaultValue, "default" }, { &widgetName, "widgetLabel" }, { &type, "type" }, { &direction, "direction" },
                 { &controlType, "controlType" }, { &stateType, "stateType" }, { &constraint, "constraint" }, { &scale, "scale" } })
            c->setComponentID (id);

        name.setInputRestrictions (maximumNameLength, lettersAndDigits);
        name.setTooltip ("The endpoint's name, in lowerCamelCase");
        name.onTextChange = [this] { edit ([this] (Endpoint& e) { e.name = name.getText(); }); };
        label.setInputRestrictions (0, printableCharacters);
        label.setTooltip ("Easy to read version of name");
        label.onTextChange = [this] { edit ([this] (Endpoint& e) { e.label = label.getText(); }); };
        description.setInputRestrictions (0, printableCharacters);
        description.setTooltip ("Describe the endpoint, in plain text or 'markdown'");
        description.onTextChange = [this] { edit ([this] (Endpoint& e) { e.description = description.getText(); }); };

        fillCombo (type, { "Control", "Stream" });
        type.onChange = [this]
        {
            // streams have no widgets
            if (type.getSelectedItemIndex() != 0)
                document.definition.unassignWidget (uid);
            edit ([this] (Endpoint& e) { e.isControl = type.getSelectedItemIndex() == 0; });
        };
        fillCombo (direction, { "Audio Input", "Audio Output" });
        direction.onChange = [this] { edit ([this] (Endpoint& e) { e.isInputStream = direction.getSelectedItemIndex() == 0; }); };
        fillCombo (controlType, { "State", "Bang" });
        controlType.onChange = [this] { edit ([this] (Endpoint& e) { e.isState = controlType.getSelectedItemIndex() == 0; }); };
        fillCombo (stateType, { "Float", "Integer", "String" });
        stateType.onChange = [this] { edit ([this] (Endpoint& e) { e.stateType = (StateType) stateType.getSelectedItemIndex(); }); };
        fillCombo (constraint, { "Range", "Allowed Values" });
        constraint.onChange = [this] { edit ([this] (Endpoint& e) { e.useRange = constraint.getSelectedItemIndex() == 0; }); };
        fillCombo (scale, { "Linear", "Exponential", "Decibel" });
        scale.onChange = [this] { edit ([this] (Endpoint& e) { e.scale = scaleNames[scale.getSelectedItemIndex()]; }); };

        minimum.onTextChange = [this] { edit ([this] (Endpoint& e) { e.minimum = minimum.getText(); }); };
        maximum.onTextChange = [this] { edit ([this] (Endpoint& e) { e.maximum = maximum.getText(); }); };
        defaultValue.onTextChange = [this] { edit ([this] (Endpoint& e) { e.defaultValue = defaultValue.getText(); }); };

        allowedValues.onAdd = [this]
        {
            edit ([] (Endpoint& e)
            {
                int n = 1;
                const bool numeric = e.stateType != StateType::stringType;
                auto candidate = [&] { return numeric ? juce::String (n) : "value" + juce::String (n); };
                while (e.allowedStates.contains (candidate()))
                    ++n;
                e.allowedStates.add (candidate());
            });
            if (auto* e = endpoint())
                allowedValues.focusRow (e->allowedStates.size() - 1);
        };
        allowedValues.onRemove = [this] (int row) { edit ([row] (Endpoint& e) { e.allowedStates.remove (row); }); };
        allowedValues.onEdit = [this] (int row, int, const juce::String& text) { edit ([row, text] (Endpoint& e) { e.allowedStates.set (row, text); }); };

        stateLabels.onAdd = [this]
        {
            edit ([] (Endpoint& e) { e.stateLabels.push_back ({ e.defaultValue, "label" }); });
            if (auto* e = endpoint())
                stateLabels.focusRow ((int) e->stateLabels.size() - 1);
        };
        stateLabels.onRemove = [this] (int row) { edit ([row] (Endpoint& e) { e.stateLabels.erase (e.stateLabels.begin() + row); }); };
        stateLabels.onEdit = [this] (int row, int column, const juce::String& text)
        {
            edit ([row, column, text] (Endpoint& e) { (column == 0 ? e.stateLabels[(size_t) row].value : e.stateLabels[(size_t) row].text) = text; });
        };

        widgetButton.setTooltip ("The widget that shows this endpoint in Integra Live's module properties");
        widgetButton.onClick = [this] { showWidgetMenu(); };
        widgetName.setInputRestrictions (maximumNameLength, lettersAndDigits);
        widgetName.setTooltip ("Widgets shared by several endpoints need their own lowerCamelCase label");
        widgetName.onTextChange = [this]
        {
            if (auto* w = document.definition.widgetFor (uid); w != nullptr && w->mappings.size() > 1)
            {
                w->ownLabel = widgetName.getText();
                document.changed();
            }
        };

        advancedButton.onClick = [this]
        {
            showAdvanced = advancedButton.getToggleState();
            refresh();
            if (auto* p = getParentComponent())
                p->resized();
        };

        saved.setTooltip ("Tick if this endpoint's value is saved in projects");
        saved.onClick = [this] { edit ([this] (Endpoint& e) { e.isSavedToFile = saved.getToggleState(); }); };
        inputFile.setTooltip ("Tick if this endpoint names a file the module reads, which is copied into the project's data directory");
        inputFile.onClick = [this] { edit ([this] (Endpoint& e) { e.isInputFile = inputFile.getToggleState(); }); };
        source.setTooltip ("Tick if this Endpoint can be a source, EG for Connections or Scripts");
        source.onClick = [this] { edit ([this] (Endpoint& e) { e.canBeSource = source.getToggleState(); }); };
        target.setTooltip ("Tick if this Endpoint can be a target, EG for Connections or Scripts");
        target.onClick = [this] { edit ([this] (Endpoint& e) { e.canBeTarget = target.getToggleState(); }); };
        sentToHost.setTooltip ("Tick if this Endpoint should be sent to the host");
        sentToHost.onClick = [this] { edit ([this] (Endpoint& e) { e.isSentToHost = sentToHost.getToggleState(); }); };
    }

    void EndpointEditor::edit (const std::function<void (Endpoint&)>& change)
    {
        if (auto* e = endpoint())
        {
            change (*e);
            document.changed();
        }
    }

    void EndpointEditor::setEndpoint (int newUid)
    {
        uid = newUid;
        refresh();
    }

    void EndpointEditor::updateRestrictions (const Endpoint& e)
    {
        // Range.stateType / StateInfo.propogateStateType
        const bool isString = e.stateType == StateType::stringType;
        const auto rangeCharacters = isString ? juce::String ("0123456789")
                                              : (e.stateType == StateType::integerType ? integerCharacters : numberCharacters);
        minimum.setInputRestrictions (0, rangeCharacters);
        maximum.setInputRestrictions (0, rangeCharacters);
        minimumLabel.setText (isString ? "Minimum Length" : "Minimum Value", juce::dontSendNotification);
        maximumLabel.setText (isString ? "Maximum Length" : "Maximum Value", juce::dontSendNotification);
        const auto valueCharacters = isString ? juce::String() : rangeCharacters;
        defaultValue.setInputRestrictions (0, valueCharacters);
        allowedValues.allowedCharacters = valueCharacters;
    }

    void EndpointEditor::refresh()
    {
        auto* e = endpoint();
        if (e == nullptr)
        {
            for (auto* c : getChildren())
                c->setVisible (false);
            return;
        }

        setTextIfDifferent (name, e->name);
        setTextIfDifferent (label, e->label);
        setTextIfDifferent (description, e->description);
        setComboIndex (type, e->isControl ? 0 : 1);
        setComboIndex (direction, e->isInputStream ? 0 : 1);
        setComboIndex (controlType, e->isState ? 0 : 1);
        setComboIndex (stateType, (int) e->stateType);
        setComboIndex (constraint, e->useRange ? 0 : 1);
        setComboIndex (scale, juce::jmax (0, scaleNames.indexOf (e->scale)));
        updateRestrictions (*e);
        setTextIfDifferent (minimum, e->minimum);
        setTextIfDifferent (maximum, e->maximum);
        setTextIfDifferent (defaultValue, e->defaultValue);

        std::vector<juce::StringArray> values;
        for (auto& v : e->allowedStates)
            values.push_back (juce::StringArray (v));
        allowedValues.setRows (values);
        std::vector<juce::StringArray> labels;
        for (auto& l : e->stateLabels)
            labels.push_back (juce::StringArray (l.value, l.text));
        stateLabels.setRows (labels);

        auto* widget = document.definition.widgetFor (uid);
        if (widget == nullptr)
            widgetButton.setButtonText ("No Widget");
        else if (document.config.findWidget (widget->type) != nullptr && document.config.findWidget (widget->type)->attributes.size() > 1)
            widgetButton.setButtonText (widget->type + " (" + widget->attributeFor (uid) + ")"
                                        + (widget->mappings.size() > 1 ? ", shared" : ""));
        else
            widgetButton.setButtonText (widget->type);
        setTextIfDifferent (widgetName, widget != nullptr ? document.definition.labelOf (*widget) : juce::String());
        widgetName.setEnabled (widget != nullptr && widget->mappings.size() > 1);

        saved.setToggleState (e->isSavedToFile, juce::dontSendNotification);
        inputFile.setToggleState (e->isInputFile, juce::dontSendNotification);
        source.setToggleState (e->canBeSource, juce::dontSendNotification);
        target.setToggleState (e->canBeTarget, juce::dontSendNotification);
        sentToHost.setToggleState (e->isSentToHost, juce::dontSendNotification);
        advancedButton.setToggleState (showAdvanced, juce::dontSendNotification);

        issues.setIssues (document.issuesFor (ValidationIssue::Page::endpoints, document.definition.indexOfEndpoint (uid)));
        layout (getWidth());
    }

    int EndpointEditor::layout (int width)
    {
        auto* e = endpoint();
        if (e == nullptr)
            return 0;

        // only what this endpoint uses is visible; nothing is hidden and reshown, which
        // would take the keyboard focus away from the field being typed in
        FormLayout form { { 0, 8, juce::jmax (300, width - 8), 10000 } };
        juce::Array<juce::Component*> shown;
        auto show = [&shown] (juce::Component& c, juce::Rectangle<int> r) { c.setBounds (r); shown.add (&c); };
        auto showRow = [&] (FormLabel& l, juce::Component& c, int w = 220, int h = rowHeight)
        {
            shown.add (&l);
            show (c, form.row (&l, h).removeFromLeft (w));
        };

        showRow (nameLabel, name);
        showRow (labelLabel, label);
        showRow (descriptionLabel, description, 10000, 80);
        showRow (typeLabel, type, 160);

        if (! e->isControl)
        {
            showRow (directionLabel, direction, 160);
        }
        else
        {
            showRow (controlTypeLabel, controlType, 160);
            if (e->isState)
            {
                showRow (stateTypeLabel, stateType, 160);
                showRow (constraintLabel, constraint, 160);
                if (e->useRange)
                {
                    showRow (minimumLabel, minimum, 160);
                    showRow (maximumLabel, maximum, 160);
                }
                else
                {
                    showRow (allowedLabel, allowedValues, 220, allowedValues.getIdealHeight());
                }
                showRow (defaultLabel, defaultValue, 160);
                if (e->hasScale())
                    showRow (scaleLabel, scale, 160);
                showRow (stateLabelsLabel, stateLabels, 320, stateLabels.getIdealHeight());
            }

            showRow (widgetLabel, widgetButton, 260);
            showRow (widgetNameLabel, widgetName);

            show (advancedButton, form.row (nullptr).removeFromLeft (220));
            if (showAdvanced)
            {
                if (e->isState)
                {
                    showRow (savedLabel, saved, 40);
                    showRow (inputFileLabel, inputFile, 40);
                }
                showRow (sourceLabel, source, 40);
                showRow (targetLabel, target, 40);
                if (document.developerMode)
                    showRow (sentToHostLabel, sentToHost, 40);
            }
        }

        const int issueHeight = issues.getIdealHeight (form.area.getWidth() - labelWidth);
        if (issueHeight > 0)
            show (issues, form.row (nullptr, issueHeight));

        for (auto* c : getChildren())
            c->setVisible (shown.contains (c));
        return form.area.getY() + 8;
    }

    void EndpointEditor::showWidgetMenu()
    {
        auto& d = document.definition;
        const auto* current = d.widgetFor (uid);

        struct Choice { juce::String type, attribute; int shareWith = 0; };
        std::vector<Choice> choices { {} };     // id 1: no widget
        juce::PopupMenu menu;
        menu.addItem (1, "No Widget", true, current == nullptr);
        menu.addSeparator();

        for (auto& definition : document.config.widgets)
        {
            const bool isCurrentType = current != nullptr && current->type == definition.name;
            if (definition.attributes.size() == 1)
            {
                choices.push_back ({ definition.name, definition.attributes[0] });
                menu.addItem ((int) choices.size(), definition.name, true, isCurrentType);
                continue;
            }

            juce::PopupMenu attributes;
            for (auto& attribute : definition.attributes)
            {
                const bool isCurrentAttribute = isCurrentType && current->attributeFor (uid) == attribute;

                // WidgetSelection.getPotentialSharers: widgets of this type with another attribute in use
                juce::PopupMenu sharing;
                for (auto& w : d.widgets)
                {
                    if (w.type != definition.name)
                        continue;
                    juce::StringArray others;
                    for (auto& [a, other] : w.mappings)
                        if (a != attribute && other != uid)
                            if (auto* oe = d.findEndpoint (other))
                                others.add (oe->name);
                    if (others.isEmpty())
                        continue;
                    choices.push_back ({ definition.name, attribute, w.uid });
                    sharing.addItem ((int) choices.size(), "Share with " + others.joinIntoString (", "), true, isCurrentAttribute && current == &w);
                }

                if (sharing.getNumItems() > 0)
                {
                    choices.push_back ({ definition.name, attribute });
                    sharing.addItem ((int) choices.size(), "Use Separate Widget", true, isCurrentAttribute && current->mappings.size() == 1);
                    attributes.addSubMenu (attribute, sharing, true, nullptr, isCurrentAttribute);
                }
                else
                {
                    choices.push_back ({ definition.name, attribute });
                    attributes.addItem ((int) choices.size(), attribute, true, isCurrentAttribute);
                }
            }
            menu.addSubMenu (definition.name, attributes, true, nullptr, isCurrentType);
        }

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&widgetButton),
                            [safe = juce::Component::SafePointer<EndpointEditor> (this), choices, endpointUid = uid] (int result)
        {
            if (safe == nullptr || result <= 0 || result > (int) choices.size())
                return;
            auto& choice = choices[(size_t) result - 1];
            auto& doc = safe->document;
            juce::Point<int> size { 85, 85 };
            if (auto* w = doc.config.findWidget (choice.type))
                size = w->defaultSize;
            doc.definition.assignWidget (endpointUid, choice.type, choice.attribute, choice.shareWith, size);
            doc.changed();
        });
    }

    //==============================================================================
    EndpointsPage::EndpointsPage (Document& d) : Page (d), editor (d)
    {
        for (auto* l : { &endpointsHeading, &withoutWidgetsHeading })
        {
            l->setFont (juce::FontOptions (14.0f, juce::Font::bold));
            addAndMakeVisible (l);
        }
        for (auto* c : std::initializer_list<juce::Component*> { &list, &addButton, &removeButton, &upButton, &downButton, &sortButton,
                                                                 &withoutWidgets, &listIssues, &viewport })
            addAndMakeVisible (c);

        list.setRowHeight (22);
        list.setOutlineThickness (1);
        withoutWidgets.setModel (&withoutWidgetsModel);
        withoutWidgets.setRowHeight (20);
        withoutWidgets.setOutlineThickness (1);
        withoutWidgets.setTooltip ("Control endpoints that Integra Live users can only reach through scripts and routing: click to select one");

        addButton.setTooltip ("Add an endpoint");
        addButton.onClick = [this]
        {
            const int newUid = document.definition.addEndpoint().uid;
            document.changed();
            selectEndpoint (document.definition.indexOfEndpoint (newUid));
            editor.getNameEditor().grabKeyboardFocus();
            editor.getNameEditor().selectAll();
        };
        removeButton.onClick = [this]
        {
            const int row = list.getSelectedRow();
            if (row < 0 || row >= (int) document.definition.endpoints.size())
                return;
            edit ([row] (ModuleDefinition& m) { m.removeEndpoint (m.endpoints[(size_t) row].uid); });
            selectEndpoint (juce::jmin (row, (int) document.definition.endpoints.size() - 1));
        };
        upButton.onClick = [this] { moveSelected (-1); };
        downButton.onClick = [this] { moveSelected (1); };
        sortButton.setTooltip ("Streams first, then controls in the left-to-right order of their widgets");
        sortButton.onClick = [this]
        {
            const int selected = editor.getEndpoint();
            edit ([] (ModuleDefinition& m) { m.sortEndpoints(); });
            selectEndpoint (document.definition.indexOfEndpoint (selected));
        };

        viewport.setViewedComponent (&editor, false);
        viewport.setScrollBarsShown (true, false);
        refresh();
    }

    int EndpointsPage::getNumRows()     { return (int) document.definition.endpoints.size(); }

    void EndpointsPage::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
    {
        auto& endpoints = document.definition.endpoints;
        if (row < 0 || row >= (int) endpoints.size())
            return;
        auto& t = Theme::get();
        auto& e = endpoints[(size_t) row];
        if (selected)
            g.fillAll (t.selection);

        const bool hasIssues = ! document.issuesFor (ValidationIssue::Page::endpoints, row).empty();
        auto r = juce::Rectangle<int> (0, 0, width, height).reduced (6, 0);
        g.setFont (juce::FontOptions (13.0f));
        g.setColour (t.textDim);
        g.drawText (describe (e), r.removeFromRight (70), juce::Justification::centredRight);
        g.setColour (hasIssues ? juce::Colour (0xffd04040) : t.text);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText ((hasIssues ? juce::String::fromUTF8 ("\xe2\x9a\xa0 ") : juce::String()) + (e.name.isEmpty() ? juce::String ("(no name)") : e.name),
                    r, juce::Justification::centredLeft, true);
    }

    void EndpointsPage::selectedRowsChanged (int row)
    {
        auto& endpoints = document.definition.endpoints;
        editor.setEndpoint (row >= 0 && row < (int) endpoints.size() ? endpoints[(size_t) row].uid : 0);
        resized();
        updateButtons();
    }

    void EndpointsPage::selectEndpoint (int index)
    {
        if (index >= 0)
            list.selectRow (index);
        else
            list.deselectAllRows();
        selectedRowsChanged (index);
    }

    void EndpointsPage::moveSelected (int delta)
    {
        const int row = list.getSelectedRow();
        const int to = row + delta;
        auto& endpoints = document.definition.endpoints;
        if (row < 0 || to < 0 || to >= (int) endpoints.size())
            return;
        edit ([row, to] (ModuleDefinition& m) { std::swap (m.endpoints[(size_t) row], m.endpoints[(size_t) to]); });
        selectEndpoint (to);
    }

    void EndpointsPage::updateButtons()
    {
        const int row = list.getSelectedRow();
        const int n = (int) document.definition.endpoints.size();
        removeButton.setEnabled (row >= 0);
        upButton.setEnabled (row > 0);
        downButton.setEnabled (row >= 0 && row < n - 1);
        sortButton.setEnabled (n > 1);
    }

    void EndpointsPage::refresh()
    {
        list.updateContent();
        list.repaint();
        withoutWidgetsModel.names = document.definition.controlsWithoutWidgets();
        withoutWidgets.updateContent();
        withoutWidgets.repaint();

        // the selection follows the endpoint (sorting and removing move rows)
        const int index = document.definition.indexOfEndpoint (editor.getEndpoint());
        if (index != list.getSelectedRow())
        {
            if (index >= 0)
                list.selectRow (index, false, true);
            else if (! document.definition.endpoints.empty())
                list.selectRow (0, false, true);
            else
                list.deselectAllRows();
        }
        if (editor.getEndpoint() == 0 && list.getSelectedRow() >= 0)
            editor.setEndpoint (document.definition.endpoints[(size_t) list.getSelectedRow()].uid);
        else
            editor.refresh();

        listIssues.setIssues (document.issuesFor (ValidationIssue::Page::endpoints, -1));
        updateButtons();
        resized();
    }

    void EndpointsPage::resized()
    {
        auto r = getLocalBounds().reduced (8);
        auto left = r.removeFromLeft (260);
        r.removeFromLeft (12);

        endpointsHeading.setBounds (left.removeFromTop (22));
        auto withoutArea = left.removeFromBottom (juce::jlimit (60, 160, 24 + withoutWidgetsModel.names.size() * 20));
        withoutWidgetsHeading.setBounds (withoutArea.removeFromTop (22));
        withoutWidgets.setBounds (withoutArea);
        withoutWidgetsHeading.setVisible (! withoutWidgetsModel.names.isEmpty());
        withoutWidgets.setVisible (! withoutWidgetsModel.names.isEmpty());
        left.removeFromBottom (8);

        // EndpointList.validateMyOwnContent errors (only those not about one endpoint)
        std::vector<ValidationIssue> own;
        for (auto& i : document.issuesFor (ValidationIssue::Page::endpoints))
            if (i.endpointIndex < 0)
                own.push_back (i);
        listIssues.setIssues (own);
        const int issueHeight = listIssues.getIdealHeight (left.getWidth());
        listIssues.setBounds (left.removeFromBottom (issueHeight));
        listIssues.setVisible (issueHeight > 0);

        auto buttons2 = left.removeFromBottom (rowHeight);
        left.removeFromBottom (4);
        auto buttons1 = left.removeFromBottom (rowHeight);
        left.removeFromBottom (6);
        list.setBounds (left);

        addButton.setBounds (buttons1.removeFromLeft (buttons1.getWidth() / 2).withTrimmedRight (2));
        removeButton.setBounds (buttons1.withTrimmedLeft (2));
        const int w = buttons2.getWidth() / 3;
        upButton.setBounds (buttons2.removeFromLeft (w).withTrimmedRight (2));
        downButton.setBounds (buttons2.removeFromLeft (w).withTrimmedRight (2));
        sortButton.setBounds (buttons2);

        viewport.setBounds (r);
        const int width = viewport.getMaximumVisibleWidth();
        editor.setSize (width, juce::jmax (r.getHeight(), editor.layout (width)));
    }

    void EndpointsPage::WithoutWidgetsModel::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool)
    {
        g.setColour (Theme::get().text);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (names[row], 6, 0, width - 12, height, juce::Justification::centredLeft);
    }

    void EndpointsPage::WithoutWidgetsModel::listBoxItemClicked (int row, const juce::MouseEvent&)
    {
        if (auto* e = owner.document.definition.findEndpointNamed (names[row]))
            owner.selectEndpoint (owner.document.definition.indexOfEndpoint (e->uid));
    }
}
