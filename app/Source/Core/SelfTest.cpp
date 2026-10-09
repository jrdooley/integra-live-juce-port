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
