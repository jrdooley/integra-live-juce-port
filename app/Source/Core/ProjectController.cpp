#include "ProjectController.h"

namespace integra
{
    namespace
    {
        const char* const projectDefaultName = "Untitled";     // Project.defaultProjectName
        const char* const playerDefaultName = "Player1";       // Player.defaultPlayerName
        const char* const midiMonitorDefaultName = "MidiMonitor";

        /** Default info texts shown in the Info panel (InfoMarkupForViews defaults, shortened). */
        const char* const projectInfo = "# Integra Live project\n\nUse the *Arrange* view to lay out **tracks** and **blocks** over time, "
                                        "and the *Live* view to perform with the controls you choose to show.";
        const char* const trackInfo   = "A **track** holds blocks that play one after another.";
        const char* const blockInfo   = "A **block** contains modules. Double-click it to edit its modules in the *Module View*.";

        /** Range of a numeric endpoint from its constraint, if it has one. */
        bool getEndpointRange (IntegraSession& session, juce::String endpointPath, float& min, float& max, juce::String& scale)
        {
            auto server = session.lock();
            auto* endpoint = server->find_node_endpoint (toPath (endpointPath));
            if (endpoint == nullptr)
                return false;

            auto* control = endpoint->get_endpoint_definition().get_control_info();
            if (control == nullptr || control->get_type() != IControlInfo::STATEFUL)
                return false;

            auto* state = control->get_state_info();
            if (state->get_type() == CValue::STRING)
                return false;

            auto* range = state->get_constraint().get_value_range();
            if (range == nullptr)
                return false;

            min = (float) range->get_minimum();
            max = (float) range->get_maximum();
            scale = "linear";

            if (auto* vs = state->get_value_scale())
            {
                if (vs->get_scale_type() == IValueScale::EXPONENTIAL)   scale = "exponential";
                else if (vs->get_scale_type() == IValueScale::DECIBEL)  scale = "decibel";
            }
            return true;
        }
    }

    ProjectController::ProjectController (IntegraSession& s, ProjectModel& m)
        : session (s), model (m)
    {
    }

    //==============================================================================
    bool ProjectController::perform (juce::UndoableAction* action)
    {
        return undoManager.perform (action);
    }

    juce::String ProjectController::createNode (const GUID& guid, juce::String prefix, juce::String parentPath)
    {
        auto name = session.getNewChildName (parentPath, prefix, guid);
        auto* action = new CreateNodeAction (session, guid, name, parentPath);
        auto path = action->getPath();
        return perform (action) ? path : juce::String();
    }

    juce::String ProjectController::createNode (juce::String moduleName, juce::String prefix, juce::String parentPath)
    {
        if (auto* entry = session.getCatalogue().findByName (moduleName))
            return createNode (entry->guid, prefix, parentPath);

        jassertfalse;   // core module missing from the module directory?
        return {};
    }

    void ProjectController::setValueInTransaction (juce::String endpointPath, const juce::var& value)
    {
        perform (new SetValueAction (session, endpointPath, value));
    }

    juce::String ProjectController::relativeTo (juce::String containerPath, juce::String absolutePath) const
    {
        if (containerPath.isNotEmpty() && absolutePath.startsWith (containerPath + "."))
            return absolutePath.substring (containerPath.length() + 1);

        jassert (containerPath.isEmpty());
        return absolutePath;
    }

    juce::String ProjectController::addConnectionNode (juce::String containerPath, juce::String source, juce::String target)
    {
        auto connection = createNode ("Connection", "Connection", containerPath);
        if (connection.isEmpty())
            return {};

        if (source.isNotEmpty())  setValueInTransaction (connection + ".sourcePath", relativeTo (containerPath, source));
        if (target.isNotEmpty())  setValueInTransaction (connection + ".targetPath", relativeTo (containerPath, target));
        return connection;
    }

    //==============================================================================
    void ProjectController::ensureSettingsNodes()
    {
        auto top = session.getChildNames ({});
        bool haveAudio = false, haveMidi = false;
        for (auto& name : top)
        {
            auto type = session.getModuleName (name);
            haveAudio |= type == "AudioSettings";
            haveMidi |= type == "MidiSettings";
        }

        if (! haveAudio)    session.createNode ("AudioSettings", "AudioSettings1", {});
        if (! haveMidi)     session.createNode ("MidiSettings", "MidiSettings1", {});
    }

    juce::Result ProjectController::newProject()
    {
        play (false);
        undoManager.clearUndoHistory();

        if (model.get().valid)
            session.deleteNode (model.get().path);

        {
            auto server = session.lock();
            server->get_module_manager().unload_unused_embedded_modules();
        }
        session.refreshCatalogue();
        ensureSettingsNodes();

        if (session.createNode ("Container", projectDefaultName, {}) != CError::SUCCESS)
            return juce::Result::fail ("Couldn't create project: " + session.getLastError());

        session.createNode ("Player", playerDefaultName, projectDefaultName);
        session.createNode ("MidiRawInput", midiMonitorDefaultName, projectDefaultName);
        session.setValue (juce::String (projectDefaultName) + ".info", projectInfo);
        model.rebuild();

        auto track = addTrack();
        model.rebuild();
        addBlock (track, 0, newBlockSeconds * model.get().player.rate);
        model.rebuild();

        undoManager.clearUndoHistory();
        projectFile = juce::File();
        transactionCounter = savedTransactionCount = 0;
        return juce::Result::ok();
    }

