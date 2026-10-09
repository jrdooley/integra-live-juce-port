#pragma once

#include "NotificationHub.h"
#include <map>

namespace integra
{
    /**
        Routes value notifications to whoever is interested in a particular endpoint, so
        that hundreds of on-screen controls don't each scan every change.

        Callbacks run on the message thread with the endpoint's path. Registrations are
        keyed by an owner pointer; call removeAll(owner) in the owner's destructor.
    */
    class EndpointWatcher  : private NotificationHub::Listener
    {
    public:
        using Callback = std::function<void (const juce::String& path, CCommandSource source)>;

        explicit EndpointWatcher (NotificationHub& h) : hub (h)    { hub.addListener (this); }
        ~EndpointWatcher() override                                { hub.removeListener (this); }

        void watch (const juce::String& path, const void* owner, Callback callback)
        {
            watchers.emplace (path, Entry { owner, std::move (callback) });
        }

        /** Watches every endpoint under a node path prefix (e.g. a whole module). */
        void watchPrefix (const juce::String& prefix, const void* owner, Callback callback)
        {
            prefixWatchers.push_back ({ prefix, Entry { owner, std::move (callback) } });
        }

        void removeAll (const void* owner)
        {
            for (auto it = watchers.begin(); it != watchers.end();)
                it = it->second.owner == owner ? watchers.erase (it) : std::next (it);

            prefixWatchers.erase (std::remove_if (prefixWatchers.begin(), prefixWatchers.end(),
                                                  [owner] (auto& p) { return p.second.owner == owner; }),
                                  prefixWatchers.end());
        }

    private:
        struct Entry
        {
            const void* owner;
            Callback callback;
        };

        void endpointsChanged (const std::vector<NotificationHub::Change>& changes) override
        {
            for (auto& change : changes)
            {
                // copy: callbacks may add/remove watchers
                std::vector<Callback> toCall;
                auto range = watchers.equal_range (change.path);
                for (auto it = range.first; it != range.second; ++it)
                    toCall.push_back (it->second.callback);

                for (auto& [prefix, entry] : prefixWatchers)
                    if (change.path.startsWith (prefix))
                        toCall.push_back (entry.callback);

                for (auto& callback : toCall)
                    callback (change.path, change.source);
            }
        }

        NotificationHub& hub;
        std::multimap<juce::String, Entry> watchers;
        std::vector<std::pair<juce::String, Entry>> prefixWatchers;
    };
}
