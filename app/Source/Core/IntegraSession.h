#pragma once

#include "IntegraIncludes.h"
#include "ModuleCatalogue.h"
#include "NotificationHub.h"

namespace integra
{
    /**
        Owns the in-process libIntegra session (this replaces the separate
        integra_server process and its XML-RPC/OSC protocol used by the Flex GUI).

        Started in host_driven_audio mode: libpd's audio is pulled by AudioHost from
        JUCE's audio device callback rather than by PortAudio.
    */
    class IntegraSession
    {
    public:
        struct Locations
        {
            juce::File systemModules;       // shipped .module files
            juce::File thirdPartyModules;   // user-installed modules (writable)
            juce::File resources;           // libIntegra data: CollectionSchema.xsd, id2guid.csv
            juce::File blockLibrary;        // demo blocks
            juce::File userBlockLibrary;    // user-saved blocks (writable)
            juce::File viewInfo;            // the Info view's help texts (Flex assets/viewInfo)
            juce::File assets;              // About box texts, licence, module documentation style (Flex assets)

            /** Finds resources next to the executable / in the app bundle, falling back
                to the source tree the app was built from. */
            static Locations findDefault();
        };

        IntegraSession() = default;
        ~IntegraSession();

        juce::Result start (const Locations&);
        void stop();
        bool isRunning() const noexcept                 { return running; }

        const Locations& getLocations() const noexcept  { return locations; }

        /** Locks libIntegra for the lifetime of the returned object. Keep it short:
            the player thread and Pd feedback also need this lock. */
        CServerLock lock()                              { return session.get_server(); }

        /** Direct access for the audio thread (CIntegraSession::process_audio). */
        CIntegraSession& raw() noexcept                 { return session; }

        NotificationHub& getNotifications() noexcept    { return notifications; }
        const ModuleCatalogue& getCatalogue() const noexcept { return catalogue; }

        /** Re-reads the module list (after loading files with embedded modules, installing modules...) */
        void refreshCatalogue();

        //==============================================================================
        // Thin, logged wrappers around libIntegra commands. All take and return
        // dot-separated path strings. None of these record undo; see ProjectController.

        CError createNode (const GUID& moduleGuid, const juce::String& name, const juce::String& parentPath);
        CError createNode (const juce::String& moduleName, const juce::String& name, const juce::String& parentPath);
        CError deleteNode (const juce::String& path);
        CError renameNode (const juce::String& path, const juce::String& newName);
        CError moveNode (const juce::String& path, const juce::String& newParentPath);
        CError setValue (const juce::String& endpointPath, const juce::var& value);
        CError bang (const juce::String& endpointPath);
        CError loadFile (const juce::File& file, const juce::String& parentPath, guid_set* newEmbeddedModules = nullptr);
        CError saveFile (const juce::File& file, const juce::String& nodePath);

        //==============================================================================
        // Queries (each takes the lock briefly).

        juce::var getValue (const juce::String& endpointPath);
        bool nodeExists (const juce::String& path);
        juce::String getModuleName (const juce::String& nodePath);     // interface name of a node, e.g. "TapDelay"
        juce::StringArray getChildNames (const juce::String& parentPath);

        /** A name like "TapDelay3" that doesn't clash with siblings in parentPath -
            same scheme as the Flex GUI (IntegraContainer.getNewChildName). */
        juce::String getNewChildName (const juce::String& parentPath, const juce::String& prefix, const GUID& moduleGuid);

        /** Last error text from a failed command, for user-facing messages. */
        const juce::String& getLastError() const noexcept { return lastError; }

    private:
        CError run (ICommand* command, CCommandResult* result = nullptr);

        CIntegraSession session;
        NotificationHub notifications;
        ModuleCatalogue catalogue;
        Locations locations;
        bool running = false;
        juce::String lastError;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IntegraSession)
    };
}
