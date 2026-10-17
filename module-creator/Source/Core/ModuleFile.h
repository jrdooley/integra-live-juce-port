#pragma once

#include "ModuleDefinition.h"

namespace modulecreator
{
    /**
        .module files: zips holding integra_module_data/interface_definition.iid, the licence
        and the implementation (Pd patches) under integra_module_data/implementation/.
        While a module is open its implementation lives in a working directory on disk, where
        it can be edited in Pd (MainComponent.loadFile / saveFile, Implementation.storeImplementation).
    */
    namespace ModuleFile
    {
        inline const juce::String moduleExtension { "module" };
        inline const juce::String bundleExtension { "bundle" };
        inline const juce::String toplevelDirectory { "integra_module_data" };
        inline const juce::String interfaceDefinitionPath { "integra_module_data/interface_definition.iid" };
        inline const juce::String licensePath { "integra_module_data/license.txt" };
        inline const juce::String implementationDirectory { "integra_module_data/implementation" };
        /** The name Integra Live recognises as the module being tested from the Module Creator. */
        inline const juce::String testingModuleFileName { "ModuleInDevelopment-4df2725b-f0ba-400a-9899-5f14632dd550.module" };

        /** Reads a module, extracting its implementation into the (empty) working directory. */
        juce::Result load (const juce::File& moduleFile, const CreatorConfig&, ModuleDefinition& result,
                           const juce::File& workingDirectory);

        /** Writes a module: the interface definition, the licence, and every (non-hidden) file
            in the working directory as the implementation. */
        juce::Result save (const juce::File& moduleFile, const ModuleDefinition&,
                           const juce::File& workingDirectory, const juce::File& licenseFile);

        /** Deletes everything in a directory, leaving the directory. */
        void clearDirectory (const juce::File&);

        //==============================================================================
        /** BundleMaker: a .bundle is a zip of .module files. */
        juce::Result createBundle (const juce::File& bundleFile, const juce::Array<juce::File>& moduleFiles);
        /** Extracts the modules of a bundle; existing files are not overwritten. `report` lists what happened. */
        juce::Result extractBundle (const juce::File& bundleFile, const juce::File& directory, juce::StringArray& report);
    }
}
