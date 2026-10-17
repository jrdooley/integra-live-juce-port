#include "SelfTest.h"
#include "Document.h"
#include "ModuleFile.h"

namespace modulecreator
{
    namespace
    {
        struct Tester
        {
            juce::String& log;
            int failures = 0;

            void check (bool ok, const juce::String& what)
            {
                log << (ok ? "ok   " : "FAIL ") << what << "\n";
                if (! ok)
                    ++failures;
            }
        };

        std::unique_ptr<juce::XmlElement> readInterfaceDefinition (const juce::File& moduleFile)
        {
            juce::ZipFile zip (moduleFile);
            const int i = zip.getIndexOfFileName (ModuleFile::interfaceDefinitionPath);
            if (i < 0)
                return {};
            std::unique_ptr<juce::InputStream> in (zip.createStreamForEntry (i));
            return in != nullptr ? juce::parseXML (in->readEntireStreamAsString()) : nullptr;
        }

        /** Element-by-element comparison, ignoring whitespace around text; describes the first difference. */
        juce::String compareXml (const juce::XmlElement& a, const juce::XmlElement& b, const juce::String& path)
        {
            const auto here = path + "/" + a.getTagName();
            if (a.getTagName() != b.getTagName())
                return here + ": <" + a.getTagName() + "> vs <" + b.getTagName() + ">";

            for (int i = 0; i < a.getNumAttributes(); ++i)
                if (b.getStringAttribute (a.getAttributeName (i)) != a.getAttributeValue (i))
                    return here + "@" + a.getAttributeName (i);
            if (a.getNumAttributes() != b.getNumAttributes())
                return here + ": attribute count";

            juce::Array<const juce::XmlElement*> ca, cb;
            for (auto* c : a.getChildIterator())  if (! c->isTextElement()) ca.add (c);
            for (auto* c : b.getChildIterator())  if (! c->isTextElement()) cb.add (c);

            if (ca.isEmpty() && cb.isEmpty())
                return a.getAllSubText().trim() == b.getAllSubText().trim()
                           ? juce::String() : here + ": '" + a.getAllSubText().trim() + "' vs '" + b.getAllSubText().trim() + "'";

            for (int i = 0; i < juce::jmin (ca.size(), cb.size()); ++i)
            {
                auto d = compareXml (*ca[i], *cb[i], here);
                if (d.isNotEmpty())
                    return d;
            }
            return ca.size() == cb.size() ? juce::String()
                                          : here + ": " + juce::String (ca.size()) + " vs " + juce::String (cb.size()) + " children";
        }

        std::map<juce::String, juce::MemoryBlock> implementationFiles (const juce::File& moduleFile)
        {
            std::map<juce::String, juce::MemoryBlock> result;
            juce::ZipFile zip (moduleFile);
            for (int i = 0; i < zip.getNumEntries(); ++i)
            {
                auto* e = zip.getEntry (i);
                if (e == nullptr || ! e->filename.startsWith (ModuleFile::implementationDirectory + "/") || e->uncompressedSize == 0)
                    continue;
                std::unique_ptr<juce::InputStream> in (zip.createStreamForEntry (i));
                juce::MemoryBlock data;
                if (in != nullptr)
                    in->readIntoMemoryBlock (data);
                result[e->filename] = data;
            }
            return result;
        }

        bool hasIssue (const std::vector<ValidationIssue>& issues, const juce::String& text)
        {
            for (auto& i : issues)
                if (i.message.contains (text))
                    return true;
            return false;
        }
    }

