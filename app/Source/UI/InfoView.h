#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "Core/AppContext.h"
#include "Markdown.h"

namespace integra
{
    /** What the Info view shows (Flex: model/Info.as). */
    struct Info
    {
        juce::String title, markdown;
        juce::String editablePath;      // node whose "info" endpoint this is ("" = read-only help)

        bool isEmpty() const noexcept   { return title.isEmpty() && markdown.isEmpty(); }
        bool operator== (const Info& o) const  { return title == o.title && markdown == o.markdown && editablePath == o.editablePath; }
        bool operator!= (const Info& o) const  { return ! operator== (o); }
    };

    /** Views say what is under the mouse (Flex: IntegraView.getInfoToDisplay). */
    struct InfoProvider
    {
        virtual ~InfoProvider() = default;
        virtual Info getInfoAt (juce::Point<int> localPosition) = 0;
    };

    /** The help texts in viewInfo/ (Flex: InfoMarkupForViews, assets/viewInfo/ Markdown files). */
    class InfoTexts
    {
    public:
        /** e.g. "ArrangeView/BlockView"; an empty Info if there is no such text. */
        static Info forView (const juce::String& name);
        /** The info of a project node (its "info" endpoint), editable in the Info view. */
        static Info forNode (AppContext&, const juce::String& path, const juce::String& placeholderView = {});
        /** Keeps the platform's parts of the Flex texts (mac-only / windows-only spans) and
            drops other HTML. */
        static juce::String cleanMarkdown (const juce::String&);

        static void setFolder (const juce::File&);
        static juce::StringArray getNames();
    };

    /**
        The Info view (Flex: views/InfoView/InfoView.as): help for whatever is under the
        mouse - from the Flex GUI's help texts, or the info of the project, track, block,
        scene or module. F2 (Ctrl+3 on macOS) locks it so it can be scrolled, and the info of
        project objects can then be edited (Markdown).
    */
    class InfoView  : public juce::Component,
                      public InfoProvider,
                      private juce::Timer
    {
    public:
        explicit InfoView (AppContext&);
        ~InfoView() override;

        bool isShown() const noexcept               { return shown; }
        void setShown (bool);
        bool isLocked() const noexcept              { return locked; }
        void setLocked (bool);

        const Info& getDisplayedInfo() const noexcept   { return displayed; }
        void display (const Info&);
        /** What the view would show for a point of a component (walks up to an InfoProvider). */
        static Info infoFor (juce::Component*, juce::Point<int> screenPosition);

        void startEditing();
        juce::TextEditor* getEditor() const noexcept    { return editor.get(); }
        void finishEditing (bool save);

        void paint (juce::Graphics&) override;
        void resized() override;
        Info getInfoAt (juce::Point<int>) override      { return displayed; }   // reading it keeps it

        static constexpr int defaultWidth = 260;

    private:
        void timerCallback() override;
        void layoutText();

        AppContext& context;
        bool shown = true, locked = false;
        Info displayed;
        juce::Viewport viewport;
    public:
        /** Info text with links: a click on one opens it in the browser (Flex: the
            TextArea's HTML links). */
        struct Text  : public juce::Component
        {
            juce::TextLayout layout;
            std::vector<MarkdownLink> links;
            std::function<void (const juce::String&)> onLinkClicked;

            juce::String linkAt (juce::Point<float> p) const
            {
                const int c = characterAt (layout, p);
                for (auto& l : links)
                    if (l.characters.contains (c))
                        return l.url;
                return {};
            }
            void paint (juce::Graphics& g) override     { layout.draw (g, getLocalBounds().toFloat()); }
            void mouseMove (const juce::MouseEvent& e) override
            {
                setMouseCursor (linkAt (e.position).isNotEmpty() ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
            }
            void mouseUp (const juce::MouseEvent& e) override
            {
                const auto url = linkAt (e.position);
                if (url.isNotEmpty() && e.mouseWasClicked())
                {
                    if (onLinkClicked)  onLinkClicked (url);
                    else                juce::URL (url).launchInDefaultBrowser();
                }
            }
        };
        Text& getText() noexcept        { return text; }
    private:
        Text text;
        juce::TextButton editButton { "Edit" }, saveButton { "Save" }, cancelButton { "Cancel" };
        std::unique_ptr<juce::TextEditor> editor;
    };
}