    juce::Result ProjectController::newDemoProject()
    {
        auto result = newProject();
        if (result.failed())
            return result;

        auto files = session.getLocations().blockLibrary.findChildFiles (juce::File::findFiles, false, "*.integra");
        files.sort();

        // reuse the default track for the first block, replacing its default block
        auto& p = model.get();
        if (! p.tracks.empty())
            for (auto& b : p.tracks.front().blocks)
                session.deleteNode (b.blockEnvelopePath), session.deleteNode (b.path);
        model.rebuild();

        bool first = true;
        for (auto& f : files)
        {
            auto track = first && ! model.get().tracks.empty() ? model.get().tracks.front().path : addTrack();
            first = false;
            model.rebuild();
            auto r = importBlock (f, track, 0);
            if (r.failed())
                return r;
            model.rebuild();
        }

        // clean up connections left by the deleted default block
        juce::StringArray dangling;
        for (auto& c : model.get().connections)
            if (c.targetPath.isNotEmpty() && ! session.nodeExists (parentPath (c.targetPath)))
                dangling.add (c.path);
        for (auto& c : dangling)
            session.deleteNode (c);

        model.rebuild();
        undoManager.clearUndoHistory();
        transactionCounter = savedTransactionCount = 0;
        return juce::Result::ok();
    }

    juce::Result ProjectController::openProject (const juce::File& file)
    {
        if (! file.existsAsFile())
            return juce::Result::fail ("File not found: " + file.getFullPathName());

        play (false);
        undoManager.clearUndoHistory();

        if (model.get().valid)
            session.deleteNode (model.get().path);

        {
            auto server = session.lock();
            server->get_module_manager().unload_unused_embedded_modules();
        }
        session.refreshCatalogue();
        ensureSettingsNodes();

        if (session.loadFile (file, {}) != CError::SUCCESS)
        {
            auto error = session.getLastError();
            newProject();
            return juce::Result::fail ("Couldn't open " + file.getFileName() + ":\n" + error);
        }

        model.rebuild();

        // Files saved by the Flex GUI always have a Player and MidiMonitor; add them if
        // this file came from elsewhere (as ModelLoader.as did).
        auto& p = model.get();
        if (! p.valid)
        {
            newProject();
            return juce::Result::fail (file.getFileName() + " does not contain an Integra Live project");
        }

        if (p.player.path.isEmpty())
            session.createNode ("Player", playerDefaultName, p.path);
        if (p.midiMonitorPath.isEmpty())
            session.createNode ("MidiRawInput", midiMonitorDefaultName, p.path);

        // Reset blocks to the play position as ResetAllBlocks did
        session.setValue (model.get().player.path + ".tick", 0);
        model.rebuild();

        undoManager.clearUndoHistory();
        projectFile = file;
        transactionCounter = savedTransactionCount = 0;
        return juce::Result::ok();
    }

    juce::Result ProjectController::saveProject (const juce::File& file)
    {
        if (! model.get().valid)
            return juce::Result::fail ("No project to save");

        auto target = file.withFileExtension ("integra");
        if (session.saveFile (target, model.get().path) != CError::SUCCESS)
            return juce::Result::fail ("Couldn't save " + target.getFullPathName() + ":\n" + session.getLastError());

        projectFile = target;
        savedTransactionCount = transactionCounter;
        return juce::Result::ok();
    }

    //==============================================================================
    void ProjectController::setValue (juce::String endpointPath, const juce::var& value, bool undoable)
    {
        if (! undoable)
        {
            session.setValue (endpointPath, value);
            return;
        }

        // Successive sets of the same endpoint in quick succession (dragging a slider)
        // stay in one transaction, where SetValueAction coalesces them into one step.
        const auto now = juce::Time::getMillisecondCounter();
        if (endpointPath != lastSetPath || now - lastSetTime > 1000)
            undoManager.beginNewTransaction ("Set " + lastElement (endpointPath));

        lastSetPath = endpointPath;
        lastSetTime = now;
        ++transactionCounter;
        perform (new SetValueAction (session, endpointPath, value));
    }

    void ProjectController::bang (juce::String endpointPath)
    {
        session.bang (endpointPath);
    }

    juce::Result ProjectController::rename (juce::String path, juce::String newName)
    {
        auto name = newName.trim().retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_");
        if (name.isEmpty() || juce::CharacterFunctions::isDigit (name[0]))
            return juce::Result::fail ("Names must start with a letter and contain only letters, digits and underscores");

        if (name == lastElement (path) || (! path.contains (".") && name == path))
            return juce::Result::ok();

        if (session.getChildNames (parentPath (path)).contains (name))
            return juce::Result::fail ("There is already something called " + name + " here");

        const auto oldName = path.contains (".") ? lastElement (path) : path;
        const auto parent = parentPath (path);

        return transaction ("Rename " + oldName, [&]
        {
            if (! perform (new RenameNodeAction (session, path, name)))
                return juce::Result::fail (session.getLastError());

            // module names are keys in the owning block's userData
            if (auto* block = model.get().findBlock (parent))
            {
                if (block->findModule (oldName) != nullptr)
                {
                    auto data = block->userData;
                    data.renameModule (oldName, name);
                    setValueInTransaction (parent + ".userData", data.toString());
                }
            }

            model.rebuild();
            return juce::Result::ok();
        });
    }

