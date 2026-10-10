#include "StressTest.h"
#include "EndpointInfo.h"

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
                if (ok)
                    return;     // only failures are logged: there are thousands of checks
                juce::String line = "FAIL " + what + "\n";
                log << line;
                std::cout << line << std::flush;
                ++failures;
            }

            void note (const juce::String& what)
            {
                log << what << "\n";
                std::cout << what << std::endl;
            }
        };

        /** Pulls `callbacks` blocks of 100 samples (not a multiple of 64) through AudioHost;
            returns false if the engine produced a NaN or infinity (AudioHost silences those
            blocks, so its count is checked as well as the output). */
        bool pullAudio (AudioHost& host, int callbacks)
        {
            const int silencedBefore = host.getNonFiniteBlockCount();
            const int numSamples = 100, channels = 2;
            juce::AudioBuffer<float> in (channels, numSamples), out (channels, numSamples);
            static double phase = 0;
            bool finite = true;

            for (int cb = 0; cb < callbacks; ++cb)
            {
                for (int i = 0; i < numSamples; ++i)
                {
                    const float s = 0.5f * (float) std::sin (phase);
                    phase = std::fmod (phase + juce::MathConstants<double>::twoPi * 440.0 / 44100.0, juce::MathConstants<double>::twoPi);
                    for (int c = 0; c < channels; ++c)
                        in.setSample (c, i, s);
                }

                host.audioDeviceIOCallbackWithContext (in.getArrayOfReadPointers(), channels,
                                                       out.getArrayOfWritePointers(), channels,
                                                       numSamples, {});

                for (int c = 0; c < channels; ++c)
                    for (int i = 0; i < numSamples; ++i)
                        if (! std::isfinite (out.getSample (c, i)))
                            finite = false;
            }
            return finite && host.getNonFiniteBlockCount() == silencedBefore;
        }

        /** Every node path in the snapshot exists in libIntegra. */
        bool modelMatchesServer (IntegraSession& session, const model::Project& p)
        {
            if (! p.valid)
                return false;
            for (auto& track : p.tracks)
            {
                if (! session.nodeExists (track.path))
                    return false;
                for (auto& block : track.blocks)
                {
                    if (! session.nodeExists (block.path))
                        return false;
                    for (auto& m : block.modules)
                        if (! session.nodeExists (m.path))
                            return false;
                    for (auto& c : block.connections)
                        if (! session.nodeExists (c.path))
                            return false;
                }
            }
            for (auto& s : p.scenes)
                if (! session.nodeExists (s.path))
                    return false;
            return true;
        }

        /** The project's structure as text: every track, block (with its span), module,
            connection, envelope and scene. Equal signatures mean undo/redo round-tripped. */
        juce::String signature (const model::Project& p)
        {
            juce::String sig;
            for (auto& track : p.tracks)
            {
                sig << track.path << "\n";
                for (auto& block : track.blocks)
                {
                    sig << " " << block.path << " " << block.start << "-" << block.end << "\n";
                    for (auto& m : block.modules)        sig << "  " << m.path << "\n";
                    for (auto& c : block.connections)    sig << "  " << c.path << " " << c.sourcePath << ">" << c.targetPath << "\n";
                    for (auto& e : block.envelopes)      sig << "  " << e.path << " " << e.targetPath << "\n";
                }
            }
            for (auto& scene : p.scenes)
                sig << scene.path << " " << scene.start << "+" << scene.length << "\n";
            return sig;
        }

        juce::Array<juce::var> valuesToTry (const EndpointInfo& info)
        {
            juce::Array<juce::var> values;
            switch (info.kind)
            {
                case EndpointInfo::Kind::integer:
                case EndpointInfo::Kind::real:
                {
                    const bool isInt = info.kind == EndpointInfo::Kind::integer;
                    auto num = [isInt] (double v) { return isInt ? juce::var ((int) juce::jlimit (-1.0e9, 1.0e9, v)) : juce::var (v); };
                    if (info.hasRange)
                    {
                        const double span = info.maximum - info.minimum;
                        values.add (num (info.minimum), num (info.maximum), num (info.minimum + span * 0.5),
                                    num (info.minimum - span - 1), num (info.maximum + span * 10 + 1));
                    }
                    values.add (num (0), num (-1), num (1.0e6));
                    for (auto& state : info.allowedStates)
                        values.add (isInt ? juce::var (state.getIntValue()) : juce::var (state.getDoubleValue()));
                    break;
                }
                case EndpointInfo::Kind::text:
                    values.add (juce::var (juce::String()), juce::var (juce::String::repeatedString ("x", 5000)),
                                juce::var ("{}[];,\\\"' \t\n%s%d"), juce::var ("/nonexistent/file.wav"), juce::var (juce::String::fromUTF8 ("\xc3\xa9\xe2\x82\xac")));
                    for (auto& state : info.allowedStates)
                        values.add (juce::var (state));
                    break;
                case EndpointInfo::Kind::bang:
                case EndpointInfo::Kind::missing:
                    break;
            }
            if (! info.defaultValue.isVoid())
                values.add (info.defaultValue);
            return values;
        }

        juce::Array<juce::File> demoProjects()
        {
            auto folder = IntegraSession::Locations::findDefault().blockLibrary;
            auto files = folder.findChildFiles (juce::File::findFiles, true, "*.integra");
            files.sort();
            return files;
        }
    }

    int runStressTest (IntegraSession& session, ProjectModel& model, ProjectController& controller, AudioHost& host,
                       double playbackSeconds, const juce::Array<juce::File>& extraProjects, juce::String& log)
    {
        Checker t { log };
        auto& p = model.get();
        auto& undo = controller.getUndoManager();
        const auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("integra_stress_" + juce::String (juce::Time::currentTimeMillis()));
        temp.createDirectory();
        host.configure (2, 2, 44100);     // the stress test is the audio device

        // --- every project: open, play, save, reopen, import as a block ------------
        auto projects = demoProjects();
        projects.addArray (extraProjects);
        t.check (! projects.isEmpty(), "found demo blocks to load");

        for (auto& file : projects)
        {
            const auto name = file.getFileName();
            t.note ("project " + name);
            auto r = controller.openProject (file);
            t.check (r.wasOk(), "open " + name + ": " + r.getErrorMessage());
            if (r.failed())
                continue;
            t.check (modelMatchesServer (session, p), name + ": model matches server after open");

            controller.play (true);
            t.check (pullAudio (host, 300), name + ": finite audio while playing");
            for (auto& s : p.scenes)
                controller.activateScene (s.path), pullAudio (host, 20);
            controller.setPlayPosition (0);
            controller.play (false);

            auto saved = temp.getChildFile ("resaved_" + name);
            t.check (controller.saveProject (saved).wasOk(), name + ": save");
            const auto tracks = p.tracks.size();
            const auto scenes = p.scenes.size();
            t.check (controller.openProject (saved).wasOk(), name + ": reopen saved copy");
            t.check (p.tracks.size() == tracks && p.scenes.size() == scenes, name + ": same tracks and scenes after save and reopen");
            t.check (modelMatchesServer (session, p), name + ": model matches server after reopen");

            controller.newProject();
            juce::String imported;
            r = controller.importBlock (file, p.tracks.empty() ? controller.addTrack() : p.tracks.front().path, 1000, &imported);
            model.rebuild();
            if (r.wasOk())
            {
                t.check (p.findBlock (imported) != nullptr, name + ": imported as a block");
                controller.play (true);
                t.check (pullAudio (host, 100), name + ": finite audio from the imported block");
                controller.play (false);
            }
            // a multi-track project can't be imported as a block; that's a clean refusal, not a failure
        }

        // --- every module, every control endpoint, extreme values -------------------
        t.check (controller.newProject().wasOk(), "new project for module sweep");
        model.rebuild();
        const auto block = p.tracks.front().blocks.front().path;
        int modulesTried = 0, valuesTried = 0;

        for (auto* entry : session.getCatalogue().getUserModules())
        {
            if (! entry->hasImplementation || entry->definition == nullptr)
                continue;
            auto module = controller.addModule (block, entry->guid, { 20.0f, 2.0f });
            t.check (module.isNotEmpty() && session.nodeExists (module), "add module " + entry->name);
            if (module.isEmpty())
                continue;
            ++modulesTried;

            // wire it between the block's input and output so audio actually flows through
            model.rebuild();
            juce::String audioIn, audioOut;
            for (auto* ep : entry->definition->get_endpoint_definitions())
            {
                auto info = EndpointInfo::fromDefinition (*ep);
                if (info.isAudioStream && info.isAudioInput && audioIn.isEmpty())   audioIn = module + "." + info.name;
                if (info.isAudioStream && ! info.isAudioInput && audioOut.isEmpty()) audioOut = module + "." + info.name;
            }
            if (audioIn.isNotEmpty())  controller.connect (block, block + ".AudioIn1.out", audioIn);
            if (audioOut.isNotEmpty()) controller.connect (block, audioOut, block + ".StereoAudioOut1.in1");

            for (auto* ep : entry->definition->get_endpoint_definitions())
            {
                auto info = EndpointInfo::fromDefinition (*ep);
                if (info.isAudioStream)
                    continue;
                const auto path = module + "." + info.name;
                if (info.kind == EndpointInfo::Kind::bang)
                {
                    controller.bang (path);
                    ++valuesTried;
                    continue;
                }
                for (auto& v : valuesToTry (info))
                {
                    controller.setValue (path, v, false);
                    ++valuesTried;
                    if (! pullAudio (host, 2))
                        t.check (false, entry->name + "." + info.name + " = " + v.toString().substring (0, 40) + ": non-finite audio");
                }
                if (! info.defaultValue.isVoid())
                    controller.setValue (path, info.defaultValue, false);
            }
            t.check (pullAudio (host, 20), entry->name + ": finite audio after the sweep");

            controller.remove (module);
            model.rebuild();
            t.check (! session.nodeExists (module), "remove module " + entry->name);
            t.check (modelMatchesServer (session, p), entry->name + ": model matches server after removal");
        }
        t.note ("swept " + juce::String (modulesTried) + " modules, " + juce::String (valuesTried) + " values");
        t.check (modulesTried > 20, "most modules swept");

        // --- random edits mixed with undo and redo ----------------------------------
        t.check (controller.newProject().wasOk(), "new project for edit fuzz");
        juce::Random random (20261010);
        auto userModules = session.getCatalogue().getUserModules();
        auto pick = [&random] (int size) { return size > 0 ? random.nextInt (size) : 0; };
        const int steps = 600;

        for (int step = 0; step < steps; ++step)
        {
            model.rebuild();
            std::vector<const model::Block*> blocks;
            for (auto& tr : p.tracks)
                for (auto& b : tr.blocks)
                    blocks.push_back (&b);
            const auto* b = blocks.empty() ? nullptr : blocks[(size_t) pick ((int) blocks.size())];

            juce::String action;
            switch (random.nextInt (14))
            {
                case 0: action = "add track"; controller.addTrack(); break;
                case 1:
                    if (! p.tracks.empty())
                    {
                        action = "add block";
                        auto& tr = p.tracks[(size_t) pick ((int) p.tracks.size())];
                        const int start = random.nextInt (4000);
                        controller.addBlock (tr.path, start, start + 40 + random.nextInt (800), random.nextBool());
                    }
                    break;
                case 2: case 3:
                    if (b != nullptr && ! userModules.empty())
                    {
                        action = "add module";
                        controller.addModule (b->path, userModules[(size_t) pick ((int) userModules.size())]->guid,
                                              { random.nextFloat() * 40.0f, random.nextFloat() * 20.0f });
                    }
                    break;
                case 4:
                    if (b != nullptr && b->modules.size() >= 2)
                    {
                        action = "connect";
                        auto& from = b->modules[(size_t) pick ((int) b->modules.size())];
                        auto& to = b->modules[(size_t) pick ((int) b->modules.size())];
                        if (from.entry != nullptr && to.entry != nullptr && from.entry->definition != nullptr && to.entry->definition != nullptr)
                        {
                            juce::String src, dst;
                            for (auto* ep : from.entry->definition->get_endpoint_definitions())
                                if (auto i = EndpointInfo::fromDefinition (*ep); i.canBeSource && (src.isEmpty() || random.nextBool()))
                                    src = from.path + "." + i.name;
                            for (auto* ep : to.entry->definition->get_endpoint_definitions())
                                if (auto i = EndpointInfo::fromDefinition (*ep); i.canBeTarget && (dst.isEmpty() || random.nextBool()))
                                    dst = to.path + "." + i.name;
                            if (src.isNotEmpty() && dst.isNotEmpty())
                                controller.connect (b->path, src, dst);
                        }
                    }
                    break;
                case 5:
                    if (b != nullptr && ! b->modules.empty())
                    {
                        action = "remove module";
                        controller.remove (b->modules[(size_t) pick ((int) b->modules.size())].path);
                    }
                    break;
                case 6:
                    if (b != nullptr && ! b->connections.empty())
                    {
                        action = "remove connection";
                        controller.remove (b->connections[(size_t) pick ((int) b->connections.size())].path);
                    }
                    break;
                case 7:
                    if (b != nullptr && random.nextInt (4) == 0)
                    {
                        action = "remove block";
                        controller.remove (b->path);
                    }
                    break;
                case 8:
                    if (b != nullptr)
                    {
                        action = "move block";
                        const int start = random.nextInt (4000);
                        controller.repositionBlock (b->path, start, start + juce::jmax (1, b->end - b->start));
                    }
                    break;
                case 9:
                    action = "add scene";
                    controller.addScene (random.nextInt (4000), 1 + random.nextInt (400));
                    break;
                case 10:
                    if (b != nullptr && ! b->modules.empty())
                    {
                        auto& m = b->modules[(size_t) pick ((int) b->modules.size())];
                        if (m.entry != nullptr && m.entry->definition != nullptr)
                            for (auto* ep : m.entry->definition->get_endpoint_definitions())
                                if (auto i = EndpointInfo::fromDefinition (*ep); i.isNumeric() && i.canBeTarget && random.nextBool())
                                {
                                    action = "add envelope";
                                    // copy first: each edit rebuilds the model, freeing `b` and `m`
                                    const auto blockPath = b->path, target = m.path + "." + i.name;
                                    const int length = juce::jmax (1, b->end - b->start);
                                    auto envelope = controller.addEnvelope (blockPath, target);
                                    if (envelope.isNotEmpty())
                                        controller.addControlPoint (envelope, random.nextInt (length), random.nextFloat());
                                    break;
                                }
                    }
                    break;
                case 11:
                    action = "transport";
                    controller.setPlayPosition (random.nextInt (5000));
                    controller.play (random.nextBool());
                    if (! p.scenes.empty())
                        controller.activateScene (p.scenes[(size_t) pick ((int) p.scenes.size())].path);
                    break;
                case 12:
                {
                    // undo, redo, undo: redo must restore exactly what undo took away
                    action = "undo";
                    const auto before = signature (p);
                    if (undo.undo())
                    {
                        model.rebuild();
                        const auto undone = signature (p);
                        undo.redo();
                        model.rebuild();
                        t.check (signature (p) == before, "fuzz step " + juce::String (step) + ": redo restores the state undo left");
                        undo.undo();
                        model.rebuild();
                        t.check (signature (p) == undone, "fuzz step " + juce::String (step) + ": undo again gives the same state");
                    }
                    break;
                }
                case 13: action = "redo"; undo.redo(); break;
            }

            model.rebuild();
            t.check (modelMatchesServer (session, p), "fuzz step " + juce::String (step) + " (" + action + "): model matches server");
            if (! pullAudio (host, 3))
                t.check (false, "fuzz step " + juce::String (step) + " (" + action + "): non-finite audio");
        }

        controller.play (false);
        model.rebuild();
        const auto finalState = signature (p);
        int undone = 0;
        while (undo.canUndo() && undone < 10000)
            undo.undo(), ++undone;
        model.rebuild();
        t.check (modelMatchesServer (session, p), "model matches server after undoing all " + juce::String (undone) + " steps");
        while (undo.canRedo())
            undo.redo();
        model.rebuild();
        t.check (signature (p) == finalState, "redoing everything gives back the final state");
        t.check (modelMatchesServer (session, p), "model matches server after redoing everything");
        t.check (pullAudio (host, 50), "finite audio after redoing everything");

        // --- long playback ------------------------------------------------------------
        t.check (controller.newDemoProject().wasOk(), "open the demo project");
        controller.play (true);
        const int callbacks = (int) (playbackSeconds * 44100.0 / 100.0);
        const auto started = juce::Time::getMillisecondCounterHiRes();
        bool finite = true;
        for (int done = 0; done < callbacks; done += 441)
        {
            finite = pullAudio (host, 441) && finite;      // 1 s
            if (random.nextInt (10) == 0)
                controller.setPlayPosition (random.nextInt (juce::jmax (1, p.tracks.empty() || p.tracks[0].blocks.empty() ? 800 : p.tracks[0].blocks.back().end)));
            if (! p.scenes.empty() && random.nextInt (10) == 0)
                controller.activateScene (p.scenes[(size_t) pick ((int) p.scenes.size())].path);
            model.rebuild();
        }
        controller.play (false);
        t.check (finite, "finite audio over " + juce::String (playbackSeconds) + " s of playback");
        t.check (modelMatchesServer (session, p), "model matches server after playback");
        t.note ("played " + juce::String (playbackSeconds) + " s in " + juce::String ((juce::Time::getMillisecondCounterHiRes() - started) / 1000.0, 1) + " s");

        controller.newProject();
        temp.deleteRecursively();
        return t.failures;
    }

    juce::String fingerprintModules (IntegraSession& session, ProjectModel& model, ProjectController& controller, AudioHost& host)
    {
        // every module at its defaults between the block's input and output, fed a fixed
        // signal (a 220 Hz sine plus a click every 0.5 s) for 2 s; RMS and peak of the
        // last second, so two builds (e.g. two Pd versions) can be compared
        host.stopNoDeviceThread();
        host.configure (2, 2, 44100);
        juce::String out;
        auto& p = model.get();

        for (auto* entry : session.getCatalogue().getUserModules())
        {
            if (! entry->hasImplementation || entry->definition == nullptr)
                continue;
            controller.newProject();
            model.rebuild();
            const auto block = p.tracks.front().blocks.front().path;
            auto module = controller.addModule (block, entry->guid, { 20.0f, 2.0f });
            model.rebuild();

            juce::String audioIn, audioOut;
            for (auto* ep : entry->definition->get_endpoint_definitions())
            {
                auto info = EndpointInfo::fromDefinition (*ep);
                if (info.isAudioStream && info.isAudioInput && audioIn.isEmpty())    audioIn = info.name;
                if (info.isAudioStream && ! info.isAudioInput && audioOut.isEmpty()) audioOut = info.name;
            }
            if (audioOut.isNotEmpty())
                controller.connect (block, module + "." + audioOut, block + ".StereoAudioOut1.in1");
            if (audioIn.isNotEmpty())
                controller.connect (block, block + ".AudioIn1.out", module + "." + audioIn);
            controller.setPlayPosition (0);
            controller.play (true);     // blocks only run while the playhead is inside them

            const int numSamples = 100, channels = 2, callbacks = 882;
            juce::AudioBuffer<float> in (channels, numSamples), buf (channels, numSamples);
            double sum = 0, peak = 0;
            int counted = 0;
            long sampleIndex = 0;
            for (int cb = 0; cb < callbacks; ++cb)
            {
                for (int i = 0; i < numSamples; ++i, ++sampleIndex)
                {
                    float v = 0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * 220.0 * (double) sampleIndex / 44100.0);
                    if (sampleIndex % 22050 == 0) v = 0.9f;
                    in.setSample (0, i, v);
                    in.setSample (1, i, v);
                }
                host.audioDeviceIOCallbackWithContext (in.getArrayOfReadPointers(), channels, buf.getArrayOfWritePointers(), channels, numSamples, {});
                if (cb >= callbacks / 2)
                    for (int i = 0; i < numSamples; ++i)
                    {
                        const double v = buf.getSample (0, i);
                        sum += v * v;
                        peak = juce::jmax (peak, std::abs (v));
                        ++counted;
                    }
            }
            controller.play (false);
            out << "fp " << entry->name << " " << juce::String (std::sqrt (sum / juce::jmax (1, counted)), 5) << " " << juce::String (peak, 5) << "\n";

            // and what it reported back (analysers' outputs, meters...)
            for (auto* ep : entry->definition->get_endpoint_definitions())
            {
                auto info = EndpointInfo::fromDefinition (*ep);
                if (! info.isAudioStream && info.kind != EndpointInfo::Kind::bang && info.canBeSource)
                {
                    const auto v = session.getValue (module + "." + info.name);
                    out << "fv " << entry->name << "." << info.name << " "
                        << (v.isDouble() ? juce::String ((double) v, 4) : v.toString().substring (0, 60)) << "\n";
                }
            }
        }
        controller.newProject();
        return out;
    }
}
