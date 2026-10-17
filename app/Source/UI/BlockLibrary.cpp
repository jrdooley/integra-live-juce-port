#include "BlockLibrary.h"
#include "IntegraLookAndFeel.h"
#include "MessageBox.h"

namespace integra
{
    BlockLibrary::BlockLibrary (AppContext& c) : context (c)
    {
        list.setRowHeight (22);
        list.setMultipleSelectionEnabled (false);
        addAndMakeVisible (list);
        refresh();

        // pick up blocks added with "Add to Block Library" or copied in by hand
        startTimer (1500);
    }

    BlockLibrary::~BlockLibrary() = default;

    juce::String BlockLibrary::readBlockInfo (const juce::File& file)
    {
        // the info attribute of the top level object in integra_data/nodes.ixd
        juce::ZipFile zip (file);
        auto* entry = zip.getEntry ("integra_data/nodes.ixd");
        if (entry == nullptr)
            return {};

        std::unique_ptr<juce::InputStream> stream (zip.createStreamForEntry (*entry));
        if (stream == nullptr)
            return {};

        auto xml = juce::parseXML (stream->readEntireStreamAsString());
        if (xml == nullptr)
            return {};

        if (auto* top = xml->getFirstChildElement())
            for (auto* attribute : top->getChildWithTagNameIterator ("attribute"))
                if (attribute->getStringAttribute ("name") == "info")
                    return attribute->getAllSubText().trim();
        return {};
    }

    void BlockLibrary::refresh()
    {
        auto& locations = context.session.getLocations();
        std::vector<std::pair<juce::File, bool>> files;

        // system blocks, in the order of index.xml (BlockLibrary.addSystemBlockLibraryDirectory)
        juce::Array<juce::File> systemFiles;
        if (auto index = juce::parseXML (locations.blockLibrary.getChildFile ("index.xml")))
        {
            for (auto* block : index->getChildWithTagNameIterator ("Block"))
            {
                auto f = locations.blockLibrary.getChildFile (block->getAllSubText().trim());
                if (f.existsAsFile() && f.hasFileExtension ("integra"))
                    systemFiles.add (f);
            }
        }
        else
        {
            systemFiles = locations.blockLibrary.findChildFiles (juce::File::findFiles, false, "*.integra");
            systemFiles.sort();
        }
        for (auto& f : systemFiles)
            files.push_back ({ f, false });

        // user blocks (addUserBlockLibraryDirectory)
        if (locations.userBlockLibrary != locations.blockLibrary)
        {
            auto userFiles = locations.userBlockLibrary.findChildFiles (juce::File::findFiles, false, "*.integra");
            userFiles.sort();
            for (auto& f : userFiles)
                files.push_back ({ f, true });
        }

        std::vector<Entry> updated;
        for (auto& [file, isUser] : files)
        {
            auto& cached = cache[file.getFullPathName()];
            if (cached.file != file || cached.modified != file.getLastModificationTime() || cached.isUserBlock != isUser)
            {
                cached.file = file;
                cached.isUserBlock = isUser;
                cached.title = file.getFileNameWithoutExtension();
                cached.modified = file.getLastModificationTime();
                cached.info = readBlockInfo (file);
            }
            updated.push_back (cached);
        }

        auto same = [] (const std::vector<Entry>& a, const std::vector<Entry>& b)
        {
            if (a.size() != b.size())
                return false;
            for (size_t i = 0; i < a.size(); ++i)
                if (a[i].file != b[i].file || a[i].modified != b[i].modified)
                    return false;
            return true;
        };

        if (same (updated, entries))
            return;

        const auto selected = list.getSelectedRow() >= 0 && list.getSelectedRow() < (int) entries.size()
                                ? entries[(size_t) list.getSelectedRow()].file : juce::File();
        entries = std::move (updated);
        list.updateContent();
        list.deselectAllRows();
        for (size_t i = 0; i < entries.size(); ++i)
            if (entries[i].file == selected)
                list.selectRow ((int) i);
        list.repaint();
    }