    void ProjectController::setInfo (juce::String nodePath, juce::String markdown)
    {
        setValue (nodePath + ".info", markdown);
    }

    //==============================================================================
    void ProjectController::collectDependants (juce::String path, juce::StringArray& toDelete) const
    {
        auto& p = model.get();
        const auto prefix = path + ".";

        auto refersToDeleted = [&] (juce::String endpointPath)
        {
            return endpointPath.startsWith (prefix);
        };

        auto checkContainer = [&] (const model::Container& c)
        {
            for (auto& connection : c.connections)
            {
                if (connection.path.startsWith (prefix))
                    continue;   // deleted with its parent anyway

                if (refersToDeleted (connection.sourcePath) || refersToDeleted (connection.targetPath))
                    toDelete.addIfNotAlreadyThere (connection.path);
            }

            // a scaler or MIDI input whose routing is cut is useless on its own
            for (auto& scaler : c.scalers)
                if (refersToDeleted (scaler.sourcePath) || refersToDeleted (scaler.targetPath))
                    toDelete.addIfNotAlreadyThere (scaler.path);
        };

        checkContainer (p);
        for (auto& t : p.tracks)
        {
            checkContainer (t);

            for (auto& b : t.blocks)
            {
                checkContainer (b);

                // envelopes driving a deleted module go too
                for (auto& e : b.envelopes)
                    if (refersToDeleted (e.targetPath))
                        toDelete.addIfNotAlreadyThere (e.path);

                // deleting a block deletes its block envelope
                if (b.path == path && b.blockEnvelopePath.isNotEmpty())
                    toDelete.addIfNotAlreadyThere (b.blockEnvelopePath);
            }
        }
    }

    void ProjectController::removeInTransaction (const juce::StringArray& requested)
    {
        juce::StringArray toDelete (requested);

        // follow chains: a deleted scaler takes its connections, a deleted envelope its player connection
        for (int i = 0; i < toDelete.size(); ++i)
            collectDependants (toDelete[i], toDelete);

        // drop paths inside other deleted paths
        juce::StringArray roots;
        for (auto& candidate : toDelete)
        {
            bool nested = false;
            for (auto& other : toDelete)
                if (other != candidate && candidate.startsWith (other + "."))
                    nested = true;
            if (! nested)
                roots.add (candidate);
        }

        // tidy userData of blocks losing modules
        std::map<juce::String, BlockUserData> blockData;
        for (auto& path : roots)
        {
            auto parent = parentPath (path);
            if (auto* block = model.get().findBlock (parent))
            {
                if (block->findModule (lastElement (path)) != nullptr)
                {
                    if (blockData.find (parent) == blockData.end())
                        blockData[parent] = block->userData;
                    blockData[parent].removeModule (lastElement (path));
                }
            }
        }

        for (auto& [blockPath, data] : blockData)
            setValueInTransaction (blockPath + ".userData", data.toString());

        for (auto& path : roots)
            perform (new DeleteNodeAction (session, path));

        model.rebuild();
    }

    void ProjectController::remove (juce::String path)
    {
        remove (juce::StringArray (path));
    }

    void ProjectController::remove (const juce::StringArray& paths)
    {
        if (paths.isEmpty())
            return;

        transaction ("Delete", [&] { removeInTransaction (paths); });
    }

    //==============================================================================
    juce::Colour ProjectController::newTrackColour() const
    {
        // AddTrack.getNewTrackColor: cycle through the default palette
        static const juce::uint32 defaults[] = { 0x0F8AEB, 0xEE7511, 0xD449D0, 0xAB9800, 0xFF5848, 0x2EB429 };
        const auto n = model.get().tracks.size();
        return juce::Colour (0xff000000 | defaults[n % juce::numElementsInArray (defaults)]);
    }

    juce::String ProjectController::addTrack()
    {
        auto& p = model.get();
        if (! p.valid)
            return {};

        return transaction ("Add Track", [&]
        {
            int zIndex = 0;
            for (auto& t : p.tracks)
                zIndex = juce::jmax (zIndex, (int) session.getValue (t.path + ".zIndex") + 1);

            auto colour = newTrackColour();
            auto track = createNode ("Container", "Track", p.path);
            if (track.isEmpty())
                return juce::String();

            setValueInTransaction (track + ".zIndex", zIndex);

            TrackUserData data;
            data.colour = colour.getARGB() & 0xffffff;
            setValueInTransaction (track + ".userData", data.toString());
            setValueInTransaction (track + ".info", trackInfo);

            model.rebuild();
            model.selectedTrack = track;
            return track;
        });
    }

    void ProjectController::setTrackColour (juce::String trackPath, juce::Colour colour)
    {
        updateTrackUserData (trackPath, [colour] (TrackUserData& d) { d.colour = colour.getARGB() & 0xffffff; });
    }

