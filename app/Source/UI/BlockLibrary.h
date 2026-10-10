#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "Core/AppContext.h"

namespace integra
{
    /**
        The Block Library (Flex: views/BlockLibrary/), at the right of the Arrange view.

        Lists the blocks shipped with Integra Live, in the order of the library's
        index.xml, then the blocks the user saved with "Add to Block Library". Drag a block
        onto a track to import it there, or double-click it (or press Enter) to import it
        into the selected track at the playhead. User blocks can be removed from the
        library with the context menu. Hovering shows the block's info text.
    */
    class BlockLibrary  : public juce::Component,
                          private juce::ListBoxModel,
                          private juce::Timer
    {
    public:
        explicit BlockLibrary (AppContext&);
        ~BlockLibrary() override;

        struct Entry
        {
            juce::File file;
            bool isUserBlock = false;
            juce::String title, info;       // info: the block's "info" attribute (Markdown)
            juce::Time modified;
        };

        /** Re-reads both library directories (cheap when nothing changed). */
        void refresh();
        const std::vector<Entry>& getEntries() const noexcept   { return entries; }

        /** Imports an entry into the selected track at the play position (onInstantiate). */
        juce::Result importEntry (int index);
        /** Moves a user block to the trash ("Remove from Block Library"). */
        bool removeEntry (int index);

        juce::ListBox& getList() noexcept                       { return list; }

        /** Gives the play position (ticks) for double-click imports. */
        std::function<int()> getPlayPosition;

        static constexpr const char* dragPrefix = "integra-block:";
        static constexpr int defaultWidth = 200, minimumWidth = 100, maximumWidth = 400;

        /** Reads the info text of a block file (BlockLibraryListEntry.loadInfo). */
        static juce::String readBlockInfo (const juce::File&);

        void paint (juce::Graphics&) override;
        void resized() override;
        void visibilityChanged() override                       { if (isVisible()) refresh(); }

    private:
        int getNumRows() override;
        void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
        void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
        void listBoxItemClicked (int row, const juce::MouseEvent&) override;
        void returnKeyPressed (int row) override;
        juce::var getDragSourceDescription (const juce::SparseSet<int>&) override;
        juce::String getTooltipForRow (int row) override;
        void timerCallback() override                           { refresh(); }

        AppContext& context;
        juce::ListBox list { "blocks", this };
        std::vector<Entry> entries;
        std::map<juce::String, Entry> cache;    // by full path; re-read when the file changes

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BlockLibrary)
    };
}
