#include "PdHost.h"

namespace modulecreator
{
    juce::Result PdHost::start (const juce::File& pdExecutable, const Document& document)
    {
        if (isRunning())
            return juce::Result::fail ("You are already editing this implementation.\n\nYou must close PD before you can reopen it.");
        if (! pdExecutable.existsAsFile())
            return juce::Result::fail ("Pd can't be found.  Install Pure Data (https://puredata.info) or choose the Pd application on the Implementation tab.");

        kill();
        directory = juce::File::createTempFile ("module-creator-host");
        juce::File hostPatch;
        auto r = HostPatch::create (directory, document.getMainPatch(), document.definition, document.config, hostPatch);
        if (r.failed())
        {
            kill();
            return r;
        }

        juce::StringArray args;
        args.add (pdExecutable.getFullPathName());
        args.addArray (document.config.hostArguments);
        args.add (hostPatch.getFullPathName());

        process = std::make_unique<juce::ChildProcess>();
        // no pipes: Pd's console output would fill them and stall it
        if (! process->start (args, 0))
        {
            kill();
            return juce::Result::fail ("Can't start " + pdExecutable.getFullPathName());
        }
        return juce::Result::ok();
    }

    bool PdHost::isRunning()
    {
        if (process != nullptr && ! process->isRunning())
            kill();     // Pd was closed: tidy up its host patch (HostStarter.onHostExit)
        return process != nullptr;
    }

    void PdHost::kill()
    {
        if (process != nullptr && process->isRunning())
            process->kill();
        process.reset();
        if (directory != juce::File())
            directory.deleteRecursively();
        directory = juce::File();
    }
}
