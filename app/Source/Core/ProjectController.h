#pragma once

#include "IntegraSession.h"
#include "IntegraActions.h"
#include "ProjectModel.h"

namespace integra
{
    /**
        High-level editing operations on an Integra Live project - the JUCE port of the
        Flex GUI's controller/serverCommands. Each public editing method is one undo
        step built from the primitives in IntegraActions.h, and follows the same node
        conventions as the Flex GUI so files stay compatible.

        All methods are message-thread only. Paths are absolute dot-separated strings.
    */
    class ProjectController
    {
    public:
        ProjectController (IntegraSession&, ProjectModel&);

        juce::UndoManager& getUndoManager() noexcept        { return undoManager; }
        ProjectModel& getModel() noexcept                   { return model; }
        IntegraSession& getSession() noexcept               { return session; }

        static constexpr int newBlockSeconds = 20;          // Block.newBlockSeconds in the Flex GUI

        //==============================================================================
        // Project lifecycle (not undoable; they clear the undo history)

        /** Deletes any current project and creates the default one: a track with one
            block containing AudioIn and StereoAudioOut (as the Flex GUI did). */
        juce::Result newProject();
        juce::Result openProject (const juce::File&);
        juce::Result saveProject (const juce::File&);

        /** A new project with every block from the block library imported, one per
            track - a quick tour of the shipped demo blocks. */
        juce::Result newDemoProject();

        const juce::File& getProjectFile() const noexcept   { return projectFile; }
        bool hasUnsavedChanges() const noexcept             { return undoManager.getNumActionsInCurrentTransaction() > 0 || savedTransactionCount != transactionCounter; }
        void markChanged()                                  { ++transactionCounter; }

        /** Creates the root-level AudioSettings / MidiSettings nodes if missing. */
        void ensureSettingsNodes();

        //==============================================================================
        // Generic

        void setValue (juce::String endpointPath, const juce::var& value, bool undoable = true);
        void bang (juce::String endpointPath);
        juce::Result rename (juce::String path, juce::String newName);
        void setInfo (juce::String nodePath, juce::String markdown);

        /** Deletes any node, plus everything the GUI conventions tie to it: connections
            referring to it, envelopes targeting it, its block envelope, its userData entries. */
        void remove (juce::String path);
        void remove (const juce::StringArray& paths);

        //==============================================================================
        // Tracks & blocks

        juce::String addTrack();
        void setTrackColour (juce::String trackPath, juce::Colour);
        void setTrackHeight (juce::String trackPath, int height);
        void moveTrack (juce::String trackPath, int newIndex);

        juce::String addBlock (juce::String trackPath, int startTick, int endTick, bool withDefaultContents = true);
        void repositionBlock (juce::String blockPath, int startTick, int endTick);
        juce::String moveBlockToTrack (juce::String blockPath, juce::String trackPath);

        /** Imports a saved block (.integra) into a track at the given time. */
        juce::Result importBlock (const juce::File&, juce::String trackPath, int startTick, juce::String* newBlockPath = nullptr);
        juce::Result exportNode (juce::String path, const juce::File&);

        //==============================================================================
        // Modules & audio connections (Module View)

        juce::String addModule (juce::String blockPath, const GUID& moduleGuid, juce::Point<float> gridPosition);
        void setModulePositions (juce::String blockPath, const std::map<juce::String, juce::Rectangle<float>>& positions);

        /** Creates a Connection between two absolute endpoint paths inside containerPath. */
        juce::String connect (juce::String containerPath, juce::String sourceEndpoint, juce::String targetEndpoint);

        static constexpr float moduleWidth = 7.0f;          // ModuleInstance._standardModuleWidth (grid units)
        static float moduleHeight (const ModuleCatalogue::Entry*);

        //==============================================================================
        // Control routing (Properties: Routing) - Connection -> Scaler -> Connection

        juce::String addRouting (juce::String containerPath, juce::String sourceEndpoint, juce::String targetEndpoint);
        void setRoutingSource (juce::String scalerPath, juce::String sourceEndpoint);
        void setRoutingTarget (juce::String scalerPath, juce::String targetEndpoint);

        /** Sets scaler ranges from the endpoints' constraints, as the Flex GUI did when routing. */
        void initialiseScalerRanges (juce::String scalerPath, juce::String sourceEndpoint, juce::String targetEndpoint);

        //==============================================================================
        // MIDI (Properties: MIDI) - MidiControlInput -> Scaler -> target

        juce::String addMidiControl (juce::String containerPath, juce::String targetEndpoint, bool learn);

        //==============================================================================
        // Envelopes & control points (block automation)

        juce::String addEnvelope (juce::String blockPath, juce::String targetEndpoint);
        juce::String addControlPoint (juce::String envelopePath, int tick, float value);
        void moveControlPoint (juce::String controlPointPath, int tick, float value);
        void setControlPointCurvature (juce::String controlPointPath, float curvature);

        //==============================================================================
        // Scenes & transport

        juce::String addScene (int startTick, int length);
        void setScene (juce::String scenePath, int startTick, int length, juce::String mode);
        void setSceneKeybinding (juce::String scenePath, juce::String key);
        void activateScene (juce::String scenePath);
        void nextScene();
        void previousScene();

        void play (bool shouldPlay);
        bool isPlaying();
        void stop();
        void returnToStart();
        void setPlayPosition (int tick);
        int getPlayPosition();

        //==============================================================================
        // Scripts

        juce::String addScript (juce::String containerPath);
        void setScriptText (juce::String scriptPath, juce::String text);
        void runScript (juce::String scriptPath);

        //==============================================================================
        // GUI state stored in userData (undoable like everything else)

        void updateBlockUserData (juce::String blockPath, std::function<void (BlockUserData&)>);
        void updateTrackUserData (juce::String trackPath, std::function<void (TrackUserData&)>);
        void updateProjectUserData (std::function<void (ProjectUserData&)>, bool undoable = false);
        void updateSceneUserData (juce::String scenePath, std::function<void (SceneUserData&)>);

        /** Live view control visibility for a module widget. */
        void setLiveViewControl (juce::String modulePath, juce::String widgetLabel, bool shown, juce::Rectangle<float> bounds = {});

        //==============================================================================
        /** Runs fn as a single named undo transaction. */
        template <typename Fn>
        auto transaction (juce::String name, Fn&& fn)
        {
            undoManager.beginNewTransaction (name);
            lastSetPath = {};
            ++transactionCounter;
            return fn();
        }

    private:
        bool perform (juce::UndoableAction*);
        juce::String createNode (juce::String moduleName, juce::String prefix, juce::String parentPath);
        juce::String createNode (const GUID&, juce::String prefix, juce::String parentPath);
        void setValueInTransaction (juce::String endpointPath, const juce::var& value);
        juce::String relativeTo (juce::String containerPath, juce::String absolutePath) const;
        void collectDependants (juce::String path, juce::StringArray& toDelete) const;
        void removeInTransaction (const juce::StringArray& paths);
        juce::String addConnectionNode (juce::String containerPath, juce::String source, juce::String target);
        juce::String addBlockEnvelope (juce::String trackPath, juce::String blockPath, int start, int end);
        juce::Colour newTrackColour() const;

        IntegraSession& session;
        ProjectModel& model;
        juce::UndoManager undoManager { 3000, 100 };
        juce::File projectFile;
        int transactionCounter = 0, savedTransactionCount = 0;
        juce::String lastSetPath;
        juce::uint32 lastSetTime = 0;
    };
}
