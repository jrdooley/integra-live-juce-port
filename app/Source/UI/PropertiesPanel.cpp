#include "PropertiesPanel.h"
#include "IntegraLookAndFeel.h"

namespace integra
{
    //==============================================================================
    juce::AttributedString renderMarkdown (const juce::String& markdown, juce::Colour text, juce::Colour accent)
    {
        juce::AttributedString out;
        out.setWordWrap (juce::AttributedString::byWord);

        auto appendInline = [&] (const juce::String& line, float size, bool forceBold)
        {
            // **bold**, *italic*, [text](link)
            int i = 0;
            bool bold = forceBold, italic = false;
            juce::String run;
            auto flush = [&]
            {
                if (run.isEmpty())
                    return;
                juce::FontOptions options (size, (bold ? juce::Font::bold : 0) | (italic ? juce::Font::italic : 0));
                out.append (run, juce::Font (options), text);
                run.clear();
            };

            while (i < line.length())
            {
                if (line.substring (i).startsWith ("**"))         { flush(); bold = forceBold || ! bold; i += 2; continue; }
                if (line[i] == '*' || line[i] == '_')             { flush(); italic = ! italic; ++i; continue; }
                if (line[i] == '[')
                {
                    const int close = line.indexOf (i, "](");
                    const int end = close > 0 ? line.indexOf (close, ")") : -1;
                    if (close > 0 && end > 0)
                    {
                        flush();
                        out.append (line.substring (i + 1, close), juce::Font (juce::FontOptions (size, juce::Font::underlined)), accent);
                        i = end + 1;
                        continue;
                    }
                }
                run << juce::String::charToString (line[i]);
                ++i;
            }
            flush();
        };

        for (auto line : juce::StringArray::fromLines (markdown))
        {
            auto trimmed = line.trimStart();
            if (trimmed.startsWith ("### "))        appendInline (trimmed.substring (4), 14.0f, true);
            else if (trimmed.startsWith ("## "))    appendInline (trimmed.substring (3), 16.0f, true);
            else if (trimmed.startsWith ("# "))     appendInline (trimmed.substring (2), 19.0f, true);
            else if (trimmed.startsWith ("- ") || trimmed.startsWith ("* "))
            {
                out.append (juce::String::fromUTF8 ("  \xe2\x80\xa2 "), juce::Font (juce::FontOptions (13.0f)), text);
                appendInline (trimmed.substring (2), 13.0f, false);
            }
            else                                    appendInline (line, 13.0f, false);

            out.append ("\n", juce::Font (juce::FontOptions (13.0f)), text);
        }

        return out;
    }

    juce::StringArray findRoutableEndpoints (IntegraSession& session, const juce::String& containerPath, bool sources)
    {
        static const juce::StringArray excludedTypes { "Connection", "Scaler", "Envelope", "ControlPoint", "MidiControlInput", "MidiRawInput" };
        static const juce::StringArray excludedEndpoints { "userData", "info", "zIndex" };

        juce::StringArray result;
        auto server = session.lock();
        auto* container = server->find_node (toPath (containerPath));
        if (container == nullptr)
            return result;

        std::function<void (const INode&)> visit = [&] (const INode& node)
        {
            const auto type = toString (node.get_interface_definition().get_interface_info().get_name());
            if (excludedTypes.contains (type))
                return;

            if (&node != container)
            {
                const auto nodePath = toString (node.get_path());
                for (auto* ep : node.get_interface_definition().get_endpoint_definitions())
                {
                    auto* control = ep->get_control_info();
                    if (control == nullptr)
                        continue;
                    const auto name = toString (ep->get_name());
                    if (excludedEndpoints.contains (name))
                        continue;
                    if (sources ? control->get_can_be_source() : control->get_can_be_target())
                        result.add (nodePath + "." + name);
                }
            }

            for (auto& child : node.get_children())
                visit (*child.second);
        };

        visit (*container);
        result.sortNatural();
        return result;
    }

    namespace
    {
        juce::String relative (const juce::String& container, const juce::String& path)
        {
            return path.startsWith (container + ".") ? path.substring (container.length() + 1) : path;
        }