    void ProjectController::setTrackHeight (juce::String trackPath, int height)
    {
        updateTrackUserData (trackPath, [height] (TrackUserData& d) { d.arrangeViewHeight = juce::jlimit (24, 600, height); });
    }

    void ProjectController::moveTrack (juce::String trackPath, int newIndex)
    {
        auto tracks = model.get().tracks;
        auto it = std::find_if (tracks.begin(), tracks.end(), [&] (auto& t) { return t.path == trackPath; });
        if (it == tracks.end())
            return;

        auto moved = *it;
        tracks.erase (it);
        newIndex = juce::jlimit (0, (int) tracks.size(), newIndex);
        tracks.insert (tracks.begin() + newIndex, moved);

        transaction ("Reorder Tracks", [&]
        {
            for (size_t i = 0; i < tracks.size(); ++i)
                if ((int) session.getValue (tracks[i].path + ".zIndex") != (int) i)
                    setValueInTransaction (tracks[i].path + ".zIndex", (int) i);
            model.rebuild();
        });
    }

    //==============================================================================
    juce::String ProjectController::addBlockEnvelope (juce::String trackPath, juce::String blockPath, int start, int end)
    {
        auto& p = model.get();

        auto envelope = createNode ("Envelope", "BlockEnvelope", trackPath);
        setValueInTransaction (envelope + ".startTick", start);

        // control points (-1,0) (0,1) (len-1,1) (len,0): block active from start to end
        const int ticks[] = { -1, 0, end - start - 1, end - start };
        const float values[] = { 0.0f, 1.0f, 1.0f, 0.0f };
        for (int i = 0; i < 4; ++i)
        {
            auto point = envelope + ".ControlPoint" + juce::String (i + 1);
            if (auto* entry = session.getCatalogue().findByName ("ControlPoint"))
                perform (new CreateNodeAction (session, entry->guid, "ControlPoint" + juce::String (i + 1), envelope));
            setValueInTransaction (point + ".tick", ticks[i]);
            setValueInTransaction (point + ".value", values[i]);
        }

        // Player.tick -> envelope.currentTick (project level), envelope.currentValue -> block.active (track level)
        addConnectionNode (p.path, p.player.path + ".tick", envelope + ".currentTick");
        addConnectionNode (trackPath, envelope + ".currentValue", blockPath + ".active");
        return envelope;
    }

    juce::String ProjectController::addBlock (juce::String trackPath, int startTick, int endTick, bool withDefaultContents)
    {
        if (model.get().findTrack (trackPath) == nullptr || endTick <= startTick)
            return {};

        startTick = juce::jmax (0, startTick);

        return transaction ("Add Block", [&]
        {
            auto block = createNode ("Container", "Block", trackPath);
            if (block.isEmpty())
                return juce::String();

            addBlockEnvelope (trackPath, block, startTick, endTick);
            setValueInTransaction (block + ".info", blockInfo);

            if (withDefaultContents)
            {
                model.rebuild();
                auto* in = session.getCatalogue().findByName ("AudioIn");
                auto* out = session.getCatalogue().findByName ("StereoAudioOut");

                // AddBlock.createDefaultBlockContents positions
                const float margin = moduleWidth / 8, top = moduleWidth * 3 / 4;
                if (out) addModule (block, out->guid, { moduleWidth * 4 + margin, top });
                if (in)  addModule (block, in->guid, { margin, top });
            }

            model.rebuild();
            model.selectedTrack = trackPath;
            model.selectedBlock = block;
            return block;
        });
    }

    void ProjectController::repositionBlock (juce::String blockPath, int start, int end)
    {
        auto* block = model.get().findBlock (blockPath);
        if (block == nullptr || start < 0 || end <= start || block->blockEnvelopePath.isEmpty())
            return;

        const auto envelope = block->blockEnvelopePath;
        auto* track = model.get().findTrackOfBlock (blockPath);
        const model::Envelope* blockEnvelope = nullptr;
        for (auto& e : track->blockEnvelopes)
            if (e.path == envelope)
                blockEnvelope = &e;

        if (blockEnvelope == nullptr || blockEnvelope->points.size() != 4)
            return;

        const auto point3 = blockEnvelope->points[2].path, point4 = blockEnvelope->points[3].path;
        const int oldEnd = block->end;
        const int newLength = end - start;

        // envelopes inside the block move with it; points beyond the new end are removed
        std::vector<juce::String> innerEnvelopes;
        juce::StringArray pointsToRemove;
        for (auto& e : block->envelopes)
        {
            innerEnvelopes.push_back (e.path);
            for (auto& pt : e.points)
                if (pt.tick > newLength)
                    pointsToRemove.add (pt.path);
        }

        transaction ("Move Block", [&]
        {
            for (auto& pt : pointsToRemove)
                perform (new DeleteNodeAction (session, pt));

            setValueInTransaction (envelope + ".startTick", start);

            // RepositionBlock: when growing, move the end point first so the block
            // doesn't flick inactive while the playhead is inside it
            if (end > oldEnd)
            {
                setValueInTransaction (point4 + ".tick", newLength);
                setValueInTransaction (point3 + ".tick", newLength - 1);
            }
            else
            {
                setValueInTransaction (point3 + ".tick", newLength - 1);
                setValueInTransaction (point4 + ".tick", newLength);
            }

            for (auto& e : innerEnvelopes)
                setValueInTransaction (e + ".startTick", start);

            model.rebuild();
        });
    }

