#pragma once

#include "IntegraSession.h"
#include <juce_data_structures/juce_data_structures.h>

namespace integra
{
    /*
        Undoable primitives over libIntegra commands, for juce::UndoManager.

        The Flex GUI had one hand-written inverse per high-level command (~70 classes
        in controller/serverCommands). Here every high-level operation in
        ProjectController is composed from these five primitives inside one undo
        transaction, and each primitive knows its own inverse:

            CreateNode  <->  delete
            DeleteNode  <->  reload the subtree from a temporary .integra file saved
                             just before deleting (restores all values and children)
            SetValue    <->  set previous value
            Rename      <->  rename back   (libIntegra re-targets connections itself)
            Move        <->  move back
    */

    /** Directory for the subtree snapshots DeleteNodeAction needs; removed on exit. */
    juce::File getUndoScratchDirectory();

    class IntegraAction  : public juce::UndoableAction
    {
    protected:
        explicit IntegraAction (IntegraSession& s) : session (s) {}
        IntegraSession& session;
    };

    class CreateNodeAction  : public IntegraAction
    {
    public:
        CreateNodeAction (IntegraSession&, const GUID& moduleGuid, const juce::String& name, const juce::String& parentPath);
        bool perform() override;
        bool undo() override;
        juce::String getPath() const    { return joinPath ({ parent, name }); }

    private:
        GUID guid;
        juce::String name, parent;
    };

    class DeleteNodeAction  : public IntegraAction
    {
    public:
        DeleteNodeAction (IntegraSession&, const juce::String& path);
        ~DeleteNodeAction() override;
        bool perform() override;
        bool undo() override;

    private:
        juce::String path;
        juce::File snapshot;
    };

    class SetValueAction  : public IntegraAction
    {
    public:
        /** Captures the current value as the undo value. */
        SetValueAction (IntegraSession&, const juce::String& endpointPath, const juce::var& newValue);
        bool perform() override;
        bool undo() override;
        int getSizeInUnits() override   { return 1; }

        /** Successive drags of the same control collapse into one undo step. */
        juce::UndoableAction* createCoalescedAction (juce::UndoableAction* nextAction) override;

    private:
        SetValueAction (IntegraSession&, const juce::String& endpointPath, const juce::var& newValue, const juce::var& oldValue);

        juce::String path;
        juce::var newValue, oldValue;
    };

    class RenameNodeAction  : public IntegraAction
    {
    public:
        RenameNodeAction (IntegraSession&, const juce::String& path, const juce::String& newName);
        bool perform() override;
        bool undo() override;

    private:
        juce::String parent, oldName, newName;
    };

    class MoveNodeAction  : public IntegraAction
    {
    public:
        MoveNodeAction (IntegraSession&, const juce::String& path, const juce::String& newParentPath);
        bool perform() override;
        bool undo() override;

    private:
        juce::String name, oldParent, newParent;
    };
}
