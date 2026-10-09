#include "ProjectModel.h"

namespace integra
{
    namespace model
    {
        const Module* Block::findModule (const juce::String& moduleName) const
        {
            for (auto& m : modules)
                if (m.name == moduleName)
                    return &m;
            return nullptr;
        }

        const Envelope* Block::findEnvelopeForTarget (const juce::String& absoluteEndpointPath) const
        {
            for (auto& e : envelopes)
                if (e.targetPath == absoluteEndpointPath)
                    return &e;
            return nullptr;
        }

        const Track* Project::findTrack (const juce::String& p) const
        {
            for (auto& t : tracks)
                if (t.path == p)
                    return &t;
            return nullptr;
        }

        const Block* Project::findBlock (const juce::String& p) const
        {
            for (auto& t : tracks)
                for (auto& b : t.blocks)
                    if (b.path == p)
                        return &b;
            return nullptr;
        }

        const Track* Project::findTrackOfBlock (const juce::String& blockPath) const
        {
            return findTrack (parentPath (blockPath));
        }

        const Scene* Project::findScene (const juce::String& p) const
        {
            for (auto& s : scenes)
                if (s.path == p)
                    return &s;
            return nullptr;
        }

        const Container* Project::findContainer (const juce::String& p) const
        {
            if (p == path)
                return this;
            if (auto* t = findTrack (p))
                return t;
            return findBlock (p);
        }

        int Project::getLength() const
        {
            int length = 0;
            for (auto& t : tracks)
                for (auto& b : t.blocks)
                    length = juce::jmax (length, b.end);
            return length;
        }

        bool isStructuralEndpoint (const juce::String& name)
        {
            static const juce::StringArray names {
                "userData", "startTick", "tick", "value", "curvature", "sourcePath", "targetPath",
                "start", "length", "mode", "rate", "loop", "end", "info", "zIndex", "text",
                "inRangeMin", "inRangeMax", "outRangeMin", "outRangeMax", "inScale", "outScale", "inMode",
                "device", "channel", "messageType", "noteOrController", "autoLearn"
            };
            return names.contains (name);
        }
    }

    //==============================================================================
    namespace
    {
        using namespace model;

        juce::var endpointValue (const INode& node, const char* endpoint)
        {
            if (auto* e = node.get_node_endpoint (endpoint))
                return toVar (e->get_value());
            return {};
        }

        juce::String moduleNameOf (const INode& node)
        {
            return toString (node.get_interface_definition().get_interface_info().get_name());
        }

        void fillNode (Node& n, const INode& node)
        {
            n.name = toString (node.get_name());
            n.path = toString (node.get_path());
        }

        /** Child nodes sorted by name (libIntegra keeps them in an unordered map). */
        std::vector<const INode*> sortedChildren (const node_map& children)
        {
            std::vector<const INode*> result;
            for (auto& c : children)
                result.push_back (c.second);
            std::sort (result.begin(), result.end(), [] (const INode* a, const INode* b)
            {
                return toString (a->get_name()).compareNatural (toString (b->get_name())) < 0;
            });
            return result;
        }

        juce::String resolveRelative (const juce::String& containerPath, const juce::String& relative)
        {
            if (relative.isEmpty())
                return {};
            return containerPath.isEmpty() ? relative : containerPath + "." + relative;
        }

        Connection readConnection (const INode& node, const juce::String& containerPath)
        {
            Connection c;
            fillNode (c, node);
            c.sourceRelative = endpointValue (node, "sourcePath").toString();
            c.targetRelative = endpointValue (node, "targetPath").toString();
            c.sourcePath = resolveRelative (containerPath, c.sourceRelative);
            c.targetPath = resolveRelative (containerPath, c.targetRelative);
            c.active = (int) endpointValue (node, "active") != 0;
            return c;
        }

        Envelope readEnvelope (const INode& node)
        {
            Envelope e;
            fillNode (e, node);
            e.startTick = endpointValue (node, "startTick");

            for (auto* child : sortedChildren (node.get_children()))
            {
                if (moduleNameOf (*child) != "ControlPoint")
                    continue;

                ControlPoint p;
                fillNode (p, *child);
                p.tick = endpointValue (*child, "tick");
                p.value = (float) (double) endpointValue (*child, "value");
                p.curvature = (float) (double) endpointValue (*child, "curvature");
                e.points.push_back (p);
            }

            std::stable_sort (e.points.begin(), e.points.end(), [] (auto& a, auto& b) { return a.tick < b.tick; });
            return e;
        }

        Scaler readScaler (const INode& node)
        {
            Scaler s;
            fillNode (s, node);
            s.inMin = (float) (double) endpointValue (node, "inRangeMin");
            s.inMax = (float) (double) endpointValue (node, "inRangeMax");
            s.outMin = (float) (double) endpointValue (node, "outRangeMin");
            s.outMax = (float) (double) endpointValue (node, "outRangeMax");
            s.inScale = endpointValue (node, "inScale").toString();
            s.outScale = endpointValue (node, "outScale").toString();
            s.inMode = endpointValue (node, "inMode").toString();
            s.active = (int) endpointValue (node, "active") != 0;
            return s;
        }