    int runSelfTest (const CreatorConfig& config, const juce::File& modulesDirectory, juce::String& log)
    {
        Tester t { log };
        auto scratch = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("module-creator-self-test-" + createGuid());
        scratch.createDirectory();
        auto workingDirectory = scratch.getChildFile ("working");
        workingDirectory.createDirectory();
        workingDirectory.getChildFile ("Integra Module Creator Working Directory").createDirectory();
        workingDirectory.getChildFile ("Integra Module Creator Working Directory/my precious file.txt").replaceWithText ("keep me");

        t.check (config.widgets.size() == 20 && config.findWidget ("XYPanPad") != nullptr
                     && config.findWidget ("XYPanPad")->attributes == juce::StringArray ("x", "y"),
                 "config: 20 widget types, XYPanPad has attributes x and y");
        t.check (config.standardTags.contains ("delay") && ! config.hostArguments.isEmpty(), "config: standard tags and Pd arguments");

        Document document (config, workingDirectory);
        document.developerMode = true;      // shipped modules use the 'core' tag
        t.check (document.getWorkingDirectory().getFileName() == "Integra Module Creator Working Directory 2"
                     && workingDirectory.getChildFile ("Integra Module Creator Working Directory/my precious file.txt").existsAsFile(),
                 "a folder of the user's own files is never used (and emptied) as the working directory");

        // --- every shipped module: load, validate, save, compare ---------------------
        auto modules = modulesDirectory.findChildFiles (juce::File::findFiles, false, "*.module");
        modules.sort();
        t.check (modules.size() >= 80, "found the shipped modules (" + juce::String (modules.size()) + ") in " + modulesDirectory.getFullPathName());

        int roundTrips = 0;
        for (auto& moduleFile : modules)
        {
            auto r = document.open (moduleFile);
            if (! r.wasOk())
            {
                t.check (false, moduleFile.getFileName() + " loads: " + r.getErrorMessage());
                continue;
            }

            juce::StringArray problems;
            for (auto& issue : document.validate())
                problems.add (issue.message);
            const bool valid = problems.isEmpty();

            auto original = readInterfaceDefinition (moduleFile);
            auto written = document.definition.toXml();
            auto difference = original != nullptr ? compareXml (*original, *written, {}) : juce::String ("unreadable");

            auto saved = scratch.getChildFile ("saved").getChildFile (moduleFile.getFileName());
            saved.getParentDirectory().createDirectory();
            auto saveResult = document.save (saved);
            auto reread = readInterfaceDefinition (saved);
            auto savedDifference = reread != nullptr ? compareXml (*original, *reread, {}) : juce::String ("unreadable");
            const bool sameImplementation = implementationFiles (moduleFile) == implementationFiles (saved);

            const bool ok = valid && difference.isEmpty() && saveResult.wasOk() && savedDifference.isEmpty() && sameImplementation;
            roundTrips += ok ? 1 : 0;
            if (! ok)
                t.check (false, moduleFile.getFileName() + ": " + problems.joinIntoString ("; ") + " " + difference + " " + savedDifference
                                + (sameImplementation ? "" : " implementation files differ"));
        }
        t.check (roundTrips == modules.size(), juce::String (roundTrips) + " shipped modules validate and save unchanged (interface definition and implementation)");

        // MODULE_CREATOR_SELF_TEST_OUTPUT: keep the saved modules, for libIntegra's smoke test to load
        const juce::File output (juce::SystemStats::getEnvironmentVariable ("MODULE_CREATOR_SELF_TEST_OUTPUT", {}));
        if (output != juce::File())
        {
            output.deleteRecursively();
            t.check (scratch.getChildFile ("saved").copyDirectoryTo (output.getChildFile ("modules")), "saved modules kept in " + output.getFullPathName());
        }

        // --- templates ----------------------------------------------------------------
        document.developerMode = false;
        auto templates = config.getTemplatesDirectory().findChildFiles (juce::File::findFiles, true, "*.module");
        t.check (templates.size() == 9, "9 templates");
        for (auto& f : templates)
        {
            auto r = document.importTemplate (f);
            auto issues = document.validate();
            // a template has no name until the user gives it one
            const bool onlyName = issues.size() == 1 && issues[0].message == "You must provide an Interface Name";
            t.check (r.wasOk() && onlyName && document.definition.name.isEmpty() && ! document.modified
                         && document.definition.patchName.isNotEmpty() && document.getMainPatch().existsAsFile(),
                     "import template " + f.getFileNameWithoutExtension() + " " + r.getErrorMessage()
                         + (issues.empty() ? juce::String() : " (" + issues[0].message + ")"));

            // a module made from each template, for the smoke test
            if (output != juce::File())
            {
                document.definition.name = "Test" + f.getFileNameWithoutExtension();
                document.changed();
                t.check (document.validate().empty() && document.save (output.getChildFile ("modules").getChildFile (document.definition.name + ".module")).wasOk(),
                         "a module made from " + f.getFileNameWithoutExtension() + " saved for the smoke test");
            }
        }

        // --- a module from scratch -------------------------------------------------------
        document.newFile();
        auto& d = document.definition;
        t.check (! document.validate().empty(), "an empty module doesn't validate");
        d.name = "lowercase";
        t.check (hasIssue (document.validate(), "UpperCamelCase"), "interface name must be UpperCamelCase");
        d.name = "MyGain";
        d.label = "My Gain";
        d.description = "Changes the **level** of a signal";
        d.author = "Self Test";
        d.tags = { "simple", "processing" };
        t.check (hasIssue (document.validate(), "You must create at least one Endpoint"), "needs an endpoint");

        auto& in = d.addEndpoint();
        in.name = "in1";  in.isControl = false;  in.isInputStream = true;
        const int inUid = in.uid;
        auto& out = d.addEndpoint();
        out.name = "out1";  out.isControl = false;  out.isInputStream = false;
        auto& gain = d.addEndpoint();
        gain.name = "gain";  gain.minimum = "0";  gain.maximum = "2";  gain.defaultValue = "1";
        gain.stateLabels = { { "0", "silent" }, { "1", "unity" } };
        const int gainUid = gain.uid;
        auto& mode = d.addEndpoint();
        mode.name = "mode";  mode.stateType = StateType::stringType;  mode.useRange = false;
        mode.allowedStates = { "soft", "hard" };  mode.defaultValue = "soft";
        const int modeUid = mode.uid;
        auto& reset = d.addEndpoint();
        reset.name = "reset";  reset.isState = false;
        t.check (document.validate().empty(), "a complete module validates");

        d.findEndpoint (gainUid)->defaultValue = "3";
        t.check (hasIssue (document.validate(), "not within the allowed range"), "default must be inside the range");
        d.findEndpoint (gainUid)->defaultValue = "1";
        d.findEndpoint (gainUid)->scale = "exponential";
        t.check (hasIssue (document.validate(), "Exponential Scales"), "exponential scale needs a positive minimum");
        d.findEndpoint (gainUid)->scale = "decibel";
        d.findEndpoint (modeUid)->allowedStates.add ("soft");
        t.check (hasIssue (document.validate(), "duplicates"), "allowed values must be unique");
        d.findEndpoint (modeUid)->allowedStates.remove (2);
        d.findEndpoint (inUid)->name = "init";
        t.check (hasIssue (document.validate(), "reserved"), "reserved endpoint names");
        d.findEndpoint (inUid)->name = "gain";
        t.check (hasIssue (document.validate(), "unique"), "endpoint names must be unique");
        d.findEndpoint (inUid)->name = "in1";
        d.tags.add (coreTag);
        t.check (hasIssue (document.validate(), "reserved for libIntegra developers"), "'core' tag is reserved");
        d.tags.removeString (coreTag);
        d.findEndpoint (modeUid)->isInputFile = true;
        t.check (hasIssue (document.validate(), "dataDirectory"), "input files need a dataDirectory endpoint");
        d.findEndpoint (modeUid)->isInputFile = false;

        // widgets: a slider, and an XY pad shared by two endpoints
        auto* slider = config.findWidget ("Slider");
        auto* pad = config.findWidget ("XYPanPad");
        d.assignWidget (gainUid, "Slider", "value", 0, slider->defaultSize);
        d.assignWidget (modeUid, "XYPanPad", "x", 0, pad->defaultSize);
        const int padUid = d.widgetFor (modeUid)->uid;
        auto& extra = d.addEndpoint();
        extra.name = "spread";
        const int extraUid = extra.uid;
        d.assignWidget (extraUid, "XYPanPad", "y", padUid, pad->defaultSize);
        t.check (d.widgets.size() == 2 && d.widgetFor (extraUid) == d.widgetFor (modeUid) && d.widgetFor (modeUid)->mappings.size() == 2,
                 "two endpoints share an XY pad");
        t.check (d.labelOf (*d.widgetFor (gainUid)) == "gain" && d.labelOf (*d.widgetFor (modeUid)) == "xypanpad",
                 "a widget showing one endpoint takes its name; a shared one has its own label");
        t.check (d.controlsWithoutWidgets() == juce::StringArray ("reset"), "controls without widgets");
        d.widgetFor (gainUid)->bounds.setX (500);
        d.sortEndpoints();
        juce::StringArray order;
        for (auto& e : d.endpoints)
            order.add (e.name);
        t.check (order == juce::StringArray ("in1", "out1", "mode", "spread", "gain", "reset"),
                 "sort: streams, then controls by widget position, then the rest (" + order.joinIntoString (",") + ")");
        d.autoArrangeWidgets (10);
        t.check (d.widgets[1].bounds.getX() == 10 && d.widgets[0].bounds.getX() == 10 + pad->defaultSize.x + 10, "auto arrange");
        d.findEndpoint (modeUid)->name = "Mode";
        t.check (hasIssue (document.validate(), "lowerCamelCase"), "endpoint names are lowerCamelCase");
        d.findEndpoint (modeUid)->name = "mode";

        // the implementation: a patch using an abstraction, a missing object and an unused file
        auto wd = document.getWorkingDirectory();
        wd.getChildFile ("MyGain.pd").replaceWithText ("#N canvas 0 0 450 300 10;\n#X obj 10 10 inlet~;\n#X obj 10 40 *~ 1;\n"
                                                      "#X obj 10 70 helper;\n#X obj 10 100 nosuchexternal~ 3;\n#X obj 10 130 outlet~;\n");
        wd.getChildFile ("helper.pd").replaceWithText ("#N canvas 0 0 450 300 10;\n#X obj 10 10 bonk~;\n#X obj 10 40 helper;\n");
        wd.getChildFile ("notes.txt").replaceWithText ("unused");
        t.check (document.pollWorkingDirectory() && document.modified, "new files in the working directory are noticed");
        auto& scan = document.getScan();
        t.check (d.patchName == "MyGain.pd" && scan.patchNames == juce::StringArray ("helper.pd", "MyGain.pd"), "the patch named after the module becomes the main patch");
        t.check (scan.dependencies.size() == 1 && scan.dependencies[0].label == "helper.pd", "dependency tree (circular reference skipped)");
        t.check (scan.missingObjects.size() == 1 && scan.missingObjects[0].children.size() == 1
                     && scan.missingObjects[0].children[0].label == "nosuchexternal~", "missing objects");
        t.check (scan.unusedFiles.size() == 1 && scan.unusedFiles[0].label == "notes.txt", "unused files");
        t.check (hasIssue (document.validate(), "Dependencies of the PD patch"), "missing objects stop saving");
        wd.getChildFile ("MyGain.pd").replaceWithText ("#N canvas 0 0 450 300 10;\n#X obj 10 10 inlet~;\n#X obj 10 40 *~ 1;\n"
                                                      "#X obj 10 70 helper;\n#X obj 10 130 outlet~;\n");
        juce::Thread::sleep (20);
        document.pollWorkingDirectory();
        t.check (document.validate().empty(), "valid once the dependency is gone");

        // host patch for editing in Pd
        juce::File host;
        auto hostResult = HostPatch::create (scratch.getChildFile ("host"), document.getMainPatch(), d, config, host);
        const auto hostText = host.loadFileAsString();
        t.check (hostResult.wasOk() && hostText.contains ("#X connect 0 0 1 0;") && hostText.contains ("#X connect 1 0 2 0;")
                     && hostText.contains ("send-value float gain 1 0 2;") && hostText.contains ("send-value symbol mode soft;")
                     && hostText.contains ("send-value bang reset bang;") && ! hostText.contains ("/*")
                     && scratch.getChildFile ("host/send-value.pd").existsAsFile(),
                 "host patch: audio connections and a box per endpoint " + hostResult.getErrorMessage());

        // save, reopen
        auto myGain = scratch.getChildFile ("MyGain.module");
        auto saveResult = document.save (myGain);
        t.check (saveResult.wasOk() && ! document.modified, "save " + saveResult.getErrorMessage());
        const auto savedGuid = d.moduleGuid;
        const auto savedXml = d.toXmlString();
        document.newFile();
        auto openResult = document.open (myGain);
        t.check (openResult.wasOk() && document.definition.toXmlString() == savedXml && document.definition.moduleGuid == savedGuid
                     && document.getWorkingDirectory().getChildFile ("helper.pd").existsAsFile(),
                 "reopen gives the same interface definition and implementation " + openResult.getErrorMessage());
        {
            juce::ZipFile zip (myGain);
            t.check (zip.getIndexOfFileName (ModuleFile::licensePath) >= 0
                         && zip.getIndexOfFileName (ModuleFile::implementationDirectory + "/MyGain.pd") >= 0,
                     "the module holds the licence and the implementation");
        }
        document.changed();
        t.check (document.definition.moduleGuid != savedGuid && document.modified, "an edit gives the module a new id");

        // bundles
        auto bundle = scratch.getChildFile ("Test.bundle");
        t.check (ModuleFile::createBundle (bundle, { myGain, modules.isEmpty() ? myGain : modules[0] }).wasOk(), "create bundle");
        auto extractTo = scratch.getChildFile ("extracted");
        extractTo.createDirectory();
        juce::StringArray report;
        ModuleFile::extractBundle (bundle, extractTo, report);
        t.check (extractTo.getChildFile ("MyGain.module").existsAsFile() && report.size() == 2, "extract bundle");
        report.clear();
        ModuleFile::extractBundle (bundle, extractTo, report);
        t.check (report.size() == 2 && report[0].contains ("already exists"), "extracting doesn't overwrite");

        // a module with an unsafe path in its implementation is refused
        {
            auto evil = scratch.getChildFile ("Evil.module");
            juce::ZipFile::Builder b;
            juce::MemoryBlock iid;
            { juce::MemoryOutputStream o (iid, false); o << savedXml; }
            b.addEntry (new juce::MemoryInputStream (iid, true), 9, ModuleFile::interfaceDefinitionPath, juce::Time::getCurrentTime());
            juce::MemoryBlock payload ("x", 1);
            b.addEntry (new juce::MemoryInputStream (payload, true), 9, ModuleFile::implementationDirectory + "/../../escaped.pd", juce::Time::getCurrentTime());
            juce::FileOutputStream o (evil);
            b.writeToStream (o, nullptr);
        }
        t.check (document.open (scratch.getChildFile ("Evil.module")).failed() && ! scratch.getChildFile ("escaped.pd").exists(),
                 "a module with ../ paths is refused");

        document.newFile();
        scratch.deleteRecursively();
        return t.failures;
    }
}
