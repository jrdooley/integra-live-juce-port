#include "CreatorConfig.h"

namespace modulecreator
{
    const WidgetDefinition* CreatorConfig::findWidget (const juce::String& widgetName) const
    {
        for (auto& w : widgets)
            if (w.name == widgetName)
                return &w;
        return nullptr;
    }

    juce::Result CreatorConfig::load (const juce::File& directory, CreatorConfig& result)
    {
        result = {};
        result.resourcesDirectory = directory;

        const auto file = directory.getChildFile ("ModuleCreator_config.xml");
        auto xml = juce::XmlDocument::parse (file);
        if (xml == nullptr || ! xml->hasTagName ("modulecreatorconfig"))
            return juce::Result::fail ("Can't read " + file.getFullPathName());

        result.integraDeveloper = xml->getChildByName ("integradeveloper") != nullptr;

        if (auto* tags = xml->getChildByName ("standardtags"))
            for (auto* tag : tags->getChildWithTagNameIterator ("standardtag"))
                result.standardTags.add (tag->getAllSubText().trim());

        if (auto* args = xml->getChildByName ("hostargs"))
            for (auto* arg : args->getChildWithTagNameIterator ("hostarg"))
                result.hostArguments.add (arg->getAllSubText().trim());

        if (auto* links = xml->getChildByName ("helplinks"))
            for (auto* link : links->getChildWithTagNameIterator ("helplink"))
                result.helpLinks.emplace_back (link->getStringAttribute ("name"), link->getAllSubText().trim());

        if (auto* widgets = xml->getChildByName ("widgets"))
            for (auto* w : widgets->getChildWithTagNameIterator ("widget"))
            {
                WidgetDefinition d;
                d.name = w->getStringAttribute ("name");
                d.minimumSize = { w->getIntAttribute ("minWidth"), w->getIntAttribute ("minHeight") };
                d.defaultSize = { w->getIntAttribute ("defaultWidth"), w->getIntAttribute ("defaultHeight") };
                d.maximumSize = { w->getIntAttribute ("maxWidth"), w->getIntAttribute ("maxHeight") };
                for (auto* a : w->getChildWithTagNameIterator ("attribute"))
                    d.attributes.add (a->getAllSubText().trim());
                if (d.name.isNotEmpty() && ! d.attributes.isEmpty())
                    result.widgets.push_back (d);
            }

        return juce::Result::ok();
    }

    juce::File CreatorConfig::findResourcesDirectory()
    {
        const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
        const juce::String folderName ("module-creator-resources");

        juce::Array<juce::File> candidates {
            exe.getParentDirectory().getChildFile (folderName),
            exe.getParentDirectory().getParentDirectory().getChildFile ("Resources").getChildFile (folderName),   // macOS bundle
        };
       #ifdef MODULE_CREATOR_SOURCE_RESOURCES_DIR
        candidates.add (juce::File (MODULE_CREATOR_SOURCE_RESOURCES_DIR));
       #endif

        for (auto& c : candidates)
            if (c.getChildFile ("ModuleCreator_config.xml").existsAsFile())
                return c;
        return candidates.getFirst();
    }
}
