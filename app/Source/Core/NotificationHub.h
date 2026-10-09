#pragma once

#include "IntegraIncludes.h"
#include <juce_events/juce_events.h>
#include <unordered_map>

namespace integra
{
    /**
        Receives libIntegra's set-command notifications (which arrive on whichever
        thread processed the command, with the server lock held) and re-delivers them
        on the JUCE message thread.

        Changes are coalesced: if an endpoint changes many times between two message
        loop iterations, listeners hear about it once. Listeners must not assume they
        see every intermediate value, only that they're told when to re-read it.

        The app also uses the hub to broadcast "structure changed" after it creates,
        deletes, renames, moves or loads nodes - libIntegra itself only notifies sets.
    */
    class NotificationHub  : public INotificationSink,
                             private juce::AsyncUpdater
    {
    public:
        struct Change
        {
            juce::String path;
            CCommandSource source;
        };

        struct Listener
        {
            virtual ~Listener() = default;

            /** Endpoints whose values changed since the last call (message thread). */
            virtual void endpointsChanged (const std::vector<Change>&) {}

            /** Nodes were added, removed, renamed, moved or loaded (message thread). */
            virtual void structureChanged() {}
        };

        NotificationHub() = default;
        ~NotificationHub() override { cancelPendingUpdate(); }

        void addListener (Listener* l)      { listeners.add (l); }
        void removeListener (Listener* l)   { listeners.remove (l); }

        /** Called by the app after structural edits; delivered asynchronously and coalesced. */
        void notifyStructureChanged()
        {
            structureDirty = true;
            triggerAsyncUpdate();
        }

        /** Deliver anything pending right now (message thread only). */
        void flush()                        { handleUpdateNowIfNeeded(); }

        // INotificationSink - any thread, server lock held
        void on_set_command (const IServer&, const CPath& endpointPath, const CCommandSource& source) override
        {
            {
                const juce::SpinLock::ScopedLockType sl (pendingLock);
                pending[endpointPath.get_string()] = source;
            }
            triggerAsyncUpdate();
        }

    private:
        void handleAsyncUpdate() override
        {
            std::unordered_map<std::string, CCommandSource> changes;
            {
                const juce::SpinLock::ScopedLockType sl (pendingLock);
                changes.swap (pending);
            }

            if (structureDirty.exchange (false))
                listeners.call ([] (Listener& l) { l.structureChanged(); });

            if (! changes.empty())
            {
                std::vector<Change> list;
                list.reserve (changes.size());
                for (auto& c : changes)
                    list.push_back ({ toString (c.first), c.second });

                listeners.call ([&list] (Listener& l) { l.endpointsChanged (list); });
            }
        }

        juce::SpinLock pendingLock;
        std::unordered_map<std::string, CCommandSource> pending;
        std::atomic<bool> structureDirty { false };
        juce::ListenerList<Listener> listeners;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NotificationHub)
    };
}
