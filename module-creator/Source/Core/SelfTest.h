#pragma once

#include "CreatorConfig.h"

namespace modulecreator
{
    /** Headless test of the Module Creator core (--self-test): every shipped module loads,
        validates and saves unchanged; a module built from scratch validates, saves and
        reopens; templates, implementation scanning, the Pd host patch and bundles work.
        Returns the number of failures. */
    int runSelfTest (const CreatorConfig&, const juce::File& modulesDirectory, juce::String& log);
}
