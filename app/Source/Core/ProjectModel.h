#pragma once

#include "IntegraSession.h"
#include "UserData.h"
#include <juce_events/juce_events.h>

namespace integra
{
    /*
        The Flex GUI kept a full client-side mirror of the node tree (IntegraModel.as,
        ~1800 lines) synchronised over XML-RPC. With libIntegra in-process the node
        tree itself is the source of truth; ProjectModel just takes a snapshot of it
        interpreted with the GUI's project conventions (docs/00-analysis.md §2.1):

            Project (Container)
              Player1 (Player) ── Scene children
              MidiMonitor (MidiRawInput)
              Track N (Container)
                Block N (Container) ── modules, Connection, Scaler, MidiControlInput,
                                        Envelope (+ ControlPoint), Script
                BlockEnvelope N (Envelope) ── 4 ControlPoints: the block's timeline extent
                Connection / Scaler / MidiControlInput / Script
              Connection / Scaler / MidiControlInput / Script
            AudioSettings, MidiSettings  (top level, outside the project)

        All paths in the snapshot are absolute, dot separated.
        Snapshots are cheap to rebuild and are rebuilt whenever the structure or a
        structural endpoint (positions, routing, userData...) changes.
    */
    namespace model
    {
        struct Node
        {
            juce::String name, path;
        };

        struct ControlPoint  : Node
        {
            int tick = 0;
            float value = 0.0f;
            float curvature = 0.0f;
        };

        struct Connection  : Node
        {
            juce::String sourceRelative, targetRelative;   // as stored in the node
            juce::String sourcePath, targetPath;           // absolute endpoint paths ("" if unset)
            bool active = true;

            juce::String sourceNode() const   { return parentPath (sourcePath); }
            juce::String sourceEndpoint() const { return lastElement (sourcePath); }
            juce::String targetNode() const   { return parentPath (targetPath); }
            juce::String targetEndpoint() const { return lastElement (targetPath); }
        };

        struct Envelope  : Node
        {
            int startTick = 0;
            juce::String targetPath;          // absolute endpoint the envelope drives (via a connection)
            juce::String targetConnection;    // path of that connection
            juce::String playerConnection;    // path of the Player.tick -> currentTick connection
            std::vector<ControlPoint> points; // sorted by tick
        };

        struct Scaler  : Node
        {
            float inMin = 0, inMax = 0, outMin = 0, outMax = 0;
            juce::String inScale = "linear", outScale = "linear", inMode = "snap";
            bool active = true;
            juce::String upstreamConnection, downstreamConnection;   // paths
            juce::String sourcePath, targetPath;                     // absolute endpoints routed through this scaler
        };

        struct MidiControlInput  : Node
        {
            juce::String device, messageType = "cc";
            int channel = 0, noteOrController = 0;
            bool autoLearn = false;
            juce::String outputConnection;    // connection MidiControlInput.value -> (scaler or target)
        };

        struct Script  : Node
        {
            juce::String text, info;
        };

        struct Module  : Node
        {
            const ModuleCatalogue::Entry* entry = nullptr;
            juce::Rectangle<float> position;  // grid units, from the block's userData
        };

        struct Container  : Node
        {
            std::vector<Connection> connections;
            std::vector<Scaler> scalers;
            std::vector<MidiControlInput> midiControlInputs;
            std::vector<Script> scripts;
            juce::String info;
            bool active = true;
        };

        struct Block  : Container
        {
            int start = 0, end = 0;               // ticks, from the block envelope
            juce::String blockEnvelopePath;
            std::vector<Module> modules;
            std::vector<Envelope> envelopes;
            BlockUserData userData;

            const Module* findModule (const juce::String& moduleName) const;
            const Envelope* findEnvelopeForTarget (const juce::String& absoluteEndpointPath) const;
            int length() const noexcept           { return end - start; }
        };

        struct Track  : Container
        {
            std::vector<Block> blocks;            // sorted by start
            std::vector<Envelope> blockEnvelopes;
            TrackUserData userData;
        };

        struct Scene  : Node
        {
            int start = 0, length = 1;
            juce::String mode = "hold";           // hold | play | loop
            juce::String info;
            SceneUserData userData;

            int end() const noexcept              { return start + length; }
        };

        struct Player  : Node
        {
            int rate = 40;                        // ticks per second
            bool loop = false;
            int start = -1, end = -1;             // loop / play range, -1 = unset
            juce::String currentScene;
        };

        /** Something that sets an endpoint automatically: an envelope, MIDI or a routing
            (Flex: IntegraModel.isConnectionTarget). Its controls are locked while it exists. */
        struct Driver
        {
            enum class Kind { envelope, midi, routing } kind;
            juce::String description;         // e.g. "MIDI cc 7 (channel 1)", "Track1.Block1.LFO1.out"
            juce::StringArray nodesToRemove;  // deleting these removes the driver
        };

        struct Project  : Container
        {
            bool valid = false;
            std::vector<Track> tracks;
            std::vector<Scene> scenes;            // sorted by start
            Player player;
            juce::String midiMonitorPath;
            juce::String audioSettingsPath, midiSettingsPath;
            ProjectUserData userData;

            const Track* findTrack (const juce::String& path) const;
            const Block* findBlock (const juce::String& path) const;
            const Track* findTrackOfBlock (const juce::String& blockPath) const;
            const Scene* findScene (const juce::String& path) const;
            const Container* findContainer (const juce::String& path) const;

            /** End of the last block, in ticks. */
            int getLength() const;

            /** What drives `endpointPath`. MIDI inputs still in learn mode don't count, as in Flex. */
            std::vector<Driver> driversOf (const juce::String& endpointPath) const;
        };

        /** Endpoint names whose changes alter the snapshot (others, like Player.tick or
            module parameters, don't need a rebuild). */
        bool isStructuralEndpoint (const juce::String& endpointName);
    }

    //==============================================================================
    class ProjectModel  : private NotificationHub::Listener,
                          public juce::ChangeBroadcaster
    {
    public:
        explicit ProjectModel (IntegraSession&);
        ~ProjectModel() override;

        /** The current snapshot. Valid until the next rebuild (message thread). */
        const model::Project& get() const noexcept  { return project; }

        /** Re-reads the node tree now and broadcasts a change. */
        void rebuild();

        /** Name of the top-level project container ("" if none). */
        const juce::String& getProjectPath() const noexcept { return project.path; }

        // Selection is GUI state (persisted through userData by ProjectController).
        juce::String selectedTrack, selectedBlock, selectedModule, selectedConnection;
        /** The current scene was clicked: the properties panel shows it while no track or
            block is selected (Flex: ProjectUserData.showSceneInTitlebar). */
        bool sceneInProperties = false;

    private:
        void endpointsChanged (const std::vector<NotificationHub::Change>&) override;
        void structureChanged() override;

        IntegraSession& session;
        model::Project project;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProjectModel)
    };
}
