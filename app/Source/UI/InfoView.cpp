#include "InfoView.h"
#include "PropertiesPanel.h"
#include "IntegraLookAndFeel.h"

namespace integra
{
    namespace
    {
        juce::File& infoFolder()
        {
            static juce::File folder;
            return folder;
        }

        std::map<juce::String, Info>& texts()
        {
            // InfoMarkupForViews.loadContent: every .md under viewInfo/, keyed by its relative path
            static std::map<juce::String, Info> content;
            static juce::File loadedFrom;
            if (loadedFrom != infoFolder())
            {
                loadedFrom = infoFolder();
                content.clear();
                for (auto& f : infoFolder().findChildFiles (juce::File::findFiles, true, "*.md"))
                {
                    const auto name = f.getRelativePathFrom (infoFolder()).replaceCharacter ('\\', '/').upToLastOccurrenceOf (".md", false, true);
                    Info info;
                    info.markdown = InfoTexts::cleanMarkdown (f.loadFileAsString());
                    // the first heading is the title
                    auto firstLine = info.markdown.upToFirstOccurrenceOf ("\n", false, false).trim();
                    info.title = firstLine.startsWith ("#") ? firstLine.trimCharactersAtStart ("# ") : name.fromLastOccurrenceOf ("/", false, false);
                    content[name] = info;
                }
            }
            return content;
        }

        bool isMac()
        {
           #if JUCE_MAC
            return true;
           #else
            return false;
           #endif
        }
    }

    void InfoTexts::setFolder (const juce::File& folder)   { infoFolder() = folder; }

    juce::StringArray InfoTexts::getNames()
    {
        juce::StringArray names;
        for (auto& [name, info] : texts())
            names.add (name);
        return names;
    }

    juce::String InfoTexts::cleanMarkdown (const juce::String& raw)
    {
        // <span class="mac-only">...</span> / windows-only (Linux follows Windows)
        juce::String text = raw.replace ("\r\n", "\n");
        for (auto [cls, keep] : { std::pair<const char*, bool> { "mac-only", isMac() }, { "windows-only", ! isMac() } })
        {
            const juce::String open = juce::String ("<span class=\"") + cls + "\">";
            for (int start = text.indexOf (open); start >= 0; start = text.indexOf (open))
            {
                const int end = text.indexOf (start, "</span>");
                if (end < 0)
                    break;
                const auto inner = text.substring (start + open.length(), end);
                text = text.substring (0, start) + (keep ? inner : juce::String()) + text.substring (end + 7);
            }
        }

        // other HTML tags go; Markdown links keep their text
        juce::String out;
        for (int i = 0; i < text.length(); ++i)
        {
            if (text[i] == '<' && i + 1 < text.length() && (text[i + 1] == '/' || juce::CharacterFunctions::isLetter (text[i + 1])))
            {
                const int close = text.indexOfChar (i, '>');
                if (close > i)
                {
                    i = close;
                    continue;
                }
            }
            out << juce::String::charToString (text[i]);
        }
        for (int start = out.indexOfChar ('['); start >= 0; start = out.indexOfChar (start + 1, '['))
        {
            const int mid = out.indexOf (start, "](");
            const int end = mid > 0 ? out.indexOfChar (mid, ')') : -1;
            if (mid > start && end > mid && ! out.substring (start, mid).containsChar ('\n'))
                out = out.substring (0, start) + out.substring (start + 1, mid) + out.substring (end + 1);
        }
        return out.trim();
    }

    Info InfoTexts::forView (const juce::String& name)
    {
        auto& all = texts();
        auto it = all.find (name);
        return it != all.end() ? it->second : Info();
    }

    Info InfoTexts::forNode (AppContext& context, const juce::String& path, const juce::String& placeholderView)
    {
        // the object's own info (Flex: IntegraDataObject.info), editable
        Info info;
        info.title = path.contains (".") ? lastElement (path) : path;
        info.markdown = context.session.getValue (path + ".info").toString();
        if (info.markdown.trim().isEmpty() && placeholderView.isNotEmpty())
            info.markdown = forView (placeholderView).markdown;
        info.editablePath = path;
        return info;
    }

    //==============================================================================
    InfoView::InfoView (AppContext& c) : context (c)
    {
        InfoTexts::setFolder (context.session.getLocations().viewInfo);
        viewport.setViewedComponent (&text, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);

        editButton.onClick = [this] { startEditing(); };
        saveButton.onClick = [this] { finishEditing (true); };
        cancelButton.onClick = [this] { finishEditing (false); };
        addChildComponent (editButton);
        addChildComponent (saveButton);
        addChildComponent (cancelButton);

        if (auto* settings = context.properties.getUserSettings())
            shown = settings->getBoolValue ("showInfoView", true);

        display (InfoTexts::forView ("Info"));
        startTimerHz (8);
    }

    InfoView::~InfoView() = default;

    void InfoView::setShown (bool shouldShow)
    {
        shown = shouldShow;
        if (! shown)
            setLocked (false);
        if (auto* settings = context.properties.getUserSettings())
            settings->setValue ("showInfoView", shown);
    }

    void InfoView::setLocked (bool shouldLock)
    {
        // InfoView.toggleFocus: a locked view keeps its content, scrolls and can edit
        if (! shouldLock && editor != nullptr)
            finishEditing (false);
        locked = shouldLock;
        editButton.setVisible (locked && displayed.editablePath.isNotEmpty() && editor == nullptr);
        resized();
        repaint();
    }

