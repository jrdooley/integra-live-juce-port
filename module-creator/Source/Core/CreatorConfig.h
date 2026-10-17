#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <vector>

namespace modulecreator
{
    /** A widget type modules can use, with its size limits and attributes (WidgetDefinition). */
    struct WidgetDefinition
    {
        juce::String name;
        juce::Point<int> minimumSize, defaultSize, maximumSize;
        juce::StringArray attributes;
    };

    /**
        ModuleCreator_config.xml (Flex: Config): standard tags, widget types, Pd arguments,
        help links, and the developer switch. Resources (templates, host patches, Pd object
        lists, licence) live next to it.
    */
    struct CreatorConfig
    {
        juce::File resourcesDirectory;
        bool integraDeveloper = false;
        juce::StringArray standardTags;
        juce::StringArray hostArguments;
        std::vector<std::pair<juce::String, juce::String>> helpLinks;   // name, link
        std::vector<WidgetDefinition> widgets;

        juce::File getTemplatesDirectory() const    { return resourcesDirectory.getChildFile ("templates"); }
        juce::File getHostDirectory() const         { return resourcesDirectory.getChildFile ("host"); }
        juce::File getLicenseFile() const           { return resourcesDirectory.getChildFile ("license.txt"); }
        juce::File getAgreementFile() const         { return resourcesDirectory.getChildFile ("agreement.md"); }

        const WidgetDefinition* findWidget (const juce::String& name) const;

        /** Reads ModuleCreator_config.xml from the resources directory. */
        static juce::Result load (const juce::File& resourcesDirectory, CreatorConfig& result);
        /** Finds the resources: next to the binary, in the macOS bundle, or in the source tree. */
        static juce::File findResourcesDirectory();
    };
}
