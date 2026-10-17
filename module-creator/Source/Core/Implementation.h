#pragma once

#include "ModuleDefinition.h"
#include <map>
#include <set>

namespace modulecreator
{
    struct CreatorConfig;

    /** A node of the trees on the Implementation page. */
    struct FileTreeNode
    {
        juce::String label;
        juce::File file;                            // set for unused files (which can be deleted)
        std::vector<FileTreeNode> children;
    };

    /** The Pd object names that need no file in the module (Implementation.loadObjectSet). */
    struct PdObjectSets
    {
        std::set<juce::String> internalObjects, supportedAbstractions, supportedExternals;
        void load (const CreatorConfig&);
    };

    /**
        What the Implementation page shows about the working directory (Implementation.scanWorkingDirectory):
        the patches that can be the main patch, the abstractions it uses, objects that can't be
        found, files nothing uses, and files that are too big.
    */
    struct ImplementationScan
    {
        juce::StringArray patchNames;
        std::vector<FileTreeNode> dependencies, missingObjects, unusedFiles, oversizedFiles;

        bool hasMissingObjects() const              { return ! missingObjects.empty(); }

        static ImplementationScan scan (const juce::File& workingDirectory, const juce::String& patchName, const PdObjectSets&);
    };

    /** Path -> modification time of every non-hidden file, to notice edits made in Pd. */
    std::map<juce::String, juce::int64> snapshotDirectory (const juce::File&);

    void collectFiles (const std::vector<FileTreeNode>&, juce::Array<juce::File>&);

    constexpr juce::int64 oversizedFileThreshold = 1024000;

    //==============================================================================
    /**
        The Pd patch that hosts a module's implementation for editing (HostStarter.createHostPatch):
        audio in and out of the module, and a box for every control endpoint sent to the host.
    */
    struct HostPatch
    {
        /** Writes host.pd and its abstractions into `directory`; returns the host patch. */
        static juce::Result create (const juce::File& directory, const juce::File& modulePatch,
                                    const ModuleDefinition&, const CreatorConfig&, juce::File& hostPatch);

        static juce::String generateConnections (const ModuleDefinition&);
        static juce::String generateSendValues (const ModuleDefinition&);
    };

    /** Finds a Pd executable: the configured one, then the usual install locations and PATH. */
    juce::File findPdExecutable (const juce::String& configuredPath);
}