        /** Combo box listing endpoint paths relative to a container. */
        void fillEndpointCombo (juce::ComboBox& box, const juce::StringArray& endpoints, const juce::String& container, const juce::String& selected)
        {
            box.clear (juce::dontSendNotification);
            box.addItem ("(none)", 1);
            int id = 2;
            for (auto& e : endpoints)
            {
                box.addItem (relative (container, e), id);
                if (e == selected)
                    box.setSelectedId (id, juce::dontSendNotification);
                ++id;
            }
            if (box.getSelectedId() == 0)
                box.setSelectedId (1, juce::dontSendNotification);
        }

        juce::String comboEndpoint (const juce::ComboBox& box, const juce::StringArray& endpoints)
        {
            const int index = box.getSelectedId() - 2;
            return juce::isPositiveAndBelow (index, endpoints.size()) ? endpoints[index] : juce::String();
        }

        /** A list of rows in a viewport with an "add" button underneath. */
        class RowList  : public juce::Component
        {
        public:
            RowList()
            {
                viewport.setViewedComponent (&content, false);
                viewport.setScrollBarsShown (true, false);
                addAndMakeVisible (viewport);
                addAndMakeVisible (addButton);
            }

            void setRows (juce::OwnedArray<juce::Component>&& newRows, int rowHeight)
            {
                rows.clear();
                rows.swapWith (newRows);
                int y = 0;
                for (auto* r : rows)
                {
                    content.addAndMakeVisible (r);
                    r->setBounds (0, y, viewport.getMaximumVisibleWidth(), rowHeight);
                    y += rowHeight;
                }
                height = rowHeight;
                content.setSize (viewport.getMaximumVisibleWidth(), y);
            }

            void resized() override
            {
                auto area = getLocalBounds().reduced (4);
                addButton.setBounds (area.removeFromBottom (26).removeFromLeft (180));
                area.removeFromBottom (4);
                viewport.setBounds (area);
                int y = 0;
                for (auto* r : rows)
                {
                    r->setBounds (0, y, viewport.getMaximumVisibleWidth(), height);
                    y += height;
                }
                content.setSize (viewport.getMaximumVisibleWidth(), y);
            }

            juce::TextButton addButton;

        private:
            juce::Viewport viewport;
            juce::Component content;
            juce::OwnedArray<juce::Component> rows;
            int height = 30;
        };
    }

    //==============================================================================
    class PropertiesPanel::InfoTab  : public juce::Component
    {
    public:
        explicit InfoTab (AppContext& c) : context (c)
        {
            editButton.setClickingTogglesState (true);
            editButton.onClick = [this] { setEditing (editButton.getToggleState()); };
            addAndMakeVisible (editButton);

            editor.setMultiLine (true, true);
            editor.setReturnKeyStartsNewLine (true);
            editor.setFont (juce::FontOptions (13.0f));
            addChildComponent (editor);
        }

        void setTarget (const juce::String& path)
        {
            if (editButton.getToggleState())
                setEditing (false);
            target = path;
            markdown = path.isNotEmpty() ? context.session.getValue (path + ".info").toString() : juce::String();
            repaint();
        }

        void refresh()
        {
            if (! editButton.getToggleState())
                setTarget (target);
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (Theme::get().panel);
            if (! editButton.getToggleState())
            {
                auto text = renderMarkdown (markdown.isEmpty() ? juce::String ("*No info - click Edit to describe this item.*") : markdown,
                                            Theme::get().text, Theme::get().accent);
                text.draw (g, getLocalBounds().reduced (12, 8).withTrimmedRight (80).toFloat());
            }
        }

        void resized() override
        {
            editButton.setBounds (getWidth() - 76, 6, 68, 24);
            editor.setBounds (getLocalBounds().reduced (8).withTrimmedRight (80));
        }

    private:
        void setEditing (bool editing)
        {
            if (editing)
            {
                editor.setText (markdown, false);
                editor.setVisible (true);
                editor.grabKeyboardFocus();
                editButton.setButtonText ("Done");
            }
            else
            {
                editor.setVisible (false);
                editButton.setButtonText ("Edit");
                editButton.setToggleState (false, juce::dontSendNotification);
                if (target.isNotEmpty() && editor.getText() != markdown)
                {
                    markdown = editor.getText();
                    context.controller.setInfo (target, markdown);
                }
            }
            repaint();
        }

