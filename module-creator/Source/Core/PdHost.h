#pragma once

#include "Document.h"

namespace modulecreator
{
    /**
        Runs Pd on a host patch around the module's main patch, so the implementation can be
        edited and tried out (HostStarter). Only one Pd runs at a time.
    */
    class PdHost
    {
    public:
        ~PdHost()                                   { kill(); }

        juce::Result start (const juce::File& pdExecutable, const Document&);
        bool isRunning();
        void kill();

    private:
        std::unique_ptr<juce::ChildProcess> process;
        juce::File directory;
    };
}
