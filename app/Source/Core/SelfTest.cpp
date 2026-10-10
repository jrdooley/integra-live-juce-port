#include "SelfTest.h"

namespace integra
{
    namespace
    {
        struct Checker
        {
            juce::String& log;
            int failures = 0;

            void check (bool ok, const juce::String& what)
            {
                juce::String line = juce::String (ok ? "ok   " : "FAIL ") + what + "\n";
                log << line;
                std::cout << line << std::flush;
                if (! ok)
                    ++failures;
            }
        };

        /** Pushes `blocks` callbacks of an awkward size (not a multiple of 64) through
            AudioHost with a sine on both inputs; returns RMS of output channel 0. */
        float pullAudio (AudioHost& host, int callbacks, bool feedSine)
        {
            const int numSamples = 100, channels = 2;
            juce::AudioBuffer<float> in (channels, numSamples), out (channels, numSamples);
            double phase = 0, sum = 0;
            int counted = 0;

            for (int cb = 0; cb < callbacks; ++cb)
            {
                for (int i = 0; i < numSamples; ++i)
                {
                    const float s = feedSine ? 0.5f * (float) std::sin (phase) : 0.0f;
                    phase += juce::MathConstants<double>::twoPi * 440.0 / 44100.0;
                    for (int c = 0; c < channels; ++c)
                        in.setSample (c, i, s);
                }

                host.audioDeviceIOCallbackWithContext (in.getArrayOfReadPointers(), channels,
                                                       out.getArrayOfWritePointers(), channels,
                                                       numSamples, {});

                if (cb >= callbacks / 2)
                {
                    for (int i = 0; i < numSamples; ++i)
                        sum += out.getSample (0, i) * out.getSample (0, i);
                    counted += numSamples;
                }
            }

            return (float) std::sqrt (sum / juce::jmax (1, counted));
        }
    }

    juce::File makeAlternativeModuleVersion (const juce::File& moduleFile, const juce::String& endpointToRemove, const juce::File& destination)
    {
        auto work = juce::File::createTempFile ("module_version");
        work.createDirectory();
        juce::ZipFile (moduleFile).uncompressTo (work);

        auto iidFile = work.getChildFile ("integra_module_data/interface_definition.iid");
        auto xml = juce::parseXML (iidFile);
        if (xml == nullptr)
            return {};

        // a new version of the same module: new module GUID, same origin GUID, newer date
        xml->setAttribute ("moduleGuid", juce::Uuid().toDashedString());
        if (auto* info = xml->getChildByName ("InterfaceInfo"))
            if (auto* modified = info->getChildByName ("ModifiedDate"))
                modified->deleteAllTextElements(), modified->addTextElement ("2030-01-01 00:00:00.000000");

        // drop an endpoint and the widgets that show it
        std::function<void (juce::XmlElement&)> prune = [&] (juce::XmlElement& parent)
        {
            for (auto* child = parent.getFirstChildElement(); child != nullptr;)
            {
                auto* next = child->getNextElement();
                const bool isEndpoint = child->hasTagName ("Endpoint") && child->getChildElementAllSubText ("Name", {}) == endpointToRemove;
                const bool isWidget = child->hasTagName ("Widget") && child->getChildElementAllSubText ("Label", {}) == endpointToRemove;
                if (isEndpoint || isWidget)
                    parent.removeChildElement (child, true);
                else
                    prune (*child);
                child = next;
            }
        };
        prune (*xml);
        xml->writeTo (iidFile);

        juce::ZipFile::Builder builder;
        for (auto& f : work.findChildFiles (juce::File::findFiles, true))
            builder.addFile (f, 9, f.getRelativePathFrom (work).replaceCharacter ('\\', '/'));
        destination.deleteFile();
        juce::FileOutputStream out (destination);
        builder.writeToStream (out, nullptr);
        out.flush();
        work.deleteRecursively();
        return destination;
    }

