#include "Implementation.h"
#include "CreatorConfig.h"

namespace modulecreator
{
    namespace
    {
        void loadObjectSet (const juce::File& file, std::set<juce::String>& result)
        {
            for (auto& line : juce::StringArray::fromLines (file.loadFileAsString()))
                if (line.trim().isNotEmpty())
                    result.insert (line.trim());
        }

        bool isHiddenFile (const juce::File& f)     { return f.isHidden() || f.getFileName().startsWithChar ('.'); }

        juce::Array<juce::File> listDirectory (const juce::File& directory)
        {
            juce::Array<juce::File> result;
            for (const auto& entry : juce::RangedDirectoryIterator (directory, false, "*", juce::File::findFilesAndDirectories))
                if (! isHiddenFile (entry.getFile()))
                    result.add (entry.getFile());
            result.sort();
            return result;
        }

        juce::String relativePath (const juce::File& root, const juce::File& file)
        {
            return file.isAChildOf (root) ? file.getRelativePathFrom (root).replaceCharacter ('\\', '/')
                                          : file.getFullPathName().replaceCharacter ('\\', '/');
        }

        bool isNumericalConstant (const juce::String& s)
        {
            double d = 0;
            return parseNumber (s, d);
        }

        /** Implementation.loadDependencies: the non-vanilla objects of a patch, recursively. */
        void loadDependencies (const juce::File& pdFile, const juce::File& root, const PdObjectSets& sets,
                               std::vector<FileTreeNode>& results, std::vector<FileTreeNode>& missing,
                               juce::StringArray ancestors, std::set<juce::String>& usedFiles)
        {
            if (! pdFile.existsAsFile())
                return;
            usedFiles.insert (pdFile.getFullPathName());
            ancestors.add (pdFile.getFullPathName());

            std::set<juce::String> seen;
            FileTreeNode* missingParent = nullptr;

            for (auto line : juce::StringArray::fromLines (pdFile.loadFileAsString()))
            {
                auto words = juce::StringArray::fromTokens (line.trim(), " ", {});
                if (words.size() < 5 || words[1] != "obj")
                    continue;
                auto object = words[4].removeCharacters (";").trim();
                if (object.isEmpty() || sets.internalObjects.count (object) != 0 || isNumericalConstant (object) || ! seen.insert (object).second)
                    continue;

                auto abstraction = pdFile.getParentDirectory().getChildFile (object + ".pd");
                if (abstraction.existsAsFile())
                {
                    if (ancestors.contains (abstraction.getFullPathName()))
                        continue;   // circular dependency
                    FileTreeNode node;
                    node.label = relativePath (root, abstraction);
                    loadDependencies (abstraction, root, sets, node.children, missing, ancestors, usedFiles);
                    results.push_back (std::move (node));
                }
                else if (sets.supportedAbstractions.count (object) == 0 && sets.supportedExternals.count (object) == 0)
                {
                    if (missingParent == nullptr)
                    {
                        missing.push_back ({ relativePath (root, pdFile), {}, {} });
                        missingParent = &missing.back();
                    }
                    missingParent->children.push_back ({ object, {}, {} });
                }
            }
        }

        std::vector<FileTreeNode> findUnusedFiles (const juce::File& directory, const std::set<juce::String>& usedFiles)
        {
            std::vector<FileTreeNode> result;
            for (auto& f : listDirectory (directory))
            {
                if (f.isDirectory())
                {
                    auto children = findUnusedFiles (f, usedFiles);
                    if (! children.empty())
                        result.push_back ({ f.getFileName(), {}, std::move (children) });
                }
                else if (usedFiles.count (f.getFullPathName()) == 0)
                {
                    result.push_back ({ f.getFileName(), f, {} });
                }
            }
            return result;
        }

        std::vector<FileTreeNode> findOversizedFiles (const juce::File& directory)
        {
            std::vector<FileTreeNode> result;
            for (auto& f : listDirectory (directory))
            {
                if (f.isDirectory())
                {
                    auto children = findOversizedFiles (f);
                    if (! children.empty())
                        result.push_back ({ f.getFileName(), {}, std::move (children) });
                }
                else if (f.getSize() >= oversizedFileThreshold)
                {
                    result.push_back ({ f.getFileName() + " (" + juce::File::descriptionOfSizeInBytes (f.getSize()) + ")", {}, {} });
                }
            }
            return result;
        }