    void InfoView::display (const Info& info)
    {
        if (info == displayed && text.getHeight() > 0)
            return;
        displayed = info;
        editButton.setVisible (locked && displayed.editablePath.isNotEmpty() && editor == nullptr);
        layoutText();
        viewport.setViewPosition (0, 0);
        repaint();
    }

    Info InfoView::infoFor (juce::Component* c, juce::Point<int> screenPosition)
    {
        for (; c != nullptr; c = c->getParentComponent())
            if (auto* provider = dynamic_cast<InfoProvider*> (c))
            {
                auto info = provider->getInfoAt (c->getLocalPoint (nullptr, screenPosition));
                if (! info.isEmpty())
                    return info;
            }
        return {};
    }

    void InfoView::timerCallback()
    {
        // follow the mouse (IntegraLive.onMouseMove -> SetDisplayedInfo) unless locked
        if (locked || ! isShowing() || juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
            return;
        auto source = juce::Desktop::getInstance().getMainMouseSource();
        auto* under = source.getComponentUnderMouse();
        if (under == nullptr || under->getTopLevelComponent() != getTopLevelComponent())
            return;
        auto info = infoFor (under, source.getScreenPosition().roundToInt());
        if (! info.isEmpty() && info != displayed)
            display (info);
    }

    void InfoView::layoutText()
    {
        auto& t = Theme::get();
        auto markdown = displayed.markdown;
        // the title is shown in the header; drop a matching first heading
        if (markdown.trimStart().startsWith ("#"))
        {
            auto first = markdown.trimStart().upToFirstOccurrenceOf ("\n", false, false);
            if (first.trimCharactersAtStart ("# ").trim() == displayed.title.trim())
                markdown = markdown.trimStart().fromFirstOccurrenceOf ("\n", false, false);
        }
        auto attributed = renderMarkdown (markdown.isEmpty() ? juce::String ("*No info*") : markdown, t.text, t.accent);
        const int width = juce::jmax (40, viewport.getMaximumVisibleWidth() - 4);
        text.layout.createLayout (attributed, (float) width);
        text.setSize (width, (int) std::ceil (text.layout.getHeight()) + 8);
        text.repaint();
    }

    void InfoView::startEditing()
    {
        if (displayed.editablePath.isEmpty())
            return;
        setLocked (true);
        editor = std::make_unique<juce::TextEditor>();
        editor->setMultiLine (true, true);
        editor->setReturnKeyStartsNewLine (true);
        editor->setText (context.session.getValue (displayed.editablePath + ".info").toString(), false);
        addAndMakeVisible (*editor);
        editButton.setVisible (false);
        saveButton.setVisible (true);
        cancelButton.setVisible (true);
        resized();
        editor->grabKeyboardFocus();
    }

    void InfoView::finishEditing (bool save)
    {
        if (editor == nullptr)
            return;
        auto edited = std::move (editor);
        saveButton.setVisible (false);
        cancelButton.setVisible (false);
        if (save && displayed.editablePath.isNotEmpty())
        {
            context.controller.setValue (displayed.editablePath + ".info", edited->getText());   // SetObjectInfo
            displayed.markdown = edited->getText();
        }
        edited.reset();
        editButton.setVisible (locked && displayed.editablePath.isNotEmpty());
        layoutText();
        resized();
        repaint();
    }

    void InfoView::paint (juce::Graphics& g)
    {
        auto& t = Theme::get();
        g.fillAll (t.panelAlt);
        auto header = getLocalBounds().removeFromTop (26);
        g.setColour (t.header);
        g.fillRect (header);
        g.setColour (t.text);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText ("Info" + (displayed.title.isNotEmpty() ? ":  " + displayed.title : juce::String()),
                    header.reduced (8, 0).withTrimmedRight (editButton.isVisible() ? 60 : 0), juce::Justification::centredLeft, true);

        if (locked)
        {
            g.setColour (t.accent);
            g.drawRect (getLocalBounds(), 2);
        }
        else
        {
            // InfoView.updateFocusPrompt
            juce::String prompt = isMac() ? "Press Ctrl+3 to lock" : "Press F2 to lock";
            if (text.getHeight() > viewport.getHeight())
                prompt << "/scroll";
            if (displayed.editablePath.isNotEmpty())
                prompt << "/edit";
            prompt << " info";
            g.setColour (t.textDim);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (prompt, getLocalBounds().removeFromBottom (18), juce::Justification::centred);
        }
    }

    void InfoView::resized()
    {
        auto area = getLocalBounds();
        auto header = area.removeFromTop (26);
        editButton.setBounds (header.removeFromRight (56).reduced (3));
        if (editor != nullptr)
        {
            auto buttons = area.removeFromBottom (28).reduced (4, 3);
            cancelButton.setBounds (buttons.removeFromRight (70));
            buttons.removeFromRight (4);
            saveButton.setBounds (buttons.removeFromRight (70));
            editor->setBounds (area.reduced (4));
            viewport.setVisible (false);
            return;
        }
        viewport.setVisible (true);
        if (! locked)
            area.removeFromBottom (18);
        viewport.setBounds (area.reduced (6, 4));
        viewport.setScrollBarsShown (locked, false);
        layoutText();
    }
}
