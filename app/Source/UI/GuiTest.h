#pragma once

#include "MainComponent.h"

namespace integra
{
    /**
        Drives the real GUI components with synthesized mouse/keyboard events and checks
        the resulting project state. Run with `IntegraLive --gui-test` (needs a display;
        Xvfb is fine). Returns the number of failed checks.
    */
    int runGuiTest (MainComponent&, AppContext&, juce::String& log);
}