        void snapshot (const juce::File& directory, const juce::String& prefix, std::map<juce::String, juce::int64>& result)
        {
            for (auto& f : listDirectory (directory))
            {
                if (f.isDirectory())
                    snapshot (f, prefix + f.getFileName() + "/", result);
                else
                    result[prefix + f.getFileName()] = f.getLastModificationTime().toMilliseconds() ^ (f.getSize() << 40);
            }
        }

        /** Pd needs forward slashes and backslash-escaped spaces. */
        juce::String pdEscape (const juce::String& s)
        {
            return s.replaceCharacter ('\\', '/').replace (" ", "\\ ");
        }

        juce::String numberText (double d)
        {
            return juce::String (d, 10).trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
        }
    }

    //==============================================================================
    void PdObjectSets::load (const CreatorConfig& config)
    {
        loadObjectSet (config.resourcesDirectory.getChildFile ("pd_internal_objects.txt"), internalObjects);
        loadObjectSet (config.resourcesDirectory.getChildFile ("supported_pd_abstractions.txt"), supportedAbstractions);
        loadObjectSet (config.resourcesDirectory.getChildFile ("supported_pd_externals.txt"), supportedExternals);
    }

    ImplementationScan ImplementationScan::scan (const juce::File& workingDirectory, const juce::String& patchName, const PdObjectSets& sets)
    {
        ImplementationScan s;
        for (auto& f : listDirectory (workingDirectory))
            if (f.existsAsFile() && f.hasFileExtension ("pd"))
                s.patchNames.add (f.getFileName());

        std::set<juce::String> usedFiles;
        if (patchName.isNotEmpty() && s.patchNames.contains (patchName))
        {
            const auto patch = workingDirectory.getChildFile (patchName);
            loadDependencies (patch, workingDirectory, sets, s.dependencies, s.missingObjects, {}, usedFiles);
        }

        s.unusedFiles = findUnusedFiles (workingDirectory, usedFiles);
        s.oversizedFiles = findOversizedFiles (workingDirectory);
        return s;
    }

    std::map<juce::String, juce::int64> snapshotDirectory (const juce::File& directory)
    {
        std::map<juce::String, juce::int64> result;
        snapshot (directory, {}, result);
        return result;
    }

    void collectFiles (const std::vector<FileTreeNode>& nodes, juce::Array<juce::File>& result)
    {
        for (auto& n : nodes)
        {
            if (n.file != juce::File())
                result.add (n.file);
            collectFiles (n.children, result);
        }
    }

    //==============================================================================
    juce::String HostPatch::generateConnections (const ModuleDefinition& d)
    {
        constexpr int maxInputs = 8, maxOutputs = 8;
        int inputs = 0, outputs = 0;
        for (auto& e : d.endpoints)
        {
            if (e.isAudioStream (true))     ++inputs;
            if (e.isAudioStream (false))    ++outputs;
        }

        juce::String connections;
        for (int i = 0; i < juce::jmin (inputs, maxInputs); ++i)
            connections << "#X connect 0 " << i << " 1 " << i << ";\n";
        for (int i = 0; i < juce::jmin (outputs, maxOutputs); ++i)
            connections << "#X connect 1 " << i << " 2 " << i << ";\n";
        return connections;
    }

