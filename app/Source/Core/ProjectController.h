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

        const juce::File& getProjectFile() const noexcept   { return projectFile; }
        bool hasUnsavedChanges() const noexcept             { return undoManager.getNumActionsInCurrentTransaction() > 0 || savedTransactionCount != transactionCounter; }
        void markChanged()                                  { ++transactionCounter; }

        /** Creates the root-level AudioSettings / MidiSettings nodes if missing. */
        void ensureSettingsNodes();

        //==============================================================================
        // Generic

        void setValue (const juce::String& endpointPath, const juce::var& value, bool undoable = true);
        void bang (const juce::String& endpointPath);
        juce::Result rename (const juce::String& path, const juce::String& newName);
        void setInfo (const juce::String& nodePath, const juce::String& markdown);

        /** Deletes any node, plus everything the GUI conventions tie to it: connections
            referring to it, envelopes targeting it, its block envelope, its userData entries. */
        void remove (const juce::String& path);
        void remove (const juce::StringArray& paths);

        //==============================================================================
        // Tracks & blocks

        juce::String addTrack();
        void setTrackColour (const juce::String& trackPath, juce::Colour);
        void setTrackHeight (const juce::String& trackPath, int height);
        void moveTrack (const juce::String& trackPath, int newIndex);

        juce::String addBlock (const juce::String& trackPath, int startTick, int endTick, bool withDefaultContents = true);
        void repositionBlock (const juce::String& blockPath, int startTick, int endTick);
        juce::String moveBlockToTrack (const juce::String& blockPath, const juce::String& trackPath);

        /** Imports a saved block (.integra) into a track at the given time. */
        juce::Result importBlock (const juce::File&, const juce::String& trackPath, int startTick, juce::String* newBlockPath = nullptr);
        juce::Result exportNode (const juce::String& path, const juce::File&);

        //==============================================================================
        // Modules & audio connections (Module View)

        juce::String addModule (const juce::String& blockPath, const GUID& moduleGuid, juce::Point<float> gridPosition);
        void setModulePositions (const juce::String& blockPath, const std::map<juce::String, juce::Rectangle<float>>& positions);

        /** Creates a Connection between two absolute endpoint paths inside containerPath. */
        juce::String connect (const juce::String& containerPath, const juce::String& sourceEndpoint, const juce::String& targetEndpoint);

        static constexpr float moduleWidth = 7.0f;          // ModuleInstance._standardModuleWidth (grid units)
        static float moduleHeight (const ModuleCatalogue::Entry*);

        //==============================================================================
        // Control routing (Properties: Routing) - Connection -> Scaler -> Connection

        juce::String addRouting (const juce::String& containerPath, const juce::String& sourceEndpoint, const juce::String& targetEndpoint);
        void setRoutingSource (const juce::String& scalerPath, const juce::String& sourceEndpoint);
        void setRoutingTarget (const juce::String& scalerPath, const juce::String& targetEndpoint);

        /** Sets scaler ranges from the endpoints' constraints, as the Flex GUI did when routing. */
        void initialiseScalerRanges (const juce::String& scalerPath, const juce::String& sourceEndpoint, const juce::String& targetEndpoint);

        //==============================================================================
        // MIDI (Properties: MIDI) - MidiControlInput -> Scaler -> target

        juce::String addMidiControl (const juce::String& containerPath, const juce::String& targetEndpoint, bool learn);

        //==============================================================================
        // Envelopes & control points (block automation)

        juce::String addEnvelope (const juce::String& blockPath, const juce::String& targetEndpoint);
        juce::String addControlPoint (const juce::String& envelopePath, int tick, float value);
        void moveControlPoint (const juce::String& controlPointPath, int tick, float value);
        void setControlPointCurvature (const juce::String& controlPointPath, float curvature);

        //==============================================================================
        // Scenes & transport

        juce::String addScene (int startTick, int length);
        void setScene (const juce::String& scenePath, int startTick, int length, const juce::String& mode);
        void setSceneKeybinding (const juce::String& scenePath, const juce::String& key);
        void activateScene (const juce::String& scenePath);
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

        juce::String addScript (const juce::String& containerPath);
        void setScriptText (const juce::String& scriptPath, const juce::String& text);
        void runScript (const juce::String& scriptPath);

        //==============================================================================
        // GUI state stored in userData (undoable like everything else)

        void updateBlockUserData (const juce::String& blockPath, std::function<void (BlockUserData&)>);
        void updateTrackUserData (const juce::String& trackPath, std::function<void (TrackUserData&)>);
        void updateProjectUserData (std::function<void (ProjectUserData&)>, bool undoable = false);
        void updateSceneUserData (const juce::String& scenePath, std::function<void (SceneUserData&)>);

        /** Live view control visibility for a module widget. */
        void setLiveViewControl (const juce::String& modulePath, const juce::String& widgetLabel, bool shown, juce::Rectangle<float> bounds = {});

        //==============================================================================
        /** Runs fn as a single named undo transaction. */
        template <typename Fn>
        auto transaction (const juce::String& name, Fn&& fn)
        {
            undoManager.beginNewTransaction (name);
            lastSetPath = {};
            ++transactionCounter;
            return fn();
        }

    private:
        bool perform (juce::UndoableAction*);
        juce::String createNode (const juce::String& moduleName, const juce::String& prefix, const juce::String& parentPath);
        juce::String createNode (const GUID&, const juce::String& prefix, const juce::String& parentPath);
        void setValueInTransaction (const juce::String& endpointPath, const juce::var& value);
        juce::String relativeTo (const juce::String& containerPath, const juce::String& absolutePath) const;
        void collectDependants (const juce::String& path, juce::StringArray& toDelete) const;
        void removeInTransaction (const juce::StringArray& paths);
        juce::String addConnectionNode (const juce::String& containerPath, const juce::String& source, const juce::String& target);
        juce::String addBlockEnvelope (const juce::String& trackPath, const juce::String& blockPath, int start, int end);
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