    juce::Result BlockLibrary::importEntry (int index)
    {
        if (index < 0 || index >= (int) entries.size())
            return juce::Result::fail ("No block selected");

        // BlockLibrary.onInstantiate: the selected track, at the play position
        auto& project = context.model.get();
        if (project.findTrack (context.model.selectedTrack) == nullptr)
            return juce::Result::fail ("Select a track to import the block into");

        const int position = getPlayPosition != nullptr ? getPlayPosition() : 0;
        return context.controller.importBlock (entries[(size_t) index].file, context.model.selectedTrack, position);
    }

    bool BlockLibrary::removeEntry (int index)
    {
        if (index < 0 || index >= (int) entries.size() || ! entries[(size_t) index].isUserBlock)
            return false;

        auto file = entries[(size_t) index].file;
        const bool removed = file.moveToTrash() || file.deleteFile();
        refresh();
        return removed;
    }

    void BlockLibrary::paint (juce::Graphics& g)
    {
        auto& t = Theme::get();
        g.fillAll (t.panelAlt);

        g.setColour (t.textDim);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText ("Block Library", getLocalBounds().removeFromTop (24).reduced (8, 0), juce::Justification::centredLeft);

        if (entries.empty())
        {
            g.setFont (juce::FontOptions (12.0f, juce::Font::italic));
            g.drawFittedText ("No blocks. Save blocks here with \"Add to Block Library\" in a block's menu.",
                              list.getBounds().reduced (8).removeFromTop (60), juce::Justification::centred, 3);
        }
    }

    void BlockLibrary::resized()
    {
        list.setBounds (getLocalBounds().withTrimmedTop (24).reduced (2, 0));
    }

    int BlockLibrary::getNumRows()     { return (int) entries.size(); }

    void BlockLibrary::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
    {
        if (row < 0 || row >= (int) entries.size())
            return;

        auto& t = Theme::get();
        auto& e = entries[(size_t) row];

        // BlockLibraryListEntry: user blocks are tinted blue
        if (e.isUserBlock)
            g.fillAll (juce::Colour (0xff3060c0).withAlpha (0.16f));
        if (selected)
            g.fillAll (t.selection);

        g.setColour (t.text);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (e.title, 8, 0, width - 12, height, juce::Justification::centredLeft, true);
    }

    void BlockLibrary::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
    {
        returnKeyPressed (row);
    }

    void BlockLibrary::returnKeyPressed (int row)
    {
        auto result = importEntry (row);
        if (result.failed())
            showMessage (juce::MessageBoxIconType::WarningIcon, "Import block", result.getErrorMessage());
    }

    void BlockLibrary::listBoxItemClicked (int row, const juce::MouseEvent& e)
    {
        if (! e.mods.isPopupMenu() || row < 0 || row >= (int) entries.size())
            return;

        juce::PopupMenu menu;
        menu.addItem ("Remove from Block Library", entries[(size_t) row].isUserBlock, false, [this, file = entries[(size_t) row].file]
        {
            for (size_t i = 0; i < entries.size(); ++i)
                if (entries[i].file == file)
                    removeEntry ((int) i);
        });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

    juce::var BlockLibrary::getDragSourceDescription (const juce::SparseSet<int>& rows)
    {
        if (rows.isEmpty() || rows[0] >= (int) entries.size())
            return {};
        return juce::String (dragPrefix) + entries[(size_t) rows[0]].file.getFullPathName();
    }

    Info BlockLibrary::getInfoAt (juce::Point<int> p)
    {
        // BlockLibrary.getInfoToDisplay: the block's info, else what the library is
        const auto inList = list.getLocalPoint (this, p);
        const int row = list.getRowContainingPosition (inList.x, inList.y);
        if (list.getLocalBounds().contains (inList) && row >= 0 && row < (int) entries.size())
            return { entries[(size_t) row].title, entries[(size_t) row].info.isEmpty() ? juce::String ("No Info Available") : entries[(size_t) row].info, {} };
        return InfoTexts::forView ("BlockLibrary");
    }

    juce::String BlockLibrary::getTooltipForRow (int row)
    {
        if (row < 0 || row >= (int) entries.size())
            return {};
        auto& e = entries[(size_t) row];
        auto info = e.info.isEmpty() ? juce::String ("No Info Available") : e.info.replace ("**", "").replace ("#", "");
        return e.title + "\n\n" + info.trim() + "\n\nDrag onto a track, or double-click to import into the selected track at the playhead.";
    }
}
