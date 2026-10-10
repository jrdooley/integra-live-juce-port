#include "ModuleCatalogue.h"

namespace integra
{
    void ModuleCatalogue::rebuild (const IServer& server)
    {
        entries.clear();

        for (const GUID& id : server.get_all_module_ids())
        {
            const IInterfaceDefinition* definition = server.find_interface (id);
            if (definition == nullptr)
                continue;

            const IInterfaceInfo& info = definition->get_interface_info();

            Entry e;
            e.guid = id;
            e.originGuid = definition->get_origin_guid();
            e.definition = definition;
            e.name = toString (info.get_name());
            e.label = toString (info.get_label());
            e.description = toString (info.get_description());
            e.author = toString (info.get_author());
            for (const auto& tag : info.get_tags())
                e.tags.add (toString (tag));
            e.tags.sort (true);
            e.source = definition->get_module_source();
            e.isCore = e.tags.contains ("core");
            e.hasImplementation = definition->get_implementation_info() != nullptr;

            auto toTime = [] (const struct tm& t)
            {
                return juce::Time (t.tm_year + 1900, t.tm_mon, juce::jmax (1, t.tm_mday), t.tm_hour, t.tm_min, t.tm_sec, 0, false);
            };
            e.created = toTime (info.get_created_date());
            e.modified = toTime (info.get_modified_date());

            for (const IEndpointDefinition* endpoint : definition->get_endpoint_definitions())
            {
                if (! endpoint->is_audio_stream())
                    continue;

                if (endpoint->get_stream_info()->get_direction() == IStreamInfo::INPUT)
                    ++e.numAudioInputs;
                else
                    ++e.numAudioOutputs;
            }

            entries.push_back (std::move (e));
        }

        std::sort (entries.begin(), entries.end(), [] (const Entry& a, const Entry& b)
        {
            return a.label.compareNatural (b.label) < 0;
        });
    }

    const ModuleCatalogue::Entry* ModuleCatalogue::findByGuid (const GUID& guid) const
    {
        for (auto& e : entries)
            if (guidsEqual (e.guid, guid))
                return &e;

        return nullptr;
    }

    const ModuleCatalogue::Entry* ModuleCatalogue::findByName (const juce::String& name) const
    {
        const Entry* fallback = nullptr;

        for (auto& e : entries)
        {
            if (e.name != name)
                continue;

            if (e.source == IInterfaceDefinition::MODULE_SHIPPED_WITH_INTEGRA)
                return &e;

            if (fallback == nullptr)
                fallback = &e;
        }

        return fallback;
    }

    std::vector<const ModuleCatalogue::Entry*> ModuleCatalogue::getUserModules() const
    {
        std::vector<const Entry*> result;

        for (auto& e : entries)
            if (! e.isCore && e.source != IInterfaceDefinition::MODULE_EMBEDDED)
                result.push_back (&e);

        return result;
    }

    juce::StringArray ModuleCatalogue::getAllTags() const
    {
        juce::StringArray tags;

        for (auto* e : getUserModules())
            tags.addArray (e->tags);

        tags.removeDuplicates (true);
        tags.sortNatural();
        return tags;
    }

    juce::String ModuleCatalogue::Entry::getSourceLabel() const
    {
        switch (source)
        {
            case IInterfaceDefinition::MODULE_SHIPPED_WITH_INTEGRA:  return "Shipped";
            case IInterfaceDefinition::MODULE_3RD_PARTY:           return "3rd Party";
            case IInterfaceDefinition::MODULE_EMBEDDED:              return "Embedded";
            case IInterfaceDefinition::MODULE_IN_DEVELOPMENT:        return "In Development";
            default:                                                 return "Unknown";
        }
    }

    std::vector<const ModuleCatalogue::Entry*> ModuleCatalogue::getVersions (const GUID& originGuid) const
    {
        std::vector<const Entry*> versions;
        for (auto& e : entries)
            if (guidsEqual (e.originGuid, originGuid))
                versions.push_back (&e);

        auto priority = [] (const Entry* e)
        {
            switch (e->source)
            {
                case IInterfaceDefinition::MODULE_SHIPPED_WITH_INTEGRA:  return 2;
                case IInterfaceDefinition::MODULE_3RD_PARTY:           return 1;
                case IInterfaceDefinition::MODULE_EMBEDDED:
                case IInterfaceDefinition::MODULE_IN_DEVELOPMENT:
                default:                                                 return 0;
            }
        };

        std::stable_sort (versions.begin(), versions.end(), [&] (const Entry* a, const Entry* b)
        {
            if (priority (a) != priority (b))
                return priority (a) > priority (b);
            return a->modified > b->modified;
        });
        return versions;
    }

    const ModuleCatalogue::Entry* ModuleCatalogue::getBestVersion (const Entry& e) const
    {
        auto versions = getVersions (e.originGuid);
        return versions.empty() ? &e : versions.front();
    }
}