        MidiControlInput readMidi (const INode& node)
        {
            MidiControlInput m;
            fillNode (m, node);
            m.device = endpointValue (node, "device").toString();
            m.channel = endpointValue (node, "channel");
            m.messageType = endpointValue (node, "messageType").toString();
            m.noteOrController = endpointValue (node, "noteOrController");
            m.autoLearn = (int) endpointValue (node, "autoLearn") != 0;
            return m;
        }

        Script readScript (const INode& node)
        {
            Script s;
            fillNode (s, node);
            s.text = endpointValue (node, "text").toString();
            s.info = endpointValue (node, "info").toString();
            return s;
        }

        /** Reads Connection/Scaler/MidiControlInput/Script children common to all containers.
            Returns true if the node was one of those. */
        bool readCommonChild (Container& container, const INode& child, const juce::String& type)
        {
            if (type == "Connection")         { container.connections.push_back (readConnection (child, container.path)); return true; }
            if (type == "Scaler")             { container.scalers.push_back (readScaler (child)); return true; }
            if (type == "MidiControlInput")   { container.midiControlInputs.push_back (readMidi (child)); return true; }
            if (type == "Script")             { container.scripts.push_back (readScript (child)); return true; }
            return false;
        }

        void readContainerBasics (Container& c, const INode& node)
        {
            fillNode (c, node);
            c.info = endpointValue (node, "info").toString();
            c.active = (int) endpointValue (node, "active") != 0;
        }

        /** Hooks scalers and MIDI inputs up to the connections around them. */
        void resolveRouting (Container& container)
        {
            for (auto& s : container.scalers)
            {
                for (auto& c : container.connections)
                {
                    if (c.targetPath == s.path + ".inValue")   { s.upstreamConnection = c.path;   s.sourcePath = c.sourcePath; }
                    if (c.sourcePath == s.path + ".outValue")  { s.downstreamConnection = c.path; s.targetPath = c.targetPath; }
                }
            }

            for (auto& m : container.midiControlInputs)
                for (auto& c : container.connections)
                    if (c.sourcePath == m.path + ".value")
                        m.outputConnection = c.path;
        }

        void resolveEnvelopeTargets (std::vector<Envelope>& envelopes, const std::vector<Connection>& connections,
                                     const std::vector<Connection>& projectConnections)
        {
            for (auto& e : envelopes)
            {
                for (auto& c : connections)
                {
                    if (c.sourcePath == e.path + ".currentValue")
                    {
                        e.targetPath = c.targetPath;
                        e.targetConnection = c.path;
                    }
                }

                for (auto& c : projectConnections)
                    if (c.targetPath == e.path + ".currentTick")
                        e.playerConnection = c.path;
            }
        }

        Block readBlock (const INode& node, const IServer&, const ModuleCatalogue& catalogue)
        {
            Block b;
            readContainerBasics (b, node);
            b.userData = BlockUserData::fromString (endpointValue (node, "userData").toString());

            for (auto* child : sortedChildren (node.get_children()))
            {
                const auto type = moduleNameOf (*child);
                if (readCommonChild (b, *child, type))
                    continue;

                if (type == "Envelope")
                {
                    b.envelopes.push_back (readEnvelope (*child));
                    continue;
                }

                Module m;
                fillNode (m, *child);
                m.entry = catalogue.findByGuid (child->get_interface_definition().get_module_guid());

                auto pos = b.userData.modulePositions.find (m.name);
                if (pos != b.userData.modulePositions.end())
                    m.position = pos->second;
                else
                    m.position = {};  // laid out by the module graph

                b.modules.push_back (m);
            }

            resolveRouting (b);
            return b;
        }

        Track readTrack (const INode& node, const IServer& server, const ModuleCatalogue& catalogue)
        {
            Track t;
            readContainerBasics (t, node);
            t.userData = TrackUserData::fromString (endpointValue (node, "userData").toString());

            for (auto* child : sortedChildren (node.get_children()))
            {
                const auto type = moduleNameOf (*child);
                if (readCommonChild (t, *child, type))
                    continue;

                if (type == "Container")
                    t.blocks.push_back (readBlock (*child, server, catalogue));
                else if (type == "Envelope")
                    t.blockEnvelopes.push_back (readEnvelope (*child));
            }

            resolveRouting (t);

            // A block's extent on the timeline comes from its block envelope: an Envelope in
            // the track, driving Block.active, with control points (-1,0) (0,1) (len-1,1) (len,0)
            for (auto& envelope : t.blockEnvelopes)
            {
                for (auto& c : t.connections)
                {
                    if (c.sourcePath != envelope.path + ".currentValue" || c.targetEndpoint() != "active")
                        continue;

                    envelope.targetPath = c.targetPath;
                    envelope.targetConnection = c.path;

                    for (auto& b : t.blocks)
                    {
                        if (b.path != c.targetNode())
                            continue;

                        b.blockEnvelopePath = envelope.path;
                        b.start = envelope.startTick;
                        b.end = envelope.startTick + (envelope.points.empty() ? 1 : juce::jmax (1, envelope.points.back().tick));
                    }
                }
            }

            std::stable_sort (t.blocks.begin(), t.blocks.end(), [] (auto& a, auto& b) { return a.start < b.start; });
            return t;
        }
    }