    juce::String ProjectController::moveBlockToTrack (juce::String blockPath, juce::String trackPath)
    {
        auto* block = model.get().findBlock (blockPath);
        auto* fromTrack = model.get().findTrackOfBlock (blockPath);
        if (block == nullptr || fromTrack == nullptr || fromTrack->path == trackPath || model.get().findTrack (trackPath) == nullptr)
            return blockPath;

        const auto envelope = block->blockEnvelopePath;
        const auto blockName = lastElement (blockPath);

        if (session.getChildNames (trackPath).contains (blockName)
             || session.getChildNames (trackPath).contains (lastElement (envelope)))
            return blockPath;   // name clash; Flex renamed in this case - keep it simple and refuse

        return transaction ("Move Block To Track", [&]
        {
            // The track-level connection envelope->active must live in the new track:
            // libIntegra re-targets paths on move, but the connection itself stays behind.
            juce::String activeConnection;
            for (auto& c : fromTrack->connections)
                if (c.sourcePath == envelope + ".currentValue")
                    activeConnection = c.path;

            if (activeConnection.isNotEmpty())
                perform (new DeleteNodeAction (session, activeConnection));

            perform (new MoveNodeAction (session, blockPath, trackPath));
            if (envelope.isNotEmpty())
                perform (new MoveNodeAction (session, envelope, trackPath));

            const auto newBlock = trackPath + "." + blockName;
            const auto newEnvelope = trackPath + "." + lastElement (envelope);
            if (envelope.isNotEmpty())
                addConnectionNode (trackPath, newEnvelope + ".currentValue", newBlock + ".active");

            model.rebuild();
            model.selectedTrack = trackPath;
            model.selectedBlock = newBlock;
            return newBlock;
        });
    }

    juce::Result ProjectController::importBlock (const juce::File& file, juce::String trackPath, int startTick, juce::String* newBlockPath)
    {
        if (model.get().findTrack (trackPath) == nullptr)
            return juce::Result::fail ("No track selected");

        auto before = session.getChildNames (trackPath);

        return transaction ("Import Block", [&]
        {
            if (session.loadFile (file, trackPath) != CError::SUCCESS)
                return juce::Result::fail ("Couldn't import " + file.getFileName() + ":\n" + session.getLastError());

            juce::String imported;
            for (auto& name : session.getChildNames (trackPath))
                if (! before.contains (name))
                    imported = trackPath + "." + name;

            if (imported.isEmpty() || session.getModuleName (imported) != "Container")
            {
                if (imported.isNotEmpty())
                    session.deleteNode (imported);
                return juce::Result::fail (file.getFileName() + " is not an Integra Live block");
            }

            // record the load as an undoable "create" so undo removes it
            struct LoadedNodeAction  : IntegraAction
            {
                LoadedNodeAction (IntegraSession& s, juce::String p, juce::File f)
                    : IntegraAction (s), path (p), source (f) {}
                bool perform() override { return firstTime ? ! (firstTime = false) : session.loadFile (source, parentPath (path)) == CError::SUCCESS; }
                bool undo() override    { return session.deleteNode (path) == CError::SUCCESS; }
                juce::String path; juce::File source; bool firstTime = true;
            };
            perform (new LoadedNodeAction (session, imported, file));

            // Blocks exported from the Flex GUI don't carry their block envelope (it lives
            // in the track), so give the imported block one.
            int length = newBlockSeconds * model.get().player.rate;
            model.rebuild();
            addBlockEnvelope (trackPath, imported, startTick, startTick + length);

            model.rebuild();
            model.selectedTrack = trackPath;
            model.selectedBlock = imported;
            if (newBlockPath != nullptr)
                *newBlockPath = imported;
            return juce::Result::ok();
        });
    }

    juce::Result ProjectController::exportNode (juce::String path, const juce::File& file)
    {
        if (session.saveFile (file.withFileExtension ("integra"), path) != CError::SUCCESS)
            return juce::Result::fail ("Couldn't export:\n" + session.getLastError());
        return juce::Result::ok();
    }

    //==============================================================================
    float ProjectController::moduleHeight (const ModuleCatalogue::Entry* entry)
    {
        // ModuleInstance.getModuleHeight: max(3, one grid unit per pin)
        if (entry == nullptr)
            return 3.0f;
        return juce::jmax (3.0f, (float) juce::jmax (entry->numAudioInputs, entry->numAudioOutputs));
    }

    juce::String ProjectController::addModule (juce::String blockPath, const GUID& moduleGuid, juce::Point<float> gridPosition)
    {
        auto* block = model.get().findBlock (blockPath);
        auto* entry = session.getCatalogue().findByGuid (moduleGuid);
        if (block == nullptr || entry == nullptr)
            return {};

        auto data = block->userData;

        return transaction ("Add " + entry->label, [&]
        {
            auto module = createNode (moduleGuid, entry->name, blockPath);
            if (module.isEmpty())
                return juce::String();

            data.modulePositions[lastElement (module)] = { gridPosition.x, gridPosition.y, moduleWidth, moduleHeight (entry) };
            setValueInTransaction (blockPath + ".userData", data.toString());

            model.rebuild();
            model.selectedModule = module;
            return module;
        });
    }

