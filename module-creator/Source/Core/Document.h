#pragma once

#include "CreatorConfig.h"
#include "Implementation.h"
#include "ModuleDefinition.h"
#include <juce_events/juce_events.h>

namespace modulecreator
{
    /**
        The module being edited (Flex: ModuleCreator.mxml's state): its interface definition,
        file, modified flag, and the working directory holding its implementation. Pages edit
        `definition` directly and then call changed().
    */
    class Document  : public juce::ChangeBroadcaster
    {
    public:
        /** The implementation is kept in a working directory inside `workingDirectoryParent`. */
        Document (const CreatorConfig&, const juce::File& workingDirectoryParent);

        const CreatorConfig& config;
        ModuleDefinition definition;
        juce::File file;                            // empty until saved or opened
        bool modified = false;
        bool developerMode = false;

        /** Call after editing `definition`: new module id, new modification date, modified
            (MainComponent.onInterfaceModified), then tells the listeners synchronously. */
        void changed();
        /** Tells the listeners without marking the module modified. */
        void refresh()                              { sendSynchronousChangeMessage(); }

        void newFile();
        juce::Result open (const juce::File&);
        /** Loads a module as the starting point of a new one: new ids, no name (importTemplate). */
        juce::Result importTemplate (const juce::File&);
        juce::Result save (const juce::File&);

        /** Problems that prevent saving, including missing implementation dependencies. */
        std::vector<ValidationIssue> validate() const;
        std::vector<ValidationIssue> issuesFor (ValidationIssue::Page, int endpointIndex = -1) const;

        //==============================================================================
        juce::File getWorkingDirectory() const      { return workingDirectory; }
        /** A working directory in `parent`: one this app made (it has a marker file) or a new one,
            never a folder of the user's own files, since the working directory gets emptied. */
        static juce::File chooseWorkingDirectory (const juce::File& parent);
        /** Moves the implementation to a working directory inside a new parent folder. */
        juce::Result relocateWorkingDirectory (const juce::File& newParent);
        const ImplementationScan& getScan() const   { return scan; }
        void rescan();
        /** Rescans if files in the working directory changed (Implementation.onPollWorkingDirectory);
            returns true if they did. */
        bool pollWorkingDirectory();
        juce::File getMainPatch() const;

        juce::String getTitle() const;

    private:
        juce::File workingDirectory;
        void clearWorkingDirectory();
        PdObjectSets objectSets;
        ImplementationScan scan;
        std::map<juce::String, juce::int64> snapshot;

        JUCE_DECLARE_NON_COPYABLE (Document)
    };
}
