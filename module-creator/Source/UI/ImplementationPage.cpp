#include "ImplementationPage.h"
#include "MessageBox.h"

namespace modulecreator
{
    namespace
    {
        class NodeItem  : public juce::TreeViewItem
        {
        public:
            explicit NodeItem (const FileTreeNode& n) : node (n)
            {
                for (auto& child : node.children)
                    addSubItem (new NodeItem (child));
            }

            bool mightContainSubItems() override        { return ! node.children.empty(); }
            int getItemHeight() const override          { return 20; }
            bool canBeSelected() const override         { return false; }
            juce::String getUniqueName() const override { return node.label; }

            void paintItem (juce::Graphics& g, int width, int height) override
            {
                g.setColour (Theme::get().text);
                g.setFont (juce::FontOptions (13.0f));
                g.drawText (node.label, 4, 0, width - 4, height, juce::Justification::centredLeft, true);
            }

        private:
            FileTreeNode node;
        };

        int countRows (const std::vector<FileTreeNode>& nodes)
        {
            int n = 0;
            for (auto& node : nodes)
                n += 1 + countRows (node.children);
            return n;
        }

        juce::String describeFiles (const juce::Array<juce::File>& files, const juce::File& root)
        {
            // Implementation.buildUnusedFilesString, truncated after about 400 characters
            juce::String text;
            for (auto& f : files)
            {
                if (text.length() > 400)
                    return text + "...\n<more files>\n";
                text << f.getRelativePathFrom (root) << "\n";
            }
            return text;
        }
    }