        AppContext& context;
        juce::String target, markdown;
        juce::TextButton editButton { "Edit" };
        juce::TextEditor editor;
    };

    //==============================================================================
    class PropertiesPanel::RoutingTab  : public juce::Component
    {
    public:
        explicit RoutingTab (AppContext& c) : context (c)
        {
            list.addButton.setButtonText ("+ Add routing");
            list.addButton.onClick = [this]
            {
                if (container.isNotEmpty())
                    context.controller.addRouting (container, {}, {});
            };
            addAndMakeVisible (list);
        }

        void setTarget (const juce::String& path)
        {
            container = path;
            refresh();
        }

        void refresh()
        {
            juce::OwnedArray<juce::Component> rows;
            auto* c = context.model.get().findContainer (container);
            if (c != nullptr)
            {
                const auto sources = findRoutableEndpoints (context.session, container, true);
                const auto targets = findRoutableEndpoints (context.session, container, false);
                for (auto& s : c->scalers)
                {
                    // MIDI routings are shown in the MIDI tab
                    if (s.sourcePath.endsWith (".value") && context.session.getModuleName (parentPath (s.sourcePath)) == "MidiControlInput")
                        continue;
                    rows.add (new Row (context, container, s, sources, targets));
                }
            }
            list.setRows (std::move (rows), 34);
        }

        void resized() override       { list.setBounds (getLocalBounds()); }
        void paint (juce::Graphics& g) override { g.fillAll (Theme::get().panel); }

    private:
        class Row  : public juce::Component
        {
        public:
            Row (AppContext& c, const juce::String& containerPath, const model::Scaler& s,
                 const juce::StringArray& src, const juce::StringArray& dst)
                : context (c), scaler (s.path), sources (src), targets (dst)
            {
                fillEndpointCombo (source, sources, containerPath, s.sourcePath);
                fillEndpointCombo (target, targets, containerPath, s.targetPath);
                source.onChange = [this] { context.controller.setRoutingSource (scaler, comboEndpoint (source, sources)); };
                target.onChange = [this] { context.controller.setRoutingTarget (scaler, comboEndpoint (target, targets)); };

                auto setupNumber = [this] (juce::Label& l, float value, const char* endpoint)
                {
                    l.setText (juce::String (value, 3), juce::dontSendNotification);
                    l.setEditable (true);
                    l.setColour (juce::Label::outlineColourId, Theme::get().outline);
                    l.setJustificationType (juce::Justification::centred);
                    l.onTextChange = [this, &l, endpoint] { context.controller.setValue (scaler + "." + endpoint, l.getText().getFloatValue()); };
                    addAndMakeVisible (l);
                };
                setupNumber (inMin, s.inMin, "inRangeMin");
                setupNumber (inMax, s.inMax, "inRangeMax");
                setupNumber (outMin, s.outMin, "outRangeMin");
                setupNumber (outMax, s.outMax, "outRangeMax");

                for (auto* scale : { &inScale, &outScale })
                {
                    scale->addItemList ({ "linear", "exponential", "decibel" }, 1);
                    addAndMakeVisible (scale);
                }
                inScale.setText (s.inScale, juce::dontSendNotification);
                outScale.setText (s.outScale, juce::dontSendNotification);
                inScale.onChange = [this] { context.controller.setValue (scaler + ".inScale", inScale.getText()); };
                outScale.onChange = [this] { context.controller.setValue (scaler + ".outScale", outScale.getText()); };

                remove.onClick = [this] { context.controller.remove (scaler); };
                addAndMakeVisible (source);
                addAndMakeVisible (target);
                addAndMakeVisible (remove);
            }

            void paint (juce::Graphics& g) override
            {
                g.setColour (Theme::get().textDim);
                g.setFont (juce::FontOptions (12.0f));
                g.drawText ("->", arrowArea, juce::Justification::centred);
                g.setColour (Theme::get().outline.withAlpha (0.4f));
                g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
            }