    juce::String HostPatch::generateSendValues (const ModuleDefinition& d)
    {
        constexpr int xStart = 132, yStart = 30, xSpacing = 104, ySpacing = 54, columns = 5;
        int x = xStart, y = yStart, column = 0;
        juce::String result;

        for (auto& e : d.endpoints)
        {
            if (! e.isControl || ! e.isSentToHost)
                continue;

            juce::String type, defaultValue, minimum, maximum;
            if (e.isState)
            {
                if (e.stateType == StateType::stringType)
                {
                    type = "symbol";
                }
                else
                {
                    type = "float";
                    if (e.useRange)
                    {
                        minimum = e.minimum;
                        maximum = e.maximum;
                    }
                    else
                    {
                        // AllowedValues.minimum / maximum
                        bool first = true;
                        double lo = 0, hi = 0;
                        for (auto& v : e.allowedStates)
                        {
                            double n = 0;
                            if (! parseNumber (v, n))
                                continue;
                            lo = first ? n : juce::jmin (lo, n);
                            hi = first ? n : juce::jmax (hi, n);
                            first = false;
                        }
                        minimum = numberText (lo);
                        maximum = numberText (hi);
                    }
                }
                defaultValue = e.defaultValue;
            }
            else
            {
                type = defaultValue = "bang";
            }

            if (defaultValue.isEmpty())
                defaultValue = type == "symbol" ? "symbol" : "0";

            result << "#X obj " << x << " " << y << " send-value " << type << " " << e.name << " " << pdEscape (defaultValue);
            if (minimum.isNotEmpty() && maximum.isNotEmpty())
                result << " " << minimum << " " << maximum;
            result << ";\n";

            if (++column >= columns)
            {
                column = 0;
                x = xStart;
                y += ySpacing;
            }
            else
            {
                x += xSpacing;
            }
        }
        return result;
    }

    juce::Result HostPatch::create (const juce::File& directory, const juce::File& modulePatch,
                                    const ModuleDefinition& d, const CreatorConfig& config, juce::File& hostPatch)
    {
        if (! modulePatch.existsAsFile())
            return juce::Result::fail ("The main patch " + modulePatch.getFullPathName() + " doesn't exist");

        const auto hostDirectory = config.getHostDirectory();
        const auto templateFile = hostDirectory.getChildFile ("host.pd_template");
        if (! templateFile.existsAsFile())
            return juce::Result::fail ("Can't open the host template " + templateFile.getFullPathName());

        directory.createDirectory();
        for (auto name : { "bang-box.pd", "float-box.pd", "send-value.pd", "symbol-box.pd" })
            if (! hostDirectory.getChildFile (name).copyFileTo (directory.getChildFile (name)))
                return juce::Result::fail (juce::String ("Can't copy the host abstraction ") + name);

        auto host = templateFile.loadFileAsString();
        host = host.replace ("/* MODULE PATH */", pdEscape (modulePatch.getParentDirectory().getFullPathName()));
        host = host.replace ("/* MODULE NAME */", pdEscape (modulePatch.getFileNameWithoutExtension()));
        host = host.replace ("/* MODULE CONNECTIONS */", generateConnections (d).trimEnd());
        host = host.replace ("/* ENDPOINT SENDVALUE INSTANCES */", generateSendValues (d).trimEnd());

        hostPatch = directory.getChildFile ("host.pd");
        if (! hostPatch.replaceWithText (host, false, false, "\n"))
            return juce::Result::fail ("Can't write " + hostPatch.getFullPathName());
        return juce::Result::ok();
    }

    //==============================================================================
    juce::File findPdExecutable (const juce::String& configuredPath)
    {
        if (configuredPath.isNotEmpty())
        {
            juce::File configured (configuredPath);
           #if JUCE_MAC
            // the .app itself was chosen
            if (configured.isDirectory() && configured.hasFileExtension ("app"))
                configured = configured.getChildFile ("Contents/Resources/bin/pd");
           #endif
            if (configured.existsAsFile())
                return configured;
        }

        juce::Array<juce::File> candidates;
       #if JUCE_MAC
        for (auto& app : juce::File ("/Applications").findChildFiles (juce::File::findDirectories, false, "Pd*.app"))
            candidates.add (app.getChildFile ("Contents/Resources/bin/pd"));
       #elif JUCE_WINDOWS
        for (auto folder : { juce::File::globalApplicationsDirectory, juce::File::globalApplicationsDirectoryX86 })
            candidates.add (juce::File::getSpecialLocation (folder).getChildFile ("Pd\\bin\\pd.exe"));
       #endif

        for (auto& dir : juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("PATH", {}),
                                                        juce::File::getSeparatorChar() == '\\' ? ";" : ":", {}))
            if (dir.isNotEmpty() && juce::File::isAbsolutePath (dir))
                candidates.add (juce::File (dir).getChildFile (juce::File::getSeparatorChar() == '\\' ? "pd.exe" : "pd"));

        for (auto& c : candidates)
            if (c.existsAsFile())
                return c;
        return {};
    }
}
