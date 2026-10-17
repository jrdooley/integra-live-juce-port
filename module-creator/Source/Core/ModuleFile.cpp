#include "ModuleFile.h"
#include "CreatorConfig.h"

namespace modulecreator
{
    namespace
    {
        /** A zip entry name that stays inside the directory it is extracted to. */
        bool isSafeRelativePath (const juce::String& path)
        {
            if (path.isEmpty() || path.startsWithChar ('/') || path.startsWithChar ('\\') || path.containsChar (':'))
                return false;
            for (auto& part : juce::StringArray::fromTokens (path.replaceCharacter ('\\', '/'), "/", {}))
                if (part == "..")
                    return false;
            return true;
        }

        void addFilesToZip (juce::ZipFile::Builder& zip, const juce::File& directory, const juce::String& zipPath)
        {
            juce::Array<juce::File> children;
            for (const auto& entry : juce::RangedDirectoryIterator (directory, false, "*", juce::File::findFilesAndDirectories))
                children.add (entry.getFile());
            children.sort();

            for (auto& child : children)
            {
                if (child.isHidden() || child.getFileName().startsWithChar ('.'))
                    continue;
                const auto path = zipPath + "/" + child.getFileName();
                if (child.isDirectory())
                    addFilesToZip (zip, child, path);
                else
                    zip.addFile (child, 9, path);
            }
        }
    }

    void ModuleFile::clearDirectory (const juce::File& directory)
    {
        for (auto& f : directory.findChildFiles (juce::File::findFilesAndDirectories, false, "*"))
            f.deleteRecursively();
    }

    juce::Result ModuleFile::load (const juce::File& moduleFile, const CreatorConfig& config, ModuleDefinition& result,
                                   const juce::File& workingDirectory)
    {
        if (! moduleFile.existsAsFile())
            return juce::Result::fail ("can't open file");

        juce::ZipFile zip (moduleFile);
        const int iidIndex = zip.getIndexOfFileName (interfaceDefinitionPath);
        if (iidIndex < 0)
            return juce::Result::fail ("Can't extract interface definition " + interfaceDefinitionPath);

        std::unique_ptr<juce::InputStream> iidStream (zip.createStreamForEntry (iidIndex));
        if (iidStream == nullptr)
            return juce::Result::fail ("Can't extract interface definition " + interfaceDefinitionPath);
        auto xml = juce::XmlDocument::parse (iidStream->readEntireStreamAsString());
        if (xml == nullptr || ! xml->hasTagName ("InterfaceDeclaration"))
            return juce::Result::fail ("the interface definition is not valid XML");

        ModuleDefinition definition;
        auto r = ModuleDefinition::fromXml (*xml, config, definition);
        if (r.failed())
            return r;

        // Implementation.retrieveImplementation
        // (the caller has emptied the working directory)
        workingDirectory.createDirectory();
        const auto prefix = implementationDirectory + "/";
        for (int i = 0; i < zip.getNumEntries(); ++i)
        {
            auto* entry = zip.getEntry (i);
            if (entry == nullptr || ! entry->filename.startsWith (prefix) || entry->uncompressedSize == 0 || entry->filename.endsWithChar ('/'))
                continue;
            const auto relative = entry->filename.substring (prefix.length());
            if (! isSafeRelativePath (relative))
                return juce::Result::fail ("the module contains an unsafe file name: " + entry->filename);

            auto target = workingDirectory.getChildFile (relative);
            target.getParentDirectory().createDirectory();
            std::unique_ptr<juce::InputStream> in (zip.createStreamForEntry (i));
            if (in == nullptr)
                return juce::Result::fail ("can't extract " + entry->filename);
            target.deleteFile();
            juce::FileOutputStream out (target);
            if (! out.openedOk())
                return juce::Result::fail ("can't write " + target.getFullPathName());
            out.writeFromInputStream (*in, -1);
        }

        result = std::move (definition);
        return juce::Result::ok();
    }

    juce::Result ModuleFile::save (const juce::File& moduleFile, const ModuleDefinition& definition,
                                   const juce::File& workingDirectory, const juce::File& licenseFile)
    {
        juce::ZipFile::Builder zip;
        juce::MemoryBlock iid;
        {
            juce::MemoryOutputStream out (iid, false);
            out << definition.toXmlString();
        }
        zip.addEntry (new juce::MemoryInputStream (iid, true), 9, interfaceDefinitionPath, juce::Time::getCurrentTime());

        if (licenseFile.existsAsFile())
            zip.addFile (licenseFile, 9, licensePath);

        if (workingDirectory.isDirectory())
            addFilesToZip (zip, workingDirectory, implementationDirectory);

        // write beside the target and swap, so a failed save keeps the old file
        juce::TemporaryFile temp (moduleFile);
        {
            juce::FileOutputStream out (temp.getFile());
            if (! out.openedOk())
                return juce::Result::fail ("can't write " + moduleFile.getFullPathName());
            if (! zip.writeToStream (out, nullptr))
                return juce::Result::fail ("can't write " + moduleFile.getFullPathName());
        }
        if (! temp.overwriteTargetFileWithTemporary())
            return juce::Result::fail ("can't write " + moduleFile.getFullPathName());
        return juce::Result::ok();
    }

    //==============================================================================
    juce::Result ModuleFile::createBundle (const juce::File& bundleFile, const juce::Array<juce::File>& moduleFiles)
    {
        juce::ZipFile::Builder zip;
        for (auto& f : moduleFiles)
            zip.addFile (f, 9, f.getFileName());

        juce::TemporaryFile temp (bundleFile);
        {
            juce::FileOutputStream out (temp.getFile());
            if (! out.openedOk() || ! zip.writeToStream (out, nullptr))
                return juce::Result::fail ("can't write " + bundleFile.getFullPathName());
        }
        if (! temp.overwriteTargetFileWithTemporary())
            return juce::Result::fail ("can't write " + bundleFile.getFullPathName());
        return juce::Result::ok();
    }

    juce::Result ModuleFile::extractBundle (const juce::File& bundleFile, const juce::File& directory, juce::StringArray& report)
    {
        juce::ZipFile zip (bundleFile);
        if (zip.getNumEntries() == 0)
            return juce::Result::fail (bundleFile.getFileName() + " is not a module bundle");

        for (int i = 0; i < zip.getNumEntries(); ++i)
        {
            auto* entry = zip.getEntry (i);
            if (entry == nullptr)
                continue;
            const auto name = entry->filename.fromLastOccurrenceOf ("/", false, false);
            if (! isSafeRelativePath (name) || name.isEmpty())
                continue;

            auto target = directory.getChildFile (name);
            if (target.exists())
            {
                report.add ("Can't extract to " + target.getFullPathName() + " - file already exists!");
                continue;
            }
            std::unique_ptr<juce::InputStream> in (zip.createStreamForEntry (i));
            juce::FileOutputStream out (target);
            if (in == nullptr || ! out.openedOk())
            {
                report.add ("Can't extract " + name);
                continue;
            }
            out.writeFromInputStream (*in, -1);
            report.add ("Extracted " + name);
        }
        return juce::Result::ok();
    }
}