            void resized() override
            {
                auto area = getLocalBounds().reduced (2, 4);
                remove.setBounds (area.removeFromRight (28));
                area.removeFromRight (6);
                const int w = area.getWidth();
                const int combo = juce::jmax (120, (w - 380) / 2);
                source.setBounds (area.removeFromLeft (combo));
                inMin.setBounds (area.removeFromLeft (52).reduced (2, 0));
                inMax.setBounds (area.removeFromLeft (52).reduced (2, 0));
                inScale.setBounds (area.removeFromLeft (80).reduced (2, 0));
                arrowArea = area.removeFromLeft (24);
                target.setBounds (area.removeFromLeft (combo));
                outMin.setBounds (area.removeFromLeft (52).reduced (2, 0));
                outMax.setBounds (area.removeFromLeft (52).reduced (2, 0));
                outScale.setBounds (area.removeFromLeft (80).reduced (2, 0));
            }

        private:
            AppContext& context;
            juce::String scaler;
            juce::StringArray sources, targets;
            juce::ComboBox source, target, inScale, outScale;
            juce::Label inMin, inMax, outMin, outMax;
            juce::TextButton remove { "x" };
            juce::Rectangle<int> arrowArea;
        };

        AppContext& context;
        juce::String container;
        RowList list;
    };

    //==============================================================================
    class PropertiesPanel::ScriptingTab  : public juce::Component
    {
    public:
        explicit ScriptingTab (AppContext& c) : context (c)
        {
            scripts.onChange = [this] { showScript (scripts.getText()); };
            addAndMakeVisible (scripts);

            addButton.onClick = [this]
            {
                if (container.isEmpty())
                    return;
                storeEdits();
                auto script = context.controller.addScript (container);
                pendingSelection = script;
            };
            removeButton.onClick = [this] { if (current.isNotEmpty()) context.controller.remove (current); };
            runButton.onClick = [this]
            {
                storeEdits();
                if (current.isNotEmpty())
                    context.controller.runScript (current);
            };
            applyButton.onClick = [this] { storeEdits(); };
            for (auto* b : { &addButton, &removeButton, &runButton, &applyButton })
                addAndMakeVisible (b);

            editor.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
            addAndMakeVisible (editor);

            help.setText ("Lua 5.2. Sibling objects are globals: TapDelay1.delayTime = 0.5", juce::dontSendNotification);
            help.setColour (juce::Label::textColourId, Theme::get().textDim);
            help.setFont (juce::FontOptions (11.0f));
            addAndMakeVisible (help);
        }

        void setTarget (const juce::String& path)
        {
            storeEdits();
            container = path;
            current = {};
            refresh();
        }

        void refresh()
        {
            auto* c = context.model.get().findContainer (container);
            scripts.clear (juce::dontSendNotification);
            int id = 1;
            juce::String select = pendingSelection.isNotEmpty() ? pendingSelection : current;
            pendingSelection = {};
            if (c != nullptr)
            {
                for (auto& s : c->scripts)
                {
                    scripts.addItem (lastElement (s.path), id);
                    if (s.path == select || select.isEmpty())
                    {
                        scripts.setSelectedId (id, juce::dontSendNotification);
                        select = s.path;
                    }
                    ++id;
                }
            }

            if (select != current || ! context.session.nodeExists (current))
                showScript (scripts.getText());
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (4);
            auto bar = area.removeFromTop (28);
            scripts.setBounds (bar.removeFromLeft (180));
            bar.removeFromLeft (6);
            addButton.setBounds (bar.removeFromLeft (60));
            removeButton.setBounds (bar.removeFromLeft (70).withTrimmedLeft (4));
            runButton.setBounds (bar.removeFromRight (70));
            applyButton.setBounds (bar.removeFromRight (70).withTrimmedRight (4));
            help.setBounds (area.removeFromBottom (18));
            area.removeFromTop (4);
            editor.setBounds (area);
        }

        void paint (juce::Graphics& g) override { g.fillAll (Theme::get().panel); }

    private:
        void showScript (const juce::String& name)
        {
            storeEdits();
            current = name.isNotEmpty() ? container + "." + name : juce::String();
            document.replaceAllContent (current.isNotEmpty() ? context.session.getValue (current + ".text").toString() : juce::String());
            document.clearUndoHistory();
            document.setSavePoint();
            editor.setEnabled (current.isNotEmpty());
            removeButton.setEnabled (current.isNotEmpty());
            runButton.setEnabled (current.isNotEmpty());
        }

