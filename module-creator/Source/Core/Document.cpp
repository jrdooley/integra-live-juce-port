#include "Document.h"
#include "ModuleFile.h"

namespace modulecreator
{
    namespace
    {
        const juce::String workingDirectoryName { "Integra Module Creator Working Directory" };
        const juce::String markerName { ".integra-module-creator" };
    }

    Document::Document (const CreatorConfig& c, const juce::File& workingDirectoryParent)
        : config (c), developerMode (c.integraDeveloper), workingDirectory (chooseWorkingDirectory (workingDirectoryParent))
    {
        objectSets.load (config);
        newFile();
    }

    juce::File Document::chooseWorkingDirectory (const juce::File& parent)
    {
        for (int i = 1;; ++i)
        {
            auto candidate = parent.getChildFile (workingDirectoryName + (i > 1 ? " " + juce::String (i) : juce::String()));
            if (! candidate.exists()
                 || (candidate.isDirectory() && (candidate.getChildFile (markerName).existsAsFile()
                                                 || candidate.findChildFiles (juce::File::findFilesAndDirectories, false).isEmpty())))
                return candidate;
        }
    }

    void Document::clearWorkingDirectory()
    {
        workingDirectory.createDirectory();
        ModuleFile::clearDirectory (workingDirectory);
        workingDirectory.getChildFile (markerName).replaceWithText ("The Integra Module Creator keeps the implementation of the module being edited here.\n"
                                                                    "This folder is emptied whenever a module is opened or created.\n");
    }

    void Document::changed()
    {
        definition.touch();
        definition.regenerateModuleGuid();
        modified = true;
        sendSynchronousChangeMessage();
    }

    void Document::newFile()
    {
        file = juce::File();
        definition = ModuleDefinition::createNew();
        clearWorkingDirectory();
        modified = false;
        rescan();
        sendSynchronousChangeMessage();
    }

    juce::Result Document::open (const juce::File& f)
    {
        ModuleDefinition loaded;
        clearWorkingDirectory();
        auto r = ModuleFile::load (f, config, loaded, workingDirectory);
        if (r.failed())
        {
            newFile();      // failsafe: the working directory may be half extracted
            return r;
        }
        definition = std::move (loaded);
        file = f;
        modified = false;
        rescan();
        sendSynchronousChangeMessage();
        return juce::Result::ok();
    }

    juce::Result Document::importTemplate (const juce::File& f)
    {
        ModuleDefinition loaded;
        clearWorkingDirectory();
        auto r = ModuleFile::load (f, config, loaded, workingDirectory);
        if (r.failed())
        {
            newFile();
            return r;
        }

        // InterfaceInfo.templateImported: a new module that starts from the template's content
        loaded.originGuid = createGuid();
        loaded.regenerateModuleGuid();
        loaded.name = loaded.label = loaded.description = loaded.author = {};
        if (! developerMode)
            loaded.tags.removeString (coreTag);
        loaded.implementedInLibIntegra = loaded.implementedInLibIntegra && developerMode;
        loaded.createdDate = loaded.modifiedDate = juce::Time::getCurrentTime();

        definition = std::move (loaded);
        file = juce::File();
        modified = false;
        rescan();
        sendSynchronousChangeMessage();
        return juce::Result::ok();
    }

    juce::Result Document::save (const juce::File& f)
    {
        auto r = ModuleFile::save (f, definition, workingDirectory, config.getLicenseFile());
        if (r.wasOk())
        {
            file = f;
            modified = false;
            sendSynchronousChangeMessage();
        }
        return r;
    }

    std::vector<ValidationIssue> Document::validate() const
    {
        auto issues = definition.validate (developerMode);
        // Implementation.validateContent
        if (scan.hasMissingObjects())
            issues.push_back ({ ValidationIssue::Page::implementation, -1,
                                "Dependencies of the PD patch can't be found.  They are either unsupported externals, or missing abstractions.  "
                                "Abstractions should be placed in the working directory." });
        if (definition.patchName.isNotEmpty() && ! scan.patchNames.contains (definition.patchName))
            issues.push_back ({ ValidationIssue::Page::implementation, -1, "The main patch " + definition.patchName + " is not in the working directory" });
        return issues;
    }

    std::vector<ValidationIssue> Document::issuesFor (ValidationIssue::Page page, int endpointIndex) const
    {
        std::vector<ValidationIssue> result;
        for (auto& i : validate())
            if (i.page == page && (endpointIndex < 0 || i.endpointIndex == endpointIndex))
                result.push_back (i);
        return result;
    }

    juce::Result Document::relocateWorkingDirectory (const juce::File& newParent)
    {
        if (newParent == workingDirectory || newParent.isAChildOf (workingDirectory))
            return juce::Result::fail ("That's not a valid working directory!\n\nNew working directory must be outside your current working directory!");
        auto target = chooseWorkingDirectory (newParent);
        if (target == workingDirectory)
            return juce::Result::ok();
        if (! target.createDirectory())
            return juce::Result::fail ("Can't create " + target.getFullPathName());

        for (auto& f : workingDirectory.findChildFiles (juce::File::findFilesAndDirectories, false))
            if (! f.moveFileTo (target.getChildFile (f.getFileName())))
                return juce::Result::fail ("Can't move " + f.getFullPathName());
        workingDirectory.deleteRecursively();
        workingDirectory = target;
        rescan();
        sendSynchronousChangeMessage();
        return juce::Result::ok();
    }

    void Document::rescan()
    {
        workingDirectory.createDirectory();
        snapshot = snapshotDirectory (workingDirectory);
        scan = ImplementationScan::scan (workingDirectory, definition.patchName, objectSets);
        // a single patch, or one named after the module, is the obvious main patch
        const auto namedPatch = definition.name + ".pd";
        if (definition.patchName.isEmpty() && (scan.patchNames.size() == 1 || scan.patchNames.contains (namedPatch)))
        {
            definition.patchName = scan.patchNames.size() == 1 ? scan.patchNames[0] : namedPatch;
            scan = ImplementationScan::scan (workingDirectory, definition.patchName, objectSets);
        }
    }

    bool Document::pollWorkingDirectory()
    {
        if (snapshotDirectory (workingDirectory) == snapshot)
            return false;
        rescan();
        changed();
        return true;
    }

    juce::File Document::getMainPatch() const
    {
        return definition.patchName.isEmpty() ? juce::File() : workingDirectory.getChildFile (definition.patchName);
    }

    juce::String Document::getTitle() const
    {
        juce::String t ("Integra Module Creator");
        if (file != juce::File())
            t << " - " << file.getFileName();
        if (modified)
            t << " *";
        return t;
    }
}
