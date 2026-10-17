#pragma once

#include "MainComponent.h"

namespace modulecreator
{
    /** Drives the real window with synthesized edits, clicks and drags (--gui-test); returns the failures. */
    int runGuiTest (MainComponent&, juce::String& log);
}