    void ProjectController::setModulePositions (juce::String blockPath, const std::map<juce::String, juce::Rectangle<float>>& positions)
    {
        updateBlockUserData (blockPath, [&] (BlockUserData& d)
        {
            for (auto& [name, r] : positions)
                d.modulePositions[name] = r;
        });
    }

    juce::String ProjectController::connect (juce::String containerPath, juce::String sourceEndpoint, juce::String targetEndpoint)
    {
        if (auto* c = model.get().findContainer (containerPath))
            for (auto& existing : c->connections)
                if (existing.sourcePath == sourceEndpoint && existing.targetPath == targetEndpoint)
                    return existing.path;

        return transaction ("Connect", [&]
        {
            auto connection = addConnectionNode (containerPath, sourceEndpoint, targetEndpoint);
            model.rebuild();
            return connection;
        });
    }

    //==============================================================================
    void ProjectController::initialiseScalerRanges (juce::String scaler, juce::String source, juce::String target)
    {
        float inMin = 0, inMax = 1, outMin = 0, outMax = 1;
        juce::String inScale = "linear", outScale = "linear";

        if (source.isNotEmpty() && ! getEndpointRange (session, source, inMin, inMax, inScale))
            inMin = 0, inMax = 1;
        if (target.isNotEmpty() && ! getEndpointRange (session, target, outMin, outMax, outScale))
            outMin = 0, outMax = 1;

        // MIDI values are always 0..127
        if (source.endsWith (".value") && session.getModuleName (parentPath (source)) == "MidiControlInput")
            inMin = 0, inMax = 127, inScale = "linear";

        setValueInTransaction (scaler + ".inRangeMin", inMin);
        setValueInTransaction (scaler + ".inRangeMax", inMax);
        setValueInTransaction (scaler + ".outRangeMin", outMin);
        setValueInTransaction (scaler + ".outRangeMax", outMax);
        setValueInTransaction (scaler + ".inScale", inScale);
        setValueInTransaction (scaler + ".outScale", outScale);
    }

    juce::String ProjectController::addRouting (juce::String containerPath, juce::String sourceEndpoint, juce::String targetEndpoint)
    {
        return transaction ("Add Routing", [&]
        {
            auto scaler = createNode ("Scaler", "Scaler", containerPath);
            if (scaler.isEmpty())
                return juce::String();

            initialiseScalerRanges (scaler, sourceEndpoint, targetEndpoint);
            addConnectionNode (containerPath, sourceEndpoint, scaler + ".inValue");
            addConnectionNode (containerPath, scaler + ".outValue", targetEndpoint);
            model.rebuild();
            return scaler;
        });
    }

    void ProjectController::setRoutingSource (juce::String scalerPath, juce::String sourceEndpoint)
    {
        auto* container = model.get().findContainer (parentPath (scalerPath));
        if (container == nullptr)
            return;

        for (auto& s : container->scalers)
        {
            if (s.path != scalerPath || s.upstreamConnection.isEmpty())
                continue;

            const auto upstream = s.upstreamConnection, target = s.targetPath;
            transaction ("Set Routing Source", [&]
            {
                setValueInTransaction (upstream + ".sourcePath", relativeTo (container->path, sourceEndpoint));
                initialiseScalerRanges (scalerPath, sourceEndpoint, target);
                model.rebuild();
            });
            return;
        }
    }

    void ProjectController::setRoutingTarget (juce::String scalerPath, juce::String targetEndpoint)
    {
        auto* container = model.get().findContainer (parentPath (scalerPath));
        if (container == nullptr)
            return;

        for (auto& s : container->scalers)
        {
            if (s.path != scalerPath || s.downstreamConnection.isEmpty())
                continue;

            const auto downstream = s.downstreamConnection, source = s.sourcePath;
            transaction ("Set Routing Target", [&]
            {
                setValueInTransaction (downstream + ".targetPath", relativeTo (container->path, targetEndpoint));
                initialiseScalerRanges (scalerPath, source, targetEndpoint);
                model.rebuild();
            });
            return;
        }
    }

    //==============================================================================
    juce::String ProjectController::addMidiControl (juce::String containerPath, juce::String targetEndpoint, bool learn)
    {
        return transaction ("Add MIDI Control", [&]
        {
            auto midi = createNode ("MidiControlInput", "MidiControlInput", containerPath);
            if (midi.isEmpty())
                return juce::String();

            // ConfigureMidiControlInput( ANY_DEVICE, channel 0 (any), CC, 0 )
            setValueInTransaction (midi + ".device", juce::String());
            setValueInTransaction (midi + ".channel", 0);
            setValueInTransaction (midi + ".messageType", "cc");
            setValueInTransaction (midi + ".noteOrController", 0);

            auto scaler = createNode ("Scaler", "Scaler", containerPath);
            initialiseScalerRanges (scaler, midi + ".value", targetEndpoint);
            addConnectionNode (containerPath, midi + ".value", scaler + ".inValue");
            addConnectionNode (containerPath, scaler + ".outValue", targetEndpoint);

            if (learn)
                setValueInTransaction (midi + ".autoLearn", 1);

            model.rebuild();
            return midi;
        });
    }

