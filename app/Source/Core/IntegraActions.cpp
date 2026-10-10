#include "IntegraActions.h"

namespace integra
{
    juce::File getUndoScratchDirectory()
    {
        static const juce::File dir = []
        {
            auto d = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("IntegraLiveUndo-" + juce::String (juce::Time::currentTimeMillis()));
            d.createDirectory();
            return d;
        }();

        return dir;
    }

    //==============================================================================
    CreateNodeAction::CreateNodeAction (IntegraSession& s, const GUID& g, const juce::String& n, const juce::String& p)
        : IntegraAction (s), guid (g), name (n), parent (p)
    {
    }

    bool CreateNodeAction::perform()
    {
        return session.createNode (guid, name, parent) == CError::SUCCESS;
    }

    bool CreateNodeAction::undo()
    {
        return session.deleteNode (getPath()) == CError::SUCCESS;
    }

    //==============================================================================
    DeleteNodeAction::DeleteNodeAction (IntegraSession& s, const juce::String& p)
        : IntegraAction (s), path (p)
    {
    }

    DeleteNodeAction::~DeleteNodeAction()
    {
        if (snapshot != juce::File())
            snapshot.getParentDirectory().deleteRecursively();
    }

    bool DeleteNodeAction::perform()
    {
        // libIntegra renames a loaded top-level node after the file it came from, so the
        // snapshot must be called <node name>.integra; one directory per action keeps
        // same-named snapshots apart.
        if (snapshot == juce::File())
        {
            auto dir = getUndoScratchDirectory().getNonexistentChildFile ("deleted", {});
            dir.createDirectory();
            snapshot = dir.getChildFile ((path.contains (".") ? lastElement (path) : path) + ".integra");
        }

        // Saving can fail for nodes the file format can't hold on their own; we still
        // delete, but then the step can't be undone.
        if (session.saveFile (snapshot, path) != CError::SUCCESS)
            snapshot = juce::File();

        return session.deleteNode (path) == CError::SUCCESS;
    }

    bool DeleteNodeAction::undo()
    {
        if (! snapshot.existsAsFile())
            return false;

        return session.loadFile (snapshot, parentPath (path)) == CError::SUCCESS;
    }

    //==============================================================================
    SetValueAction::SetValueAction (IntegraSession& s, const juce::String& p, const juce::var& v)
        : IntegraAction (s), path (p), newValue (v), oldValue (s.getValue (p))
    {
    }

    SetValueAction::SetValueAction (IntegraSession& s, const juce::String& p, const juce::var& v, const juce::var& old)
        : IntegraAction (s), path (p), newValue (v), oldValue (old)
    {
    }

    bool SetValueAction::perform()
    {
        return session.setValue (path, newValue) == CError::SUCCESS;
    }

    bool SetValueAction::undo()
    {
        if (oldValue.isVoid())
            return true;    // bangs have no previous state
        return session.setValue (path, oldValue) == CError::SUCCESS;
    }

    juce::UndoableAction* SetValueAction::createCoalescedAction (juce::UndoableAction* nextAction)
    {
        if (auto* next = dynamic_cast<SetValueAction*> (nextAction))
        {
            if (next->path == path && ! oldValue.isVoid())
            {
                return new SetValueAction (session, path, next->newValue, oldValue);
            }
        }

        return nullptr;
    }

    //==============================================================================
    RenameNodeAction::RenameNodeAction (IntegraSession& s, const juce::String& path, const juce::String& n)
        : IntegraAction (s), parent (path.contains (".") ? parentPath (path) : juce::String()),     // the project is top level
          oldName (path.contains (".") ? lastElement (path) : path), newName (n)
    {
    }

    bool RenameNodeAction::perform()
    {
        return session.renameNode (joinPath ({ parent, oldName }), newName) == CError::SUCCESS;
    }

    bool RenameNodeAction::undo()
    {
        return session.renameNode (joinPath ({ parent, newName }), oldName) == CError::SUCCESS;
    }

    //==============================================================================
    MoveNodeAction::MoveNodeAction (IntegraSession& s, const juce::String& path, const juce::String& p)
        : IntegraAction (s), name (path.contains (".") ? lastElement (path) : path),
          oldParent (path.contains (".") ? parentPath (path) : juce::String()), newParent (p)
    {
    }

    bool MoveNodeAction::perform()
    {
        return session.moveNode (joinPath ({ oldParent, name }), newParent) == CError::SUCCESS;
    }

    bool MoveNodeAction::undo()
    {
        return session.moveNode (joinPath ({ newParent, name }), oldParent) == CError::SUCCESS;
    }
}
