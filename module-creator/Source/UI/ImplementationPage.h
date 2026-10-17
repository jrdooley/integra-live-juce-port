#pragma once

#include "Common.h"
#include "Core/PdHost.h"
#include <juce_data_structures/juce_data_structures.h>

namespace modulecreator
{
    /**
        The "Implementation" tab (Implementation.mxml): the working directory holding the
        module's Pd patches, the main patch, editing it in Pd, and the dependency checks.
    */
    class ImplementationPage  : public Page, private juce::Timer
    {
    public:
        ImplementationPage (Document&, PdHost&, juce::PropertySet& settings);
        ~ImplementationPage() override;

        void refresh() override;
        void resized() override;

        void editInPd();
        juce::File getPdExecutable() const;
        void addFiles();

    private:
        PdHost& host;
        juce::PropertySet& settings;

        FormLabel directoryLabel { "Working Directory" }, patchLabel { "Main Patch File" }, pdLabel { "Pd" },
                  dependenciesLabel { "Dependencies" }, missingLabel { "Missing Objects" }, unusedLabel { "Unused Files" },
                  oversizedLabel { "Oversized Files" };
        juce::TextEditor directory, pdPath;
        juce::TextButton openDirectory { "Open" }, relocate { "Relocate..." }, addFilesButton { "Add Files..." },
                         editButton { "Edit in Pd" }, choosePd { "Choose..." }, deleteUnused { "Delete Unused Files" };
        juce::ComboBox patch;
        juce::TreeView dependencies, missing, unused, oversized;
        IssueList issues;
        juce::Viewport viewport;
        juce::Component content;
        std::unique_ptr<juce::FileChooser> chooser;

        void timerCallback() override;
        void layoutContent();
        static void setTree (juce::TreeView&, const std::vector<FileTreeNode>&, const juce::String& emptyText);
    };
}