    //==============================================================================
    juce::String ProjectController::addEnvelope (juce::String blockPath, juce::String targetEndpoint)
    {
        auto* block = model.get().findBlock (blockPath);
        if (block == nullptr || block->findEnvelopeForTarget (targetEndpoint) != nullptr)
            return {};

        const int start = block->start;
        const auto startValue = session.getValue (targetEndpoint);
        if (! (startValue.isInt() || startValue.isDouble()))
            return {};   // envelopes only drive numeric endpoints

        const auto projectPath = model.get().path, playerPath = model.get().player.path;

        return transaction ("Add Envelope", [&]
        {
            auto envelope = createNode ("Envelope", "Envelope", blockPath);
            if (envelope.isEmpty())
                return juce::String();

            setValueInTransaction (envelope + ".startTick", start);
            addConnectionNode (projectPath, playerPath + ".tick", envelope + ".currentTick");
            addConnectionNode (blockPath, envelope + ".currentValue", targetEndpoint);

            auto point = createNode ("ControlPoint", "ControlPoint", envelope);
            setValueInTransaction (point + ".tick", 0);
            setValueInTransaction (point + ".value", (double) startValue);

            model.rebuild();
            return envelope;
        });
    }

    juce::String ProjectController::addControlPoint (juce::String envelopePath, int tick, float value)
    {
        return transaction ("Add Control Point", [&]
        {
            auto point = createNode ("ControlPoint", "ControlPoint", envelopePath);
            if (point.isEmpty())
                return juce::String();

            setValueInTransaction (point + ".tick", juce::jmax (0, tick));
            setValueInTransaction (point + ".value", value);
            model.rebuild();
            return point;
        });
    }

    void ProjectController::moveControlPoint (juce::String controlPointPath, int tick, float value)
    {
        transaction ("Move Control Point", [&]
        {
            setValueInTransaction (controlPointPath + ".tick", juce::jmax (0, tick));
            setValueInTransaction (controlPointPath + ".value", value);
        });
    }

    void ProjectController::setControlPointCurvature (juce::String controlPointPath, float curvature)
    {
        setValue (controlPointPath + ".curvature", juce::jlimit (-2.0f, 2.0f, curvature));
    }

    //==============================================================================
    juce::String ProjectController::addScene (int startTick, int length, const SceneRanges& moved)
    {
        auto& p = model.get();
        if (p.player.path.isEmpty())
            return {};

        // first unused key binding, as AddScene.getUnusedKeybinding
        juce::String key;
        for (auto c : juce::String ("1234567890abcdefghijklmnopqrstuvwxyz"))
        {
            bool used = false;
            for (auto& s : p.scenes)
                used |= s.userData.keybinding == juce::String::charToString (c);
            if (! used)
            {
                key = juce::String::charToString (c);
                break;
            }
        }

        const auto player = p.player.path;
        return transaction ("Add Scene", [&]
        {
            repositionScenesInTransaction (moved);

            auto scene = createNode ("Scene", "Scene", player);
            if (scene.isEmpty())
                return juce::String();

            setValueInTransaction (scene + ".start", juce::jmax (0, startTick));
            setValueInTransaction (scene + ".length", juce::jmax (1, length));
            setValueInTransaction (scene + ".mode", "hold");

            SceneUserData data;
            data.keybinding = key;
            setValueInTransaction (scene + ".userData", data.toString());

            model.rebuild();
            return scene;
        });
    }

    void ProjectController::repositionScenesInTransaction (const SceneRanges& scenes)
    {
        for (auto& [path, range] : scenes)
        {
            auto* scene = model.get().findScene (path);
            if (scene == nullptr || (scene->start == range.getStart() && scene->length == range.getLength()))
                continue;
            setValueInTransaction (path + ".start", juce::jmax (0, range.getStart()));
            setValueInTransaction (path + ".length", juce::jmax (1, range.getLength()));
        }
    }

    void ProjectController::repositionScenes (const SceneRanges& scenes)
    {
        bool changed = false;
        for (auto& [path, range] : scenes)
            if (auto* scene = model.get().findScene (path))
                changed |= scene->start != range.getStart() || scene->length != range.getLength();
        if (! changed)
            return;

        transaction ("Move Scene", [&]
        {
            repositionScenesInTransaction (scenes);
            model.rebuild();
        });
    }

    void ProjectController::setScene (juce::String scenePath, int startTick, int length, juce::String mode)
    {
        transaction ("Edit Scene", [&]
        {
            setValueInTransaction (scenePath + ".start", juce::jmax (0, startTick));
            setValueInTransaction (scenePath + ".length", juce::jmax (1, length));
            setValueInTransaction (scenePath + ".mode", mode);
            model.rebuild();
        });
    }