    ImplementationPage::ImplementationPage (Document& d, PdHost& h, juce::PropertySet& s)
        : Page (d), host (h), settings (s)
    {
        for (auto* c : std::initializer_list<juce::Component*> {
                 &directoryLabel, &patchLabel, &pdLabel, &dependenciesLabel, &missingLabel, &unusedLabel, &oversizedLabel,
                 &directory, &pdPath, &openDirectory, &relocate, &addFilesButton, &editButton, &choosePd, &deleteUnused,
                 &patch, &dependencies, &missing, &unused, &oversized, &issues })
            content.addAndMakeVisible (c);

        for (auto* e : { &directory, &pdPath })
        {
            styleEditor (*e);
            e->setReadOnly (true);
        }
        directory.setTooltip ("The module's implementation lives here while you edit it.  Put the module's Pd patches and abstractions here.");

        openDirectory.setTooltip ("Show the working directory in the file browser");
        openDirectory.onClick = [this]
        {
            // revealToUser shows the folder's parent; opening the folder itself is more useful
            if (! document.getWorkingDirectory().startAsProcess())
                document.getWorkingDirectory().revealToUser();
        };

        relocate.setTooltip ("Move the working directory somewhere else");
        relocate.onClick = [this]
        {
            chooser = std::make_unique<juce::FileChooser> ("Set Working Directory Location", document.getWorkingDirectory().getParentDirectory());
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc)
            {
                auto folder = fc.getResult();
                if (folder == juce::File())
                    return;
                if (host.isRunning())
                    return integra::showMessage (juce::MessageBoxIconType::WarningIcon, "Can't relocate working directory",
                                                 "Close Pd before moving the working directory.");
                auto r = document.relocateWorkingDirectory (folder);
                if (r.failed())
                    integra::showMessage (juce::MessageBoxIconType::WarningIcon, "Can't relocate working directory", r.getErrorMessage());
                else
                    settings.setValue ("workingDirectoryParent", folder.getFullPathName());
            });
        };

        addFilesButton.setTooltip ("Copy Pd patches, abstractions and other files the module needs into the working directory");
        addFilesButton.onClick = [this] { addFiles(); };

        patch.setTextWhenNothingSelected ("(choose the main patch)");
        patch.setTextWhenNoChoicesAvailable ("(no .pd files in the working directory)");
        patch.setTooltip ("The patch Integra Live loads for each instance of the module");
        patch.onChange = [this]
        {
            const auto name = patch.getText();
            if (name != document.definition.patchName)
            {
                document.definition.patchName = name;
                document.rescan();
                document.changed();
            }
        };

        editButton.setTooltip ("Open the main patch in Pd, inside a host patch that feeds it audio and lets you try its endpoints");
        editButton.onClick = [this] { editInPd(); };

        choosePd.setTooltip ("Choose the Pd (Pure Data) application used to edit implementations");
        choosePd.onClick = [this]
        {
            chooser = std::make_unique<juce::FileChooser> ("Choose the Pd application", getPdExecutable());
            const int chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                             | (juce::SystemStats::getOperatingSystemType() & juce::SystemStats::MacOSX ? juce::FileBrowserComponent::canSelectDirectories : 0);
            chooser->launchAsync (chooserFlags, [this] (const juce::FileChooser& fc)
            {
                if (fc.getResult() != juce::File())
                {
                    settings.setValue ("pdExecutable", fc.getResult().getFullPathName());
                    refresh();
                }
            });
        };

        deleteUnused.onClick = [this]
        {
            juce::Array<juce::File> files;
            collectFiles (document.getScan().unusedFiles, files);
            if (files.isEmpty())
                return;
            juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Delete Unused Files",
                                                "The following files will be deleted:\n\n" + describeFiles (files, document.getWorkingDirectory()) + "\nAre you sure?",
                                                "Delete", "Cancel", this,
                                                juce::ModalCallbackFunction::create ([this, files] (int result)
            {
                if (result == 0)
                    return;
                for (auto& f : files)
                    if (f.existsAsFile() && f.isAChildOf (document.getWorkingDirectory()))
                        f.deleteFile();
                document.pollWorkingDirectory();
            }));
        };

        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);

        startTimer (2000);      // Implementation._pollWorkingDirectoryMsecs was 10 s
        refresh();
    }

    ImplementationPage::~ImplementationPage()
    {
        for (auto* t : { &dependencies, &missing, &unused, &oversized })
            t->deleteRootItem();
    }

    juce::File ImplementationPage::getPdExecutable() const
    {
        return findPdExecutable (settings.getValue ("pdExecutable"));
    }

    void ImplementationPage::editInPd()
    {
        auto r = host.start (getPdExecutable(), document);
        if (r.failed())
            integra::showMessage (juce::MessageBoxIconType::WarningIcon, "Edit in Pd", r.getErrorMessage());
        refresh();
    }

    void ImplementationPage::addFiles()
    {
        chooser = std::make_unique<juce::FileChooser> ("Add files to the implementation",
                                                       juce::File (settings.getValue ("addFilesDirectory", juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName())));
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectMultipleItems | juce::FileBrowserComponent::canSelectDirectories,
                              [this] (const juce::FileChooser& fc)
        {
            auto results = fc.getResults();
            if (results.isEmpty())
                return;
            settings.setValue ("addFilesDirectory", results[0].getParentDirectory().getFullPathName());
            juce::StringArray failed;
            for (auto& f : results)
            {
                auto target = document.getWorkingDirectory().getChildFile (f.getFileName());
                if (f == target)
                    continue;
                const bool ok = f.isDirectory() ? f.copyDirectoryTo (target) : f.copyFileTo (target);
                if (! ok)
                    failed.add (f.getFullPathName());
            }
            document.pollWorkingDirectory();
            if (! failed.isEmpty())
                integra::showMessage (juce::MessageBoxIconType::WarningIcon, "Add Files", "Couldn't copy:\n" + failed.joinIntoString ("\n"));
        });
    }

    void ImplementationPage::timerCallback()
    {
        document.pollWorkingDirectory();
        const bool running = host.isRunning();
        if (editButton.getButtonText() != (running ? "Editing in Pd..." : "Edit in Pd"))
            refresh();
    }

    void ImplementationPage::setTree (juce::TreeView& tree, const std::vector<FileTreeNode>& nodes, const juce::String& emptyText)
    {
        tree.deleteRootItem();
        FileTreeNode root { emptyText, {}, nodes };
        tree.setRootItem (new NodeItem (root));
        tree.setRootItemVisible (nodes.empty());
        tree.setDefaultOpenness (true);
        tree.getRootItem()->setOpen (true);
    }

    void ImplementationPage::refresh()
    {
        auto& scan = document.getScan();
        setTextIfDifferent (directory, document.getWorkingDirectory().getFullPathName());

        const auto pd = getPdExecutable();
        setTextIfDifferent (pdPath, pd != juce::File() ? pd.getFullPathName() : juce::String ("(Pd not found: install Pure Data, or choose it)"));

        juce::StringArray current;
        for (int i = 0; i < patch.getNumItems(); ++i)
            current.add (patch.getItemText (i));
        if (current != scan.patchNames)
        {
            patch.clear (juce::dontSendNotification);
            patch.addItemList (scan.patchNames, 1);
        }
        const int index = scan.patchNames.indexOf (document.definition.patchName);
        if (patch.getSelectedItemIndex() != index)
        {
            if (index >= 0)
                patch.setSelectedItemIndex (index, juce::dontSendNotification);
            else
                patch.setSelectedId (0, juce::dontSendNotification);
        }
        patch.setEnabled (! scan.patchNames.isEmpty());

        const bool running = host.isRunning();
        editButton.setButtonText (running ? "Editing in Pd..." : "Edit in Pd");
        editButton.setEnabled (document.getMainPatch().existsAsFile() && ! running);

        // only rebuild the trees when their content changes (keeps scroll positions)
        auto signature = [] (const std::vector<FileTreeNode>& nodes)
        {
            std::function<void (const std::vector<FileTreeNode>&, juce::String&)> walk = [&] (auto& ns, juce::String& out)
            {
                for (auto& n : ns) { out << n.label << "{"; walk (n.children, out); out << "}"; }
            };
            juce::String s;
            walk (nodes, s);
            return s;
        };
        auto update = [&] (juce::TreeView& tree, const std::vector<FileTreeNode>& nodes, const juce::String& emptyText)
        {
            const auto sig = signature (nodes) + emptyText;
            if (tree.getProperties()["signature"].toString() != sig || tree.getRootItem() == nullptr)
            {
                tree.getProperties().set ("signature", sig);
                setTree (tree, nodes, emptyText);
            }
        };
        update (dependencies, scan.dependencies, document.definition.patchName.isEmpty() ? "(no main patch)" : "(no abstractions)");
        update (missing, scan.missingObjects, "(none)");
        update (unused, scan.unusedFiles, "(none)");
        update (oversized, scan.oversizedFiles, "(none)");
        deleteUnused.setEnabled (! scan.unusedFiles.empty());

        issues.setIssues (document.issuesFor (ValidationIssue::Page::implementation));
        layoutContent();
    }

    void ImplementationPage::resized()
    {
        viewport.setBounds (getLocalBounds());
        layoutContent();
    }

    void ImplementationPage::layoutContent()
    {
        auto& scan = document.getScan();
        const int visible = viewport.getMaximumVisibleWidth();
        const int width = juce::jlimit (560, 900, visible);
        FormLayout form { { juce::jmax (0, (visible - width) / 2), 12, width - 16, 10000 } };

        auto dirRow = form.row (&directoryLabel);
        addFilesButton.setBounds (dirRow.removeFromRight (100));
        dirRow.removeFromRight (4);
        relocate.setBounds (dirRow.removeFromRight (90));
        dirRow.removeFromRight (4);
        openDirectory.setBounds (dirRow.removeFromRight (60));
        dirRow.removeFromRight (4);
        directory.setBounds (dirRow);

        auto patchRow = form.row (&patchLabel);
        patch.setBounds (patchRow.removeFromLeft (260));
        patchRow.removeFromLeft (8);
        editButton.setBounds (patchRow.removeFromLeft (130));

        auto pdRow = form.row (&pdLabel);
        choosePd.setBounds (pdRow.removeFromRight (90));
        pdRow.removeFromRight (4);
        pdPath.setBounds (pdRow);

        auto treeHeight = [] (const std::vector<FileTreeNode>& nodes) { return juce::jlimit (24, 220, (juce::jmax (1, countRows (nodes))) * 20 + 4); };
        dependencies.setBounds (form.row (&dependenciesLabel, treeHeight (scan.dependencies)));
        missing.setBounds (form.row (&missingLabel, treeHeight (scan.missingObjects)));
        auto unusedRow = form.row (&unusedLabel, treeHeight (scan.unusedFiles) + rowHeight + 4);
        deleteUnused.setBounds (unusedRow.removeFromBottom (rowHeight).removeFromLeft (180));
        unusedRow.removeFromBottom (4);
        unused.setBounds (unusedRow);
        oversized.setBounds (form.row (&oversizedLabel, treeHeight (scan.oversizedFiles)));
        oversized.setTooltip ("Files of 1 MB or more make modules, and the projects that use them, big");

        const int issueHeight = issues.getIdealHeight (form.area.getWidth() - labelWidth);
        issues.setBounds (form.row (nullptr, issueHeight));
        issues.setVisible (issueHeight > 0);

        content.setSize (juce::jmax (visible, width), form.area.getY() + 12);
    }
}