    //==============================================================================
    ProjectModel::ProjectModel (IntegraSession& s)  : session (s)
    {
        session.getNotifications().addListener (this);
    }

    ProjectModel::~ProjectModel()
    {
        session.getNotifications().removeListener (this);
    }

    void ProjectModel::rebuild()
    {
        model::Project p;

        if (session.isRunning())
        {
            auto server = session.lock();
            const auto& catalogue = session.getCatalogue();

            std::vector<std::pair<int, Track>> tracks;

            for (auto* top : sortedChildren (server->get_nodes()))
            {
                const auto type = moduleNameOf (*top);

                if (type == "AudioSettings")      p.audioSettingsPath = toString (top->get_path());
                else if (type == "MidiSettings")  p.midiSettingsPath = toString (top->get_path());
                else if (type == "Container" && ! p.valid)
                {
                    p.valid = true;
                    readContainerBasics (p, *top);
                    p.userData = ProjectUserData::fromString (endpointValue (*top, "userData").toString());

                    for (auto* child : sortedChildren (top->get_children()))
                    {
                        const auto childType = moduleNameOf (*child);
                        if (readCommonChild (p, *child, childType))
                            continue;

                        if (childType == "Container")
                        {
                            tracks.emplace_back ((int) endpointValue (*child, "zIndex"), readTrack (*child, *server, catalogue));
                        }
                        else if (childType == "Player" && p.player.path.isEmpty())
                        {
                            fillNode (p.player, *child);
                            p.player.rate = endpointValue (*child, "rate");
                            p.player.loop = (int) endpointValue (*child, "loop") != 0;
                            p.player.start = endpointValue (*child, "start");
                            p.player.end = endpointValue (*child, "end");
                            p.player.currentScene = endpointValue (*child, "scene").toString();

                            for (auto* sceneNode : sortedChildren (child->get_children()))
                            {
                                if (moduleNameOf (*sceneNode) != "Scene")
                                    continue;

                                Scene s;
                                fillNode (s, *sceneNode);
                                s.start = endpointValue (*sceneNode, "start");
                                s.length = endpointValue (*sceneNode, "length");
                                s.mode = endpointValue (*sceneNode, "mode").toString();
                                s.info = endpointValue (*sceneNode, "info").toString();
                                s.userData = SceneUserData::fromString (endpointValue (*sceneNode, "userData").toString());
                                p.scenes.push_back (s);
                            }
                        }
                        else if (childType == "MidiRawInput" && p.midiMonitorPath.isEmpty())
                        {
                            p.midiMonitorPath = toString (child->get_path());
                        }
                    }
                }
            }

            std::stable_sort (tracks.begin(), tracks.end(), [] (auto& a, auto& b) { return a.first < b.first; });
            for (auto& t : tracks)
                p.tracks.push_back (std::move (t.second));

            std::stable_sort (p.scenes.begin(), p.scenes.end(), [] (auto& a, auto& b) { return a.start < b.start; });

            resolveRouting (p);

            for (auto& t : p.tracks)
            {
                resolveEnvelopeTargets (t.blockEnvelopes, t.connections, p.connections);
                for (auto& b : t.blocks)
                    resolveEnvelopeTargets (b.envelopes, b.connections, p.connections);
            }
        }

        project = std::move (p);

        // drop selections that no longer exist
        if (project.findTrack (selectedTrack) == nullptr)
            selectedTrack = {};
        if (project.findBlock (selectedBlock) == nullptr)
            selectedBlock = {};
        if (auto* block = project.findBlock (selectedBlock))
        {
            if (block->findModule (lastElement (selectedModule)) == nullptr || parentPath (selectedModule) != selectedBlock)
                selectedModule = {};
        }
        else
        {
            selectedModule = {};
        }

        sendChangeMessage();
    }

    void ProjectModel::structureChanged()
    {
        rebuild();
    }

    void ProjectModel::endpointsChanged (const std::vector<NotificationHub::Change>& changes)
    {
        for (auto& change : changes)
        {
            const auto endpoint = lastElement (change.path);

            if (! model::isStructuralEndpoint (endpoint))
                continue;

            // "tick" and "value" are structural only for control points: they are also
            // Player.tick (40 times a second while playing) and MidiControlInput.value
            if ((endpoint == "tick" || endpoint == "value")
                 && session.getModuleName (parentPath (change.path)) != "ControlPoint")
                continue;

            rebuild();
            return;
        }
    }
}