    void ProjectController::setSceneKeybinding (juce::String scenePath, juce::String key)
    {
        // a key can only select one scene
        transaction ("Set Scene Key", [&]
        {
            for (auto& s : model.get().scenes)
            {
                if (s.path != scenePath && key.isNotEmpty() && s.userData.keybinding == key)
                {
                    auto d = s.userData;
                    d.keybinding = {};
                    setValueInTransaction (s.path + ".userData", d.toString());
                }
            }

            if (auto* scene = model.get().findScene (scenePath))
            {
                auto d = scene->userData;
                d.keybinding = key;
                setValueInTransaction (scenePath + ".userData", d.toString());
            }
            model.rebuild();
        });
    }

    void ProjectController::activateScene (juce::String scenePath)
    {
        auto& p = model.get();
        if (p.player.path.isNotEmpty())
            session.setValue (p.player.path + ".scene", scenePath.isEmpty() ? juce::String() : lastElement (scenePath));
    }

    void ProjectController::nextScene()       { if (model.get().player.path.isNotEmpty()) session.bang (model.get().player.path + ".next"); }
    void ProjectController::previousScene()   { if (model.get().player.path.isNotEmpty()) session.bang (model.get().player.path + ".prev"); }

    void ProjectController::play (bool shouldPlay)
    {
        auto& p = model.get();
        if (p.player.path.isNotEmpty())
            session.setValue (p.player.path + ".play", shouldPlay ? 1 : 0);
    }

    bool ProjectController::isPlaying()
    {
        auto& p = model.get();
        return p.player.path.isNotEmpty() && (int) session.getValue (p.player.path + ".play") != 0;
    }

    void ProjectController::stop()
    {
        play (false);
    }

    void ProjectController::returnToStart()
    {
        auto& p = model.get();
        if (p.player.path.isNotEmpty())
            session.bang (p.player.path + ".home");
    }

    void ProjectController::setPlayPosition (int tick)
    {
        auto& p = model.get();
        if (p.player.path.isNotEmpty())
            session.setValue (p.player.path + ".tick", juce::jmax (0, tick));
    }

    int ProjectController::getPlayPosition()
    {
        auto& p = model.get();
        return p.player.path.isNotEmpty() ? (int) session.getValue (p.player.path + ".tick") : 0;
    }

    //==============================================================================
    juce::String ProjectController::addScript (juce::String containerPath)
    {
        return transaction ("Add Script", [&]
        {
            auto script = createNode ("Script", "Script", containerPath);
            model.rebuild();
            return script;
        });
    }

    void ProjectController::setScriptText (juce::String scriptPath, juce::String text)
    {
        transaction ("Edit Script", [&] { setValueInTransaction (scriptPath + ".text", text); });
    }

    void ProjectController::runScript (juce::String scriptPath)
    {
        session.bang (scriptPath + ".trigger");
    }

    //==============================================================================
    void ProjectController::updateBlockUserData (juce::String blockPath, std::function<void (BlockUserData&)> fn)
    {
        auto* block = model.get().findBlock (blockPath);
        if (block == nullptr)
            return;

        auto data = block->userData;
        fn (data);
        auto text = data.toString();
        if (text != block->userData.toString())
            transaction ("Edit Block", [&] { setValueInTransaction (blockPath + ".userData", text); model.rebuild(); });
    }

    void ProjectController::updateTrackUserData (juce::String trackPath, std::function<void (TrackUserData&)> fn)
    {
        auto* track = model.get().findTrack (trackPath);
        if (track == nullptr)
            return;

        auto data = track->userData;
        fn (data);
        auto text = data.toString();
        if (text != track->userData.toString())
            transaction ("Edit Track", [&] { setValueInTransaction (trackPath + ".userData", text); model.rebuild(); });
    }

    void ProjectController::updateProjectUserData (std::function<void (ProjectUserData&)> fn, bool undoable)
    {
        auto& p = model.get();
        if (! p.valid)
            return;

        auto data = p.userData;
        fn (data);
        auto text = data.toString();
        if (text == p.userData.toString())
            return;

        if (undoable)
            transaction ("Edit Project", [&] { setValueInTransaction (p.path + ".userData", text); });
        else
            session.setValue (p.path + ".userData", text);

        model.rebuild();
    }

    void ProjectController::updateSceneUserData (juce::String scenePath, std::function<void (SceneUserData&)> fn)
    {
        auto* scene = model.get().findScene (scenePath);
        if (scene == nullptr)
            return;

        auto data = scene->userData;
        fn (data);
        transaction ("Edit Scene", [&] { setValueInTransaction (scenePath + ".userData", data.toString()); model.rebuild(); });
    }

    void ProjectController::setLiveViewControl (juce::String modulePath, juce::String widgetLabel, bool shown, juce::Rectangle<float> bounds)
    {
        const auto moduleName = lastElement (modulePath);
        updateBlockUserData (parentPath (modulePath), [&] (BlockUserData& d)
        {
            auto& controls = d.liveViewControls;
            controls.erase (std::remove_if (controls.begin(), controls.end(), [&] (auto& c)
            {
                return c.moduleName == moduleName && c.widgetLabel == widgetLabel;
            }), controls.end());

            if (shown)
                controls.push_back ({ moduleName, widgetLabel, bounds });
        });
    }
}