    int runSelfTest (IntegraSession& session, ProjectModel& model, ProjectController& controller, AudioHost& host, juce::String& log)
    {
        Checker t { log };
        auto& p = model.get();

        // --- new project defaults ------------------------------------------------
        t.check (controller.newProject().wasOk(), "new project");
        t.check (p.valid && p.path == "Untitled", "project container 'Untitled'");
        t.check (p.player.path == "Untitled.Player1" && p.player.rate == 40, "Player1 at 40 ticks/s");
        t.check (p.midiMonitorPath == "Untitled.MidiMonitor", "MidiMonitor");
        t.check (p.audioSettingsPath.isNotEmpty() && p.midiSettingsPath.isNotEmpty(), "AudioSettings and MidiSettings nodes");
        t.check (p.tracks.size() == 1 && p.tracks[0].blocks.size() == 1, "one track with one block");

        static const model::Block emptyBlock;
        auto blk = [&p] (const juce::String& path) -> const model::Block& {
            auto* b = p.findBlock (path);
            return b != nullptr ? *b : emptyBlock;
        };

        if (p.tracks.empty() || p.tracks[0].blocks.empty())
            return t.failures + 1;

        auto track = p.tracks.at (0).path;
        auto block = p.tracks.at (0).blocks.at (0).path;
        {
            auto& b = p.tracks[0].blocks[0];
            t.check (b.start == 0 && b.end == 800, "block spans 0..800 ticks (20 s), from its block envelope");
            t.check (b.modules.size() == 2 && b.findModule ("AudioIn1") && b.findModule ("StereoAudioOut1"), "block has AudioIn1 + StereoAudioOut1");
            t.check (b.findModule ("AudioIn1") && b.findModule ("AudioIn1")->position.toDouble() == juce::Rectangle<double> (0.875, 5.25, 7.0, 3.0),
                     "module position stored in block userData (Flex grid units)");
            t.check (p.tracks[0].userData.colour == 0x0F8AEB, "first track gets the first Flex palette colour");
        }

        // --- audio through the JUCE re-blocking path -----------------------------
        auto in = block + ".AudioIn1.out", outL = block + ".StereoAudioOut1.in1";
        auto connection = controller.connect (block, in, outL);
        t.check (connection.isNotEmpty(), "connect AudioIn1.out -> StereoAudioOut1.in1");
        t.check (session.getValue (connection + ".sourcePath").toString() == "AudioIn1.out", "connection paths stored relative to the block");

        session.setValue (p.player.path + ".tick", 10);   // inside the block -> active
        host.configure (2, 2, 44100);
        juce::Thread::sleep (100);
        float rms = pullAudio (host, 600, true);
        t.check (rms > 0.1f, "sine in -> JUCE callback (100-sample buffers) -> libpd -> out, rms " + juce::String (rms, 3));

        // --- block activity follows the playhead via the block envelope ------------
        session.setValue (p.player.path + ".tick", 900);  // after the block
        juce::Thread::sleep (100);
        t.check ((int) session.getValue (block + ".active") == 0, "block inactive after its end (block envelope)");
        session.setValue (p.player.path + ".tick", 10);
        juce::Thread::sleep (100);
        t.check ((int) session.getValue (block + ".active") == 1, "block active inside its extent");

        // --- undo / redo ----------------------------------------------------------
        auto& undo = controller.getUndoManager();
        auto delay = controller.addModule (block, session.getCatalogue().findByName ("TapDelay")->guid, { 14, 5 });
        t.check (delay == block + ".TapDelay1", "add TapDelay1");
        controller.setValue (delay + ".delayTime", 1.5);
        t.check (juce::approximatelyEqual ((double) session.getValue (delay + ".delayTime"), 1.5), "set delayTime");

        undo.undo();
        t.check (! juce::approximatelyEqual ((double) session.getValue (delay + ".delayTime"), 1.5), "undo set value");
        undo.undo();
        model.rebuild();
        t.check (! session.nodeExists (delay) && blk (block).userData.modulePositions.count ("TapDelay1") == 0, "undo add module (node and position removed)");
        undo.redo();
        model.rebuild();
        t.check (session.nodeExists (delay) && blk (block).findModule ("TapDelay1") != nullptr, "redo add module");

        // delete with dependants, then undo restores the subtree and its connections
        auto c2 = controller.connect (block, in, delay + ".in1");
        auto c3 = controller.connect (block, delay + ".out1", block + ".StereoAudioOut1.in2");

        // no self-links and no feedback loops (IntegraModel.canSetAudioLink)
        {
            model.rebuild();
            auto delay2 = controller.addModule (block, session.getCatalogue().findByName ("TapDelay")->guid, { 24, 5 });
            model.rebuild();
            auto forward = controller.connect (block, delay + ".out1", delay2 + ".in1");
            t.check (forward.isNotEmpty(), "connect TapDelay1 -> TapDelay2");
            t.check (controller.connect (block, delay2 + ".out1", delay + ".in1").isEmpty(), "a connection that would make a feedback loop is refused");
            t.check (controller.connect (block, delay + ".out1", delay + ".in1").isEmpty(), "a module can't be connected to itself");
            controller.remove (delay2);
            model.rebuild();
        }
        controller.setValue (delay + ".feedback", 0.25);
        controller.remove (delay);
        model.rebuild();
        t.check (! session.nodeExists (delay) && ! session.nodeExists (c2) && ! session.nodeExists (c3), "delete module removes its connections");
        undo.undo();
        model.rebuild();
        t.check (session.nodeExists (delay) && session.nodeExists (c2) && session.nodeExists (c3), "undo delete restores module and connections");
        t.check (std::abs ((double) session.getValue (delay + ".feedback") - 0.25) < 1.0e-6, "undo delete restores values");

        // --- rename keeps routing and positions ------------------------------------
        t.check (controller.rename (delay, "Echo").wasOk(), "rename TapDelay1 -> Echo");
        model.rebuild();
        t.check (session.getValue (c2 + ".targetPath").toString() == "Echo.in1", "rename re-targets connections");
        t.check (blk (block).userData.modulePositions.count ("Echo") == 1, "rename moves userData position");
        delay = block + ".Echo";

        // --- routing, envelope, scene, script, MIDI ---------------------------------
        auto scaler = controller.addRouting (p.path, block + ".StereoAudioOut1.inLevel", delay + ".mix");
        model.rebuild();
        t.check (scaler.isNotEmpty() && p.scalers.size() == 1 && p.scalers[0].sourcePath == block + ".StereoAudioOut1.inLevel"
                    && p.scalers[0].targetPath == delay + ".mix", "routing: Connection -> Scaler -> Connection");

        auto envelope = controller.addEnvelope (block, delay + ".delayTime");
        model.rebuild();
        controller.addControlPoint (envelope, 400, 2.0f);
        model.rebuild();
        auto* env = blk (block).findEnvelopeForTarget (delay + ".delayTime");
        t.check (env != nullptr && env->points.size() == 2 && env->playerConnection.isNotEmpty(), "envelope with player connection and 2 points");
        session.setValue (p.player.path + ".tick", 200);
        juce::Thread::sleep (50);
        auto midValue = (double) session.getValue (delay + ".delayTime");
        t.check (midValue > 0.1 && midValue < 2.0, "envelope interpolates target while playhead moves (" + juce::String (midValue, 3) + ")");

        // curvature of the first point bends the segment after it (t^(2^-c))
        if (env != nullptr && ! env->points.empty())
        {
            const auto firstPoint = env->points.front().path;
            controller.setControlPointCurvature (firstPoint, 1.0f);
            session.setValue (p.player.path + ".tick", 0);
            session.setValue (p.player.path + ".tick", 200);
            juce::Thread::sleep (50);
            const auto curved = (double) session.getValue (delay + ".delayTime");
            const auto startValue = (double) env->points.front().value;
            const double expected = startValue + std::sqrt (0.5) * (2.0 - startValue);
            t.check (std::abs (curved - expected) < 0.02, "curvature 1 gives sqrt interpolation (" + juce::String (curved, 3) + ", expected " + juce::String (expected, 3) + ")");
            controller.setControlPointCurvature (firstPoint, 0.0f);
        }

        auto scene = controller.addScene (0, 400);
        model.rebuild();
        t.check (p.scenes.size() == 1 && p.scenes[0].userData.keybinding == "1", "scene with key binding '1'");
        controller.activateScene (scene);
        t.check (session.getValue (p.player.path + ".scene").toString() == lastElement (scene), "activate scene sets Player.scene");

        auto script = controller.addScript (block);
        controller.setScriptText (script, "Echo.feedback = 0.5");
        controller.runScript (script);
        juce::Thread::sleep (50);
        t.check (std::abs ((double) session.getValue (delay + ".feedback") - 0.5) < 1.0e-6, "Lua script sets a module value");

        auto midi = controller.addMidiControl (block, delay + ".outLevel", false);
        model.rebuild();
        t.check (midi.isNotEmpty() && blk (block).midiControlInputs.size() == 1
                    && blk (block).midiControlInputs[0].outputConnection.isNotEmpty(), "MIDI control input routed through a scaler");

        // what drives an endpoint (locks its controls)
        {
            auto kinds = [&] (const juce::String& endpoint)
            {
                juce::String k;
                for (auto& d : p.driversOf (endpoint))
                    k << (d.kind == model::Driver::Kind::envelope ? "env" : d.kind == model::Driver::Kind::midi ? "midi" : "routing") << ";";
                return k;
            };
            t.check (kinds (delay + ".mix") == "routing;", "a routed endpoint is driven by the routing (" + kinds (delay + ".mix") + ")");
            t.check (kinds (delay + ".delayTime") == "env;", "an enveloped endpoint is driven by the envelope");
            t.check (kinds (delay + ".outLevel") == "midi;", "a MIDI-controlled endpoint is driven by MIDI");
            t.check (kinds (delay + ".feedback").isEmpty(), "a free endpoint has no drivers");
        }

        // --- reposition and move block --------------------------------------------
        controller.repositionBlock (block, 100, 500);
        model.rebuild();
        t.check (blk (block).start == 100 && blk (block).end == 500, "reposition block 100..500");
        auto track2 = controller.addTrack();
        model.rebuild();
        block = controller.moveBlockToTrack (block, track2);
        model.rebuild();
        t.check (p.findBlock (block) != nullptr && p.findTrackOfBlock (block) != nullptr && p.findTrackOfBlock (block)->path == track2 && blk (block).start == 100,
                 "move block to second track keeps its extent");
        session.setValue (p.player.path + ".tick", 50);
        juce::Thread::sleep (50);
        t.check ((int) session.getValue (block + ".active") == 0, "moved block still driven by its envelope (inactive at 50)");
        session.setValue (p.player.path + ".tick", 200);
        juce::Thread::sleep (50);
        t.check ((int) session.getValue (block + ".active") == 1, "moved block active at 200");

        // --- save, reopen ------------------------------------------------------------
        auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("integra-selftest.integra");
        t.check (controller.saveProject (file).wasOk() && file.existsAsFile(), "save project");
        const auto oldProjectPath = p.path;
        t.check (controller.openProject (file).wasOk(), "reopen project");

        // libIntegra names the loaded project after the file, as the Flex GUI did
        t.check (p.path == "integra_selftest",
                 "reopened project named after the file (" + p.path + ")");
        block = p.path + block.fromFirstOccurrenceOf (oldProjectPath, false, false);
        t.check (p.tracks.size() == 2 && p.findBlock (block) != nullptr && blk (block).findModule ("Echo") != nullptr
                    && p.scenes.size() == 1 && p.scalers.size() == 1, "reopened project has same structure");
        t.check (blk (block).userData.modulePositions.count ("Echo") == 1, "reopened userData positions");
        file.deleteFile();

        // --- module versions (Module Manager: SwitchModuleVersion, UpgradeModules) ---
        {
            auto& catalogue = session.getCatalogue();
            const auto* shipped = catalogue.findByName ("TapDelay");
            const GUID shippedGuid = shipped->guid, origin = shipped->originGuid;
            auto altFile = makeAlternativeModuleVersion (session.getLocations().systemModules.getChildFile ("TapDelay.module"), "frequency",
                                                         juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("TapDelayAlternative.module"));
            auto installed = controller.installModule (altFile);
            t.check (installed.wasOk(), "install a 3rd party version of TapDelay " + installed.getErrorMessage());
            altFile.deleteFile();

            auto versions = catalogue.getVersions (origin);
            t.check (versions.size() == 2 && guidsEqual (versions[0]->guid, shippedGuid)
                        && versions[1]->source == IInterfaceDefinition::MODULE_3RD_PARTY,
                     "both versions are listed, the shipped one first");
            const GUID altGuid = versions.size() == 2 ? versions[1]->guid : shippedGuid;

            const auto echo = block + ".Echo";
            auto guidOf = [&] (const juce::String& path)
            {
                auto* b = p.findBlock (parentPath (path));
                auto* m = b != nullptr ? b->findModule (lastElement (path)) : nullptr;
                return m != nullptr && m->entry != nullptr ? m->entry->guid : GUID {};
            };
            auto connectionsTo = [&] (const juce::String& endpoint)
            {
                int n = 0;
                for (auto* c : std::initializer_list<const model::Container*> { &p, &blk (block) })
                    for (auto& conn : c->connections)
                        n += conn.targetPath == endpoint ? 1 : 0;
                return n;
            };

            controller.setValue (echo + ".feedback", 0.4);
            controller.setValue (echo + ".frequency", 500.0);
            controller.addRouting (p.path, block + ".StereoAudioOut1.inLevel", echo + ".frequency");
            model.rebuild();
            const int audioIn = connectionsTo (echo + ".in1");
            t.check (audioIn == 1 && p.driversOf (echo + ".frequency").size() == 1, "TapDelay has an audio input and a routing to frequency");

            t.check (controller.switchModuleVersion (echo, altGuid), "switch TapDelay to the 3rd party version");
            model.rebuild();
            t.check (guidsEqual (guidOf (echo), altGuid), "the module now uses the 3rd party version");
            t.check (std::abs ((double) session.getValue (echo + ".feedback") - 0.4) < 1.0e-6, "values carry over to the new version");
            t.check (connectionsTo (echo + ".in1") == 1 && p.driversOf (echo + ".mix").size() == 1, "still-valid connections and routings are kept");
            t.check (p.driversOf (echo + ".frequency").empty() && ! session.nodeExists (echo + ".frequency"),
                     "the routing to the removed endpoint is removed");
            t.check (controller.hasUpgradableModules(), "the project now has an upgradable module");

            controller.getUndoManager().undo();
            model.rebuild();
            t.check (guidsEqual (guidOf (echo), shippedGuid) && p.driversOf (echo + ".frequency").size() == 1
                        && std::abs ((double) session.getValue (echo + ".frequency") - 500.0) < 1.0e-3,
                     "undo restores the shipped version, its value and the routing");
            controller.getUndoManager().redo();
            model.rebuild();
            t.check (guidsEqual (guidOf (echo), altGuid), "redo switches again");

            auto upgrade = controller.upgradeAll();
            model.rebuild();
            t.check (upgrade.upgradedModules.size() == 1 && guidsEqual (guidOf (echo), shippedGuid) && ! controller.hasUpgradableModules(),
                     "Upgrade All switches it back to the shipped version");
            t.check (upgrade.backup.existsAsFile(), "a backup is saved before upgrading (" + upgrade.backup.getFileName() + ")");
            upgrade.backup.deleteFile();
            t.check (controller.describeVersionChange (altGuid, shippedGuid).contains ("Added: frequency"),
                     "the version report lists the endpoint difference");

            auto removed = controller.uninstallModule (altGuid, true);
            t.check (removed.wasOk() && catalogue.getVersions (origin).size() == 1, "uninstall the 3rd party version " + removed.getErrorMessage());
        }

        // --- open the shipped demo blocks into a project (Flex-saved files) --------
        controller.newProject();
        auto firstTrack = p.tracks.at (0).path;
        for (auto& f : session.getLocations().blockLibrary.findChildFiles (juce::File::findFiles, false, "*.integra"))
        {
            juce::String imported;
            auto result = controller.importBlock (f, firstTrack, 1000, &imported);
            model.rebuild();
            auto* b = p.findBlock (imported);
            t.check (result.wasOk() && b != nullptr && ! b->modules.empty() && ! b->userData.modulePositions.empty(),
                     "import demo block " + f.getFileName() + " (" + juce::String ((int) (b ? b->modules.size() : 0)) + " modules)");
        }

        controller.newProject();
        return t.failures;
    }
}