        void storeEdits()
        {
            if (current.isNotEmpty() && document.hasChangedSinceSavePoint() && context.session.nodeExists (current))
            {
                context.controller.setScriptText (current, document.getAllContent());
                document.setSavePoint();
            }
        }

        AppContext& context;
        juce::String container, current, pendingSelection;
        juce::ComboBox scripts;
        juce::TextButton addButton { "+ New" }, removeButton { "Delete" }, runButton { "Run" }, applyButton { "Apply" };
        juce::CodeDocument document;
        juce::LuaTokeniser tokeniser;
        juce::CodeEditorComponent editor { document, &tokeniser };
        juce::Label help;
    };

    //==============================================================================
    class PropertiesPanel::MidiTab  : public juce::Component
    {
    public:
        explicit MidiTab (AppContext& c) : context (c)
        {
            list.addButton.setButtonText ("+ Add MIDI control");
            list.addButton.onClick = [this]
            {
                if (container.isNotEmpty())
                    context.controller.addMidiControl (container, {}, false);
            };
            addAndMakeVisible (list);
        }

        void setTarget (const juce::String& path)
        {
            container = path;
            refresh();
        }

        void refresh()
        {
            juce::OwnedArray<juce::Component> rows;
            auto* c = context.model.get().findContainer (container);
            if (c != nullptr)
            {
                const auto targets = findRoutableEndpoints (context.session, container, false);
                juce::StringArray devices;
                if (context.model.get().midiSettingsPath.isNotEmpty())
                    devices = unpackStringList (context.session.getValue (context.model.get().midiSettingsPath + ".availableInputDevices").toString());
                devices.removeEmptyStrings();

                for (auto& m : c->midiControlInputs)
                {
                    const model::Scaler* scaler = nullptr;
                    for (auto& s : c->scalers)
                        if (s.sourcePath == m.path + ".value")
                            scaler = &s;
                    rows.add (new Row (context, container, m, scaler, targets, devices));
                }
            }
            list.setRows (std::move (rows), 34);
        }

        void resized() override       { list.setBounds (getLocalBounds()); }
        void paint (juce::Graphics& g) override { g.fillAll (Theme::get().panel); }

    private:
        class Row  : public juce::Component
        {
        public:
            Row (AppContext& c, const juce::String& containerPath, const model::MidiControlInput& m, const model::Scaler* s,
                 const juce::StringArray& t, const juce::StringArray& devices)
                : context (c), midi (m.path), scaler (s != nullptr ? s->path : juce::String()), targets (t)
            {
                device.addItem ("Any device", 1);
                device.addItemList (devices, 2);
                device.setSelectedId (m.device.isEmpty() ? 1 : devices.indexOf (m.device) + 2, juce::dontSendNotification);
                if (device.getSelectedId() < 1)
                {
                    device.addItem (m.device + " (absent)", 100);
                    device.setSelectedId (100, juce::dontSendNotification);
                }
                device.onChange = [this] { context.controller.setValue (midi + ".device", device.getSelectedId() == 1 ? juce::String() : device.getText()); };

                channel.addItem ("Any ch.", 1);
                for (int i = 1; i <= 16; ++i)
                    channel.addItem ("Ch. " + juce::String (i), i + 1);
                channel.setSelectedId (m.channel + 1, juce::dontSendNotification);
                channel.onChange = [this] { context.controller.setValue (midi + ".channel", channel.getSelectedId() - 1); };

                type.addItem ("CC", 1);
                type.addItem ("Note", 2);
                type.setSelectedId (m.messageType == "noteon" ? 2 : 1, juce::dontSendNotification);
                type.onChange = [this] { context.controller.setValue (midi + ".messageType", type.getSelectedId() == 2 ? "noteon" : "cc"); };

                number.setRange (0, 127, 1);
                number.setSliderStyle (juce::Slider::IncDecButtons);
                number.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 40, 20);
                number.setValue (m.noteOrController, juce::dontSendNotification);
                number.onValueChange = [this] { context.controller.setValue (midi + ".noteOrController", (int) number.getValue()); };

