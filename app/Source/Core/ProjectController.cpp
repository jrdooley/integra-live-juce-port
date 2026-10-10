#include "ProjectController.h"
#include "EndpointInfo.h"

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

    juce::String ProjectController::projectNameFromFile (const juce::File& file)
    {
        auto name = file.getFileNameWithoutExtension()
                        .retainCharacters ("0123456789_abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");
        while (name.isNotEmpty() && juce::CharacterFunctions::isDigit (name[0]))
            name = name.substring (1);
        return name.isEmpty() ? juce::String (projectDefaultName) : name;
    }

    juce::Result ProjectController::saveProjectAs (const juce::File& file)
    {
        if (! model.get().valid)
            return juce::Result::fail ("No project to save");

        const auto target = file.withFileExtension ("integra");
        const auto name = projectNameFromFile (target);
        const auto oldName = model.get().path;
        if (name != oldName)
        {
            // keep the GUI's selection, which is held as paths
            auto remap = [&] (juce::String path)
            {
                return path == oldName ? name : path.startsWith (oldName + ".") ? name + path.substring (oldName.length()) : path;
            };
            const auto track = remap (model.selectedTrack), block = remap (model.selectedBlock),
                       module = remap (model.selectedModule), connection = remap (model.selectedConnection);

            auto renamed = rename (oldName, name);
            if (renamed.failed())
                return renamed;

            model.selectedTrack = track;
            model.selectedBlock = block;
            model.selectedModule = module;
            model.selectedConnection = connection;
        }
        return saveProject (target);
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
        // Inside a transaction() the set joins that transaction instead.
        const auto now = juce::Time::getMillisecondCounter();
        if (transactionDepth == 0 && (endpointPath != lastSetPath || now - lastSetTime > 1000))
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

        const auto oldName = path.contains (".") ? lastElement (path) : path;
        const auto parent = path.contains (".") ? parentPath (path) : juce::String();   // the project is top level

        if (session.getChildNames (parent).contains (name))
            return juce::Result::fail ("There is already something called " + name + " here");

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

    void ProjectController::setTrackExpanded (juce::String trackPath, bool expanded)
    {
        updateTrackUserData (trackPath, [expanded] (TrackUserData& d) { d.arrangeViewExpanded = expanded; });
    }

    void ProjectController::setTrackLiveViewExpanded (juce::String trackPath, bool expanded)
    {
        updateTrackUserData (trackPath, [expanded] (TrackUserData& d) { d.liveViewExpanded = expanded; });
    }

    void ProjectController::previewTrackColour (juce::String trackPath, juce::Colour colour)
    {
        if (auto* track = model.get().findTrack (trackPath))
        {
            auto data = track->userData;
            data.colour = colour.getARGB() & 0xffffff;
            session.setValue (trackPath + ".userData", data.toString());
            model.rebuild();
        }
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

    juce::String ProjectController::createBlock (juce::String trackPath, int startTick)
    {
        auto* track = model.get().findTrack (trackPath);
        if (track == nullptr)
            return {};

        startTick = juce::jmax (0, startTick);
        for (auto& b : track->blocks)
            if (b.start <= startTick && startTick < b.end)
                startTick = b.end;
        const int endTick = startTick + newBlockSeconds * juce::jmax (1, model.get().player.rate);

        return transaction ("Add Block", [&]
        {
            auto block = addBlock (trackPath, startTick, endTick);
            if (block.isEmpty())
                return block;

            // doBlockRescheduling with the new block as the dragged one
            std::map<juce::String, juce::Range<int>> ranges { { block, { startTick, endTick } } };
            if (auto* t = model.get().findTrack (trackPath))
                rescheduleBlocks (*t, block, true, ranges);
            ranges.erase (block);
            for (auto& [path, range] : ranges)
                repositionBlock (path, range.getStart(), range.getEnd());

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

    void ProjectController::repositionBlocks (juce::String blockPath, juce::String trackPath, const std::map<juce::String, juce::Range<int>>& ranges)
    {
        transaction ("Move Block", [&]
        {
            juce::String movedPath = blockPath;
            if (trackPath.isNotEmpty() && trackPath != parentPath (blockPath))
            {
                movedPath = moveBlockToTrack (blockPath, trackPath);
                if (movedPath == blockPath)
                    return;   // refused (name clash)
            }

            for (auto& [path, range] : ranges)
            {
                const auto current = path == blockPath ? movedPath : path;
                if (auto* b = model.get().findBlock (current))
                    if (b->start != range.getStart() || b->end != range.getEnd())
                        repositionBlock (current, range.getStart(), range.getEnd());
            }
        });
    }

    void ProjectController::repositionBlockGroup (const std::map<juce::String, juce::String>& targetTracks,
                                                  const std::map<juce::String, juce::Range<int>>& ranges)
    {
        transaction ("Move Blocks", [&]
        {
            std::map<juce::String, juce::String> moved;
            for (auto& [path, track] : targetTracks)
                if (track != parentPath (path))
                    moved[path] = moveBlockToTrack (path, track);

            for (auto& [path, range] : ranges)
            {
                const auto current = moved.count (path) > 0 ? moved[path] : path;
                if (auto* b = model.get().findBlock (current))
                    if (b->start != range.getStart() || b->end != range.getEnd())
                        repositionBlock (current, range.getStart(), range.getEnd());
            }
        });
    }

    void ProjectController::rescheduleBlocks (const model::Track& track, const juce::String& dragBlock, bool movingRight,
                                              std::map<juce::String, juce::Range<int>>& draggedBlocks)
    {
        const auto dragged = draggedBlocks[dragBlock];

        struct Span { juce::String path; int start, end; int length() const { return end - start; } };
        std::vector<Span> others;
        for (auto& b : track.blocks)
            if (b.path != dragBlock)
                others.push_back ({ b.path, b.start, b.end });
        std::stable_sort (others.begin(), others.end(), [] (const Span& a, const Span& b) { return a.start + a.end < b.start + b.end; });

        int insertion = (int) others.size();
        for (int i = 0; i < (int) others.size(); ++i)
        {
            if (movingRight ? others[(size_t) i].end > dragged.getStart() : others[(size_t) i].start >= dragged.getEnd())
            {
                insertion = i;
                break;
            }
        }

        int durationToLeft = 0;
        for (int i = 0; i < insertion; ++i)
            durationToLeft += others[(size_t) i].length();
        while (durationToLeft > dragged.getStart() && insertion > 0)
            durationToLeft -= others[(size_t) --insertion].length();

        int maximumEnd = dragged.getStart();
        for (int i = insertion - 1; i >= 0; --i)
        {
            auto& b = others[(size_t) i];
            if (b.end <= maximumEnd)
                break;
            draggedBlocks[b.path] = { maximumEnd - b.length(), maximumEnd };
            maximumEnd -= b.length();
        }

        int minimumStart = dragged.getEnd();
        for (int i = insertion; i < (int) others.size(); ++i)
        {
            auto& b = others[(size_t) i];
            if (b.start >= minimumStart)
                break;
            draggedBlocks[b.path] = { minimumStart, minimumStart + b.length() };
            minimumStart += b.length();
        }
    }

    //==============================================================================
    namespace
    {
        std::map<juce::String, EndpointInfo> endpointInfos (const IInterfaceDefinition* definition)
        {
            std::map<juce::String, EndpointInfo> result;
            if (definition != nullptr)
                for (auto* ep : definition->get_endpoint_definitions())
                    result[toString (ep->get_name())] = EndpointInfo::fromDefinition (*ep);
            return result;
        }

        bool isStateful (const EndpointInfo& e)
        {
            return e.kind == EndpointInfo::Kind::integer || e.kind == EndpointInfo::Kind::real || e.kind == EndpointInfo::Kind::text;
        }

        /** SwitchObjectVersion.isConnectionStillValid */
        bool connectionStillValid (const std::map<juce::String, EndpointInfo>& oldInfos, const std::map<juce::String, EndpointInfo>& newInfos,
                                   const juce::String& endpointName, bool isTarget)
        {
            auto oldIt = oldInfos.find (endpointName);
            auto newIt = newInfos.find (endpointName);
            if (oldIt == oldInfos.end() || newIt == newInfos.end())
                return false;
            auto& o = oldIt->second;
            auto& n = newIt->second;
            if (o.isAudioStream != n.isAudioStream)
                return false;
            if (n.isAudioStream)
                return isTarget ? n.isAudioInput : ! n.isAudioInput;
            if (n.kind == EndpointInfo::Kind::text)
                return false;
            return isTarget ? n.canBeTarget : n.canBeSource;
        }

        int levenshtein (const juce::String& a, const juce::String& b)
        {
            std::vector<int> row ((size_t) b.length() + 1);
            for (int j = 0; j <= b.length(); ++j)
                row[(size_t) j] = j;
            for (int i = 1; i <= a.length(); ++i)
            {
                int previous = row[0];
                row[0] = i;
                for (int j = 1; j <= b.length(); ++j)
                {
                    const int saved = row[(size_t) j];
                    row[(size_t) j] = juce::jmin (row[(size_t) j] + 1, row[(size_t) j - 1] + 1, previous + (a[i - 1] == b[j - 1] ? 0 : 1));
                    previous = saved;
                }
            }
            return row[(size_t) b.length()];
        }

        /** SwitchModuleVersion.enforceConstraint: fit an old value to the new endpoint. */
        juce::var fitToEndpoint (const juce::var& value, const EndpointInfo& e)
        {
            if (! e.allowedStates.isEmpty())
            {
                juce::String best;
                double bestDistance = 0.0;
                for (auto& allowed : e.allowedStates)
                {
                    const double distance = e.kind == EndpointInfo::Kind::text ? (double) levenshtein (value.toString(), allowed)
                                                                                : std::abs ((double) value - allowed.getDoubleValue());
                    if (best.isEmpty() || distance < bestDistance)
                        best = allowed, bestDistance = distance;
                }
                if (e.kind == EndpointInfo::Kind::integer)  return best.getIntValue();
                if (e.kind == EndpointInfo::Kind::real)     return best.getDoubleValue();
                return best;
            }

            switch (e.kind)
            {
                case EndpointInfo::Kind::integer:
                {
                    int v = juce::roundToInt ((double) value);
                    return e.hasRange ? juce::jlimit ((int) e.minimum, (int) e.maximum, v) : v;
                }
                case EndpointInfo::Kind::real:
                {
                    double v = (double) value;
                    return e.hasRange ? juce::jlimit (e.minimum, e.maximum, v) : v;
                }
                case EndpointInfo::Kind::text:
                {
                    auto s = value.toString();
                    if (e.hasRange)
                    {
                        while (s.length() < (int) e.minimum)
                            s << "*";
                        if (s.length() > (int) e.maximum)
                            s = s.substring (0, (int) e.maximum);
                    }
                    return s;
                }
                default:
                    return value;
            }
        }
    }

    juce::StringArray ProjectController::findInstances (const GUID& moduleGuid) const
    {
        juce::StringArray paths;
        for (auto& t : model.get().tracks)
            for (auto& b : t.blocks)
                for (auto& m : b.modules)
                    if (m.entry != nullptr && guidsEqual (m.entry->guid, moduleGuid))
                        paths.add (m.path);
        return paths;
    }

    bool ProjectController::hasUpgradableModules() const
    {
        auto& catalogue = session.getCatalogue();
        for (auto& t : model.get().tracks)
            for (auto& b : t.blocks)
                for (auto& m : b.modules)
                    if (m.entry != nullptr && catalogue.getBestVersion (*m.entry) != m.entry)
                        return true;
        return false;
    }

    bool ProjectController::switchModuleVersion (juce::String modulePath, const GUID& toGuid)
    {
        auto& catalogue = session.getCatalogue();
        auto* block = model.get().findBlock (parentPath (modulePath));
        auto* module = block != nullptr ? block->findModule (lastElement (modulePath)) : nullptr;
        auto* target = catalogue.findByGuid (toGuid);
        if (module == nullptr || module->entry == nullptr || target == nullptr
             || ! guidsEqual (module->entry->originGuid, target->originGuid) || guidsEqual (module->entry->guid, toGuid))
            return false;   // SwitchObjectVersion.initialize

        const auto oldInfos = endpointInfos (module->entry->definition);
        const auto newInfos = endpointInfos (target->definition);
        const auto prefix = modulePath + ".";

        // SwitchObjectVersion.removeObsoleteConnections
        juce::StringArray obsolete;
        auto check = [&] (const model::Container& c)
        {
            for (auto& s : c.scalers)
            {
                if ((s.sourcePath.startsWith (prefix) && ! connectionStillValid (oldInfos, newInfos, lastElement (s.sourcePath), false))
                     || (s.targetPath.startsWith (prefix) && ! connectionStillValid (oldInfos, newInfos, lastElement (s.targetPath), true)))
                    obsolete.addIfNotAlreadyThere (s.path);
            }
            for (auto& conn : c.connections)
            {
                if ((conn.sourcePath.startsWith (prefix) && ! connectionStillValid (oldInfos, newInfos, lastElement (conn.sourcePath), false))
                     || (conn.targetPath.startsWith (prefix) && ! connectionStillValid (oldInfos, newInfos, lastElement (conn.targetPath), true)))
                    obsolete.addIfNotAlreadyThere (conn.path);
            }
        };
        auto& p = model.get();
        check (p);
        for (auto& t : p.tracks)
        {
            check (t);
            for (auto& b : t.blocks)
            {
                check (b);
                // an envelope whose target goes away goes with it
                for (auto& e : b.envelopes)
                    if (e.targetPath.startsWith (prefix) && ! connectionStillValid (oldInfos, newInfos, lastElement (e.targetPath), true))
                        obsolete.addIfNotAlreadyThere (e.path);
            }
        }

        // SwitchModuleVersion.generateNewAttributeValues
        std::vector<std::pair<juce::String, juce::var>> values;
        for (auto& [name, info] : newInfos)
        {
            if (! isStateful (info))
                continue;
            auto old = oldInfos.find (name);
            auto value = old != oldInfos.end() && isStateful (old->second) ? fitToEndpoint (session.getValue (prefix + name), info)
                                                                           : info.defaultValue;
            // a data directory goes first: modules may not cope with it being set after file names
            if (name == "dataDirectory")
                values.insert (values.begin(), { name, value });
            else
                values.push_back ({ name, value });
        }

        // live view controls whose widget is gone (correctLiveViewControls)
        juce::StringArray newWidgets;
        if (target->definition != nullptr)
            for (auto* w : target->definition->get_widget_definitions())
                newWidgets.add (toString (w->get_label()));

        const auto blockPath = block->path, moduleName = module->name;
        const GUID newGuid = toGuid;

        transaction ("Switch Module Version", [&]
        {
            if (! obsolete.isEmpty())
            {
                removeInTransaction (obsolete);
                model.rebuild();
            }

            perform (new DeleteNodeAction (session, modulePath));
            perform (new CreateNodeAction (session, newGuid, moduleName, blockPath));
            for (auto& [name, value] : values)
                setValueInTransaction (prefix + name, value);

            updateBlockUserData (blockPath, [&] (BlockUserData& d)
            {
                auto& controls = d.liveViewControls;
                controls.erase (std::remove_if (controls.begin(), controls.end(), [&] (const BlockUserData::LiveViewControl& c)
                {
                    return c.moduleName == moduleName && ! newWidgets.contains (c.widgetLabel);
                }), controls.end());

                // correctModulePosition: the height follows the new version's pins
                auto it = d.modulePositions.find (moduleName);
                if (it != d.modulePositions.end())
                    it->second.setHeight (moduleHeight (target));
            });
            model.rebuild();
        });
        return true;
    }

    juce::StringArray ProjectController::switchAllVersions (const GUID& fromGuid, const GUID& toGuid)
    {
        juce::StringArray switched;
        transaction ("Switch Module Versions", [&]
        {
            for (auto& path : findInstances (fromGuid))
                if (switchModuleVersion (path, toGuid))
                    switched.add (path);
        });
        return switched;
    }

    ProjectController::UpgradeResult ProjectController::upgradeAll()
    {
        UpgradeResult result;
        if (! hasUpgradableModules())
            return result;

        // UpgradeModules.getBackupName: "<name> (backup <n>).integra" next to the project, or on the desktop
        auto directory = projectFile.existsAsFile() ? projectFile.getParentDirectory() : juce::File();
        for (auto fallback : { juce::File::userDesktopDirectory, juce::File::userDocumentsDirectory, juce::File::tempDirectory })
            if (! directory.isDirectory())
                directory = juce::File::getSpecialLocation (fallback);
        const auto name = projectFile.existsAsFile() ? projectFile.getFileNameWithoutExtension() : juce::String (projectDefaultName);
        for (int n = 1;; ++n)
        {
            result.backup = directory.getChildFile (name + " (backup " + juce::String (n) + ").integra");
            if (! result.backup.exists())
                break;
        }
        if (session.saveFile (result.backup, model.get().path) != CError::SUCCESS)
            result.backup = juce::File();

        auto& catalogue = session.getCatalogue();
        transaction ("Upgrade Modules", [&]
        {
            std::vector<GUID> outdated;
            for (auto& t : model.get().tracks)
                for (auto& b : t.blocks)
                    for (auto& m : b.modules)
                        if (m.entry != nullptr && catalogue.getBestVersion (*m.entry) != m.entry)
                            if (std::none_of (outdated.begin(), outdated.end(), [&] (const GUID& g) { return guidsEqual (g, m.entry->guid); }))
                                outdated.push_back (m.entry->guid);

            for (auto& from : outdated)
            {
                auto* entry = catalogue.findByGuid (from);
                auto switched = switchAllVersions (from, catalogue.getBestVersion (*entry)->guid);
                if (! switched.isEmpty())
                {
                    result.upgradedModules.addArray (switched);
                    result.upgradedFrom.push_back (from);
                }
            }
        });
        return result;
    }

    juce::Result ProjectController::installModule (const juce::File& moduleFile)
    {
        CModuleInstallResult result;
        CError error;
        {
            auto server = session.lock();
            error = server->get_module_manager().install_module (toStd (moduleFile.getFullPathName()), result);
        }
        if (error != CError::SUCCESS)
            return juce::Result::fail (moduleFile.getFileName() + ": " + toString (error.get_text()));

        // the catalogue's entries move, so the model (which points into it) must follow
        session.refreshCatalogue();
        model.rebuild();
        return juce::Result::ok();
    }

    juce::Result ProjectController::installModules (const juce::Array<juce::File>& files, juce::StringArray& installed)
    {
        juce::StringArray errors;
        auto install = [&] (const juce::File& moduleFile, const juce::String& title)
        {
            auto r = installModule (moduleFile);
            if (r.wasOk())
                installed.add (title);
            else
                errors.add (r.getErrorMessage());
        };

        for (auto& file : files)
        {
            if (file.hasFileExtension ("module"))
            {
                install (file, file.getFileNameWithoutExtension());
            }
            else if (file.hasFileExtension ("bundle"))
            {
                // InstallModules.unpackBundleFile: every .module in the zip
                juce::ZipFile zip (file);
                if (zip.getNumEntries() == 0)
                {
                    errors.add (file.getFileName() + " is not a module bundle");
                    continue;
                }
                auto folder = juce::File::createTempFile ("bundle");
                folder.createDirectory();
                for (int i = 0; i < zip.getNumEntries(); ++i)
                {
                    auto* entry = zip.getEntry (i);
                    if (entry == nullptr || ! entry->filename.endsWithIgnoreCase (".module"))
                        continue;
                    const auto entryName = entry->filename.fromLastOccurrenceOf ("/", false, false);
                    auto target = folder.getChildFile (entryName);
                    std::unique_ptr<juce::InputStream> in (zip.createStreamForEntry (i));
                    if (in == nullptr)
                        continue;
                    {
                        juce::FileOutputStream out (target);
                        out.writeFromInputStream (*in, -1);
                    }
                    install (target, entryName.upToLastOccurrenceOf (".", false, false));
                }
                folder.deleteRecursively();
            }
            else
            {
                errors.add (file.getFileName() + " is not a module or module bundle");
            }
        }

        return errors.isEmpty() ? juce::Result::ok() : juce::Result::fail (errors.joinIntoString ("\n"));
    }

    juce::Result ProjectController::installEmbeddedModule (const GUID& guid)
    {
        CError error;
        {
            auto server = session.lock();
            error = server->get_module_manager().install_embedded_module (guid);
        }
        if (error != CError::SUCCESS)
            return juce::Result::fail (toString (error.get_text()));

        session.refreshCatalogue();
        model.rebuild();
        return juce::Result::ok();
    }

    juce::Result ProjectController::uninstallModule (const GUID& guid, bool deleteInstances)
    {
        auto instances = findInstances (guid);
        if (deleteInstances && ! instances.isEmpty())
            remove (instances);

        CModuleUninstallResult result;
        CError error;
        {
            auto server = session.lock();
            error = server->get_module_manager().uninstall_module (guid, result);
        }

        session.refreshCatalogue();
        model.rebuild();
        undoManager.clearUndoHistory();
        markChanged();

        if (error != CError::SUCCESS)
            return juce::Result::fail (toString (error.get_text()));
        return juce::Result::ok();
    }

    juce::String ProjectController::describeVersionChange (const GUID& fromGuid, const GUID& toGuid) const
    {
        // ModuleManagementInfoGenerator.getDifferences, simplified
        auto& catalogue = session.getCatalogue();
        auto* from = catalogue.findByGuid (fromGuid);
        auto* to = catalogue.findByGuid (toGuid);
        if (from == nullptr || to == nullptr)
            return {};

        auto dateText = [] (juce::Time t) { return t.formatted ("%d %b %Y"); };
        juce::String md;
        md << "The **" << from->getSourceLabel() << "** version of **" << from->label << "** (updated " << dateText (from->modified)
           << ") will be swapped for the **" << to->getSourceLabel() << "** version (updated " << dateText (to->modified) << ").\n\n";

        const auto oldInfos = endpointInfos (from->definition);
        const auto newInfos = endpointInfos (to->definition);
        juce::StringArray added, removed, changed;
        for (auto& [name, info] : newInfos)
        {
            auto old = oldInfos.find (name);
            if (old == oldInfos.end())
                added.add (name);
            else if (old->second.kind != info.kind || old->second.isAudioStream != info.isAudioStream
                     || old->second.hasRange != info.hasRange || old->second.minimum != info.minimum || old->second.maximum != info.maximum
                     || old->second.allowedStates != info.allowedStates || old->second.defaultValue != info.defaultValue)
                changed.add (name);
        }
        for (auto& [name, info] : oldInfos)
            if (newInfos.find (name) == newInfos.end())
                removed.add (name);

        if (added.isEmpty() && removed.isEmpty() && changed.isEmpty())
            md << "No endpoints change.\n";
        if (! added.isEmpty())    md << "* Added: " << added.joinIntoString (", ") << "\n";
        if (! removed.isEmpty())  md << "* Removed: " << removed.joinIntoString (", ") << " (connections, envelopes and live controls using them are removed)\n";
        if (! changed.isEmpty())  md << "* Changed type, range or default: " << changed.joinIntoString (", ") << " (values are fitted to the new ranges)\n";
        if (to->description != from->description && to->description.isNotEmpty())
            md << "\n" << to->description << "\n";
        return md;
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

    namespace
    {
        /** A node loaded from a file, as an undoable "create": undo deletes it, redo loads it again. */
        struct LoadedNodeAction  : IntegraAction
        {
            LoadedNodeAction (IntegraSession& s, juce::String p, juce::File f)
                : IntegraAction (s), path (p), source (f) {}
            bool perform() override { return firstTime ? ! (firstTime = false) : session.loadFile (source, parentPath (path)) == CError::SUCCESS; }
            bool undo() override    { return session.deleteNode (path) == CError::SUCCESS; }
            juce::String path; juce::File source; bool firstTime = true;
        };
    }

    juce::Result ProjectController::importTrack (const juce::File& file, juce::String* newTrackPath)
    {
        auto& p = model.get();
        if (! p.valid)
            return juce::Result::fail ("No project");

        const auto projectPath = p.path, playerPath = p.player.path;
        auto before = session.getChildNames (projectPath);
        int zIndex = 0;
        for (auto& t : p.tracks)
            zIndex = juce::jmax (zIndex, (int) session.getValue (t.path + ".zIndex") + 1);

        return transaction ("Import Track", [&]
        {
            if (session.loadFile (file, projectPath) != CError::SUCCESS)
                return juce::Result::fail ("Cannot import \"" + file.getFileName() + "\":\n" + session.getLastError());

            juce::String imported;
            for (auto& name : session.getChildNames (projectPath))
                if (! before.contains (name))
                    imported = projectPath + "." + name;

            // a track holds blocks, envelopes and routing; a block file has modules in it
            bool isTrack = imported.isNotEmpty() && session.getModuleName (imported) == "Container";
            if (isTrack)
                for (auto& child : session.getChildNames (imported))
                    if (! juce::StringArray { "Container", "Envelope", "Connection", "Scaler", "Script", "MidiControlInput" }
                              .contains (session.getModuleName (imported + "." + child)))
                        isTrack = false;
            if (! isTrack)
            {
                if (imported.isNotEmpty())
                    session.deleteNode (imported);
                return juce::Result::fail ("This file cannot be imported as a track");
            }

            perform (new LoadedNodeAction (session, imported, file));

            // ImportTrack.onLoaded: the player drives every envelope in it, and it goes last
            std::function<void (const juce::String&)> connectEnvelopes = [&] (const juce::String& container)
            {
                for (auto& child : session.getChildNames (container))
                {
                    const auto path = container + "." + child;
                    const auto kind = session.getModuleName (path);
                    if (kind == "Envelope" && playerPath.isNotEmpty())
                        addConnectionNode (projectPath, playerPath + ".tick", path + ".currentTick");
                    else if (kind == "Container")
                        connectEnvelopes (path);
                }
            };
            connectEnvelopes (imported);
            setValueInTransaction (imported + ".zIndex", zIndex);

            model.rebuild();
            model.selectedTrack = imported;
            model.selectedBlock = {};
            if (newTrackPath != nullptr)
                *newTrackPath = imported;
            return juce::Result::ok();
        });
    }

    juce::Result ProjectController::importBlock (const juce::File& file, juce::String trackPath, int startTick, juce::String* newBlockPath)
    {
        if (model.get().findTrack (trackPath) == nullptr)
            return juce::Result::fail ("No track selected");

        for (auto& b : model.get().findTrack (trackPath)->blocks)
            if (b.start <= startTick && b.end > startTick)
                startTick = b.end;
        startTick = juce::jmax (0, startTick);

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
            perform (new LoadedNodeAction (session, imported, file));

            // Blocks exported from the Flex GUI don't carry their block envelope (it lives
            // in the track), so give the imported block one.
            int length = newBlockSeconds * model.get().player.rate;
            model.rebuild();
            addBlockEnvelope (trackPath, imported, startTick, startTick + length);
            model.rebuild();

            std::map<juce::String, juce::Range<int>> ranges { { imported, { startTick, startTick + length } } };
            if (auto* track = model.get().findTrack (trackPath))
                rescheduleBlocks (*track, imported, true, ranges);
            ranges.erase (imported);
            for (auto& [path, range] : ranges)
                repositionBlock (path, range.getStart(), range.getEnd());

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

    bool ProjectController::isUpstreamInAudioLinks (juce::String modulePath, juce::String candidate) const
    {
        // IntegraModel.isTargetUpstreamInAudioLinks: follow audio connections backwards
        // from modulePath, through every container, looking for candidate
        std::vector<const model::Connection*> audioLinks;
        auto collect = [&] (const model::Container& c)
        {
            for (auto& conn : c.connections)
                if (conn.sourcePath.isNotEmpty() && conn.targetPath.isNotEmpty()
                     && EndpointInfo::find (session, conn.sourcePath).isAudioStream
                     && EndpointInfo::find (session, conn.targetPath).isAudioStream)
                    audioLinks.push_back (&conn);
        };
        auto& p = model.get();
        collect (p);
        for (auto& t : p.tracks)
        {
            collect (t);
            for (auto& b : t.blocks)
                collect (b);
        }

        juce::StringArray visited, toVisit { modulePath };
        while (! toVisit.isEmpty())
        {
            const auto node = toVisit[0];
            toVisit.remove (0);
            visited.add (node);
            for (auto* link : audioLinks)
            {
                if (link->targetNode() != node)
                    continue;
                if (link->sourceNode() == candidate)
                    return true;
                if (! visited.contains (link->sourceNode()))
                    toVisit.addIfNotAlreadyThere (link->sourceNode());
            }
        }
        return false;
    }

    juce::String ProjectController::spliceModule (juce::String blockPath, const GUID& moduleGuid, juce::Point<float> gridPosition, juce::String connectionPath)
    {
        juce::String source, target;
        if (auto* block = model.get().findBlock (blockPath))
            for (auto& c : block->connections)
                if (c.path == connectionPath)
                    source = c.sourcePath, target = c.targetPath;
        auto* entry = session.getCatalogue().findByGuid (moduleGuid);
        if (entry == nullptr)
            return {};

        // the module's first audio input and output
        juce::String firstInput, firstOutput;
        if (entry->definition != nullptr)
            for (auto* ep : entry->definition->get_endpoint_definitions())
            {
                auto info = EndpointInfo::fromDefinition (*ep);
                if (! info.isAudioStream)
                    continue;
                if (info.isAudioInput && firstInput.isEmpty())
                    firstInput = info.name;
                if (! info.isAudioInput && firstOutput.isEmpty())
                    firstOutput = info.name;
            }

        return transaction ("Add " + entry->label, [&]
        {
            auto module = addModule (blockPath, moduleGuid, gridPosition);
            if (module.isEmpty() || source.isEmpty() || target.isEmpty())
                return module;
            if (firstInput.isNotEmpty() || firstOutput.isNotEmpty())
                remove (connectionPath);
            if (firstInput.isNotEmpty())
                connect (blockPath, source, module + "." + firstInput);
            if (firstOutput.isNotEmpty())
                connect (blockPath, module + "." + firstOutput, target);
            model.rebuild();
            model.selectedModule = module;
            return module;
        });
    }

    juce::String ProjectController::connect (juce::String containerPath, juce::String sourceEndpoint, juce::String targetEndpoint)
    {
        // IntegraModel.canSetAudioLink: no links from a module to itself, and none that
        // would make a feedback loop (the target already feeds the source)
        if (parentPath (sourceEndpoint) == parentPath (targetEndpoint)
             || isUpstreamInAudioLinks (parentPath (sourceEndpoint), parentPath (targetEndpoint)))
            return {};

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
        for (auto c : juce::String (SceneUserData::keybindings))
        {
            bool used = false;
            for (auto& s : p.scenes)
                used |= s.userData.keybinding.equalsIgnoreCase (juce::String::charToString (c));
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
        // SceneUserData.KEYBINDINGS: one of 0-9 / A-Z, stored upper-case
        key = key.trim().toUpperCase().substring (0, 1);
        if (key.isNotEmpty() && ! juce::String (SceneUserData::keybindings).contains (key))
            return;

        // a key can only select one scene
        transaction ("Set Scene Key", [&]
        {
            for (auto& s : model.get().scenes)
            {
                if (s.path != scenePath && key.isNotEmpty() && s.userData.keybinding.equalsIgnoreCase (key))
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

    juce::Rectangle<float> ProjectController::findNewLiveViewControlPosition (const std::vector<BlockUserData::LiveViewControl>& controls, juce::Point<float> size)
    {
        constexpr float margin = 20.0f;
        juce::Rectangle<float> r (margin, margin, size.x, size.y);
        for (;;)
        {
            bool overlaps = false;
            for (auto& c : controls)
                overlaps |= r.expanded (margin).intersects (c.bounds);
            if (! overlaps)
                return r;
            r.translate (margin, 0.0f);
        }
    }

    void ProjectController::setModuleLiveViewControls (juce::String modulePath, const std::vector<std::pair<juce::String, juce::Rectangle<float>>>& widgets, bool shown)
    {
        const auto moduleName = lastElement (modulePath);
        auto sorted = widgets;
        std::stable_sort (sorted.begin(), sorted.end(), [] (auto& a, auto& b) { return a.second.getCentreX() < b.second.getCentreX(); });

        updateBlockUserData (parentPath (modulePath), [&] (BlockUserData& d)
        {
            auto& controls = d.liveViewControls;
            if (! shown)
            {
                controls.erase (std::remove_if (controls.begin(), controls.end(), [&] (auto& c) { return c.moduleName == moduleName; }), controls.end());
                return;
            }
            for (auto& widget : sorted)     // (no structured binding: Apple clang can't capture one)
            {
                const auto& label = widget.first;
                const auto& design = widget.second;
                const bool present = std::any_of (controls.begin(), controls.end(),
                                                  [&] (auto& c) { return c.moduleName == moduleName && c.widgetLabel == label; });
                if (! present)
                    controls.push_back ({ moduleName, label, findNewLiveViewControlPosition (controls, { design.getWidth(), design.getHeight() }) });
            }
        });
    }

    juce::Result ProjectController::importModule (const juce::File& file, juce::String blockPath, juce::Point<float> gridPosition, juce::String* newModulePath)
    {
        if (model.get().findBlock (blockPath) == nullptr)
            return juce::Result::fail ("No block");
        auto before = session.getChildNames (blockPath);

        return transaction ("Import Module", [&]
        {
            if (session.loadFile (file, blockPath) != CError::SUCCESS)
                return juce::Result::fail ("Cannot import \"" + file.getFileName() + "\":\n\n" + session.getLastError());

            juce::String imported;
            for (auto& name : session.getChildNames (blockPath))
                if (! before.contains (name))
                    imported = blockPath + "." + name;

            // a module, not a block or track
            if (imported.isEmpty() || session.getModuleName (imported) == "Container")
            {
                if (imported.isNotEmpty())
                    session.deleteNode (imported);
                return juce::Result::fail ("This file cannot be imported as a module");
            }

            perform (new LoadedNodeAction (session, imported, file));
            model.rebuild();
            const model::Module* m = nullptr;
            if (auto* block = model.get().findBlock (blockPath))
                m = block->findModule (lastElement (imported));
            std::map<juce::String, juce::Rectangle<float>> positions;
            positions[lastElement (imported)] = { juce::jmax (0.0f, gridPosition.x), juce::jmax (0.0f, gridPosition.y),
                                                  moduleWidth, moduleHeight (m != nullptr ? m->entry : nullptr) };
            setModulePositions (blockPath, positions);
            model.rebuild();
            model.selectedModule = imported;
            if (newModulePath != nullptr)
                *newModulePath = imported;
            return juce::Result::ok();
        });
    }
}
