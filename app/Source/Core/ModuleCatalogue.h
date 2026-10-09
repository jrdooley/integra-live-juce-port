#pragma once

#include "IntegraIncludes.h"

namespace integra
{
    /**
        A message-thread-friendly copy of the facts the GUI needs about each
        interface definition libIntegra knows about (shipped, third-party and
        embedded modules). Rebuilt whenever the set of modules changes, e.g. after
        loading a project that embeds modules this installation doesn't ship.

        The IInterfaceDefinition pointers stay valid for as long as the module stays
        loaded in libIntegra; shipped and third-party modules are never unloaded
        while a session runs.
    */
    class ModuleCatalogue
    {
    public:
        struct Entry
        {
            GUID guid {};
            const IInterfaceDefinition* definition = nullptr;
            juce::String name, label, description, author;
            juce::StringArray tags;
            IInterfaceDefinition::module_source source = IInterfaceDefinition::MODULE_SHIPPED_WITH_INTEGRA;
            bool isCore = false;            // tagged "core": Container, Player, Scene, Connection...
            bool hasImplementation = false; // has a Pd implementation
            int numAudioInputs = 0, numAudioOutputs = 0;
        };

        void rebuild (const IServer& server);

        const std::vector<Entry>& getEntries() const noexcept       { return entries; }

        const Entry* findByGuid (const GUID&) const;

        /** Finds a module by interface name, preferring the shipped version over
            embedded/3rd-party ones with the same name. */
        const Entry* findByName (const juce::String& name) const;

        /** Modules the user can add to a block in the Module View (non-core, shipped or 3rd party). */
        std::vector<const Entry*> getUserModules() const;

        /** All distinct tags of user modules, sorted. */
        juce::StringArray getAllTags() const;

    private:
        std::vector<Entry> entries;
    };
}