                learn.setClickingTogglesState (true);
                learn.setToggleState (m.autoLearn, juce::dontSendNotification);
                learn.onClick = [this] { context.controller.setValue (midi + ".autoLearn", learn.getToggleState() ? 1 : 0, false); };

                fillEndpointCombo (target, targets, containerPath, s != nullptr ? s->targetPath : juce::String());
                target.onChange = [this]
                {
                    if (scaler.isNotEmpty())
                        context.controller.setRoutingTarget (scaler, comboEndpoint (target, targets));
                };

                remove.onClick = [this]
                {
                    juce::StringArray paths (midi);
                    if (scaler.isNotEmpty())
                        paths.add (scaler);
                    context.controller.remove (paths);
                };

                for (juce::Component* comp : std::initializer_list<juce::Component*> { &device, &channel, &type, &number, &learn, &target, &remove })
                    addAndMakeVisible (comp);
            }

            void resized() override
            {
                auto area = getLocalBounds().reduced (2, 4);
                remove.setBounds (area.removeFromRight (28));
                area.removeFromRight (6);
                device.setBounds (area.removeFromLeft (160));
                channel.setBounds (area.removeFromLeft (80).withTrimmedLeft (4));
                type.setBounds (area.removeFromLeft (70).withTrimmedLeft (4));
                number.setBounds (area.removeFromLeft (100).withTrimmedLeft (4));
                learn.setBounds (area.removeFromLeft (64).withTrimmedLeft (4));
                area.removeFromLeft (8);
                target.setBounds (area);
            }

            void paint (juce::Graphics& g) override
            {
                g.setColour (Theme::get().outline.withAlpha (0.4f));
                g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
            }

        private:
            AppContext& context;
            juce::String midi, scaler;
            juce::StringArray targets;
            juce::ComboBox device, channel, type, target;
            juce::Slider number;
            juce::TextButton learn { "Learn" }, remove { "x" };
        };

        AppContext& context;
        juce::String container;
        RowList list;
    };

    //==============================================================================
    PropertiesPanel::PropertiesPanel (AppContext& c) : context (c)
    {
        title.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        addAndMakeVisible (title);

        info = new InfoTab (c);
        routing = new RoutingTab (c);
        scripting = new ScriptingTab (c);
        midi = new MidiTab (c);

        auto tabColour = Theme::get().panelAlt;
        tabs.addTab ("Info", tabColour, info, true);
        tabs.addTab ("Routing", tabColour, routing, true);
        tabs.addTab ("Scripting", tabColour, scripting, true);
        tabs.addTab ("MIDI", tabColour, midi, true);
        tabs.setTabBarDepth (28);
        tabs.setOutline (0);
        addAndMakeVisible (tabs);

        context.model.addChangeListener (this);
        selectionChanged();
    }

    PropertiesPanel::~PropertiesPanel()
    {
        context.model.removeChangeListener (this);
    }

    juce::String PropertiesPanel::getContainerPath() const
    {
        auto& m = context.model;
        if (m.get().findBlock (m.selectedBlock) != nullptr)  return m.selectedBlock;
        if (m.get().findTrack (m.selectedTrack) != nullptr)  return m.selectedTrack;
        return m.get().valid ? m.get().path : juce::String();
    }

    void PropertiesPanel::selectionChanged()
    {
        const auto container = getContainerPath();
        auto& p = context.model.get();

        juce::String kind = container == p.path ? "Project" : p.findTrack (container) != nullptr ? "Track" : "Block";
        title.setText (container.isEmpty() ? juce::String() : kind + ":  " + container.replace (".", "  >  "), juce::dontSendNotification);

        if (container != shownContainer)
        {
            shownContainer = container;
            info->setTarget (container);
            routing->setTarget (container);
            scripting->setTarget (container);
            midi->setTarget (container);
        }
    }

    void PropertiesPanel::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        selectionChanged();
        info->refresh();
        routing->refresh();
        scripting->refresh();
        midi->refresh();
    }

    void PropertiesPanel::paint (juce::Graphics& g)
    {
        g.fillAll (Theme::get().panelAlt);
    }

    void PropertiesPanel::resized()
    {
        auto area = getLocalBounds();
        title.setBounds (area.removeFromTop (26).reduced (8, 2));
        tabs.setBounds (area);
    }
}
