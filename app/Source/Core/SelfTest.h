#pragma once

#include "ProjectController.h"
#include "AudioHost.h"

namespace integra
{
    /**
        Headless end-to-end checks of the app core against the real libIntegra + libpd,
        run with `IntegraLive --self-test`. Exercises the same ProjectController
        operations the GUI uses: new project, add track/block/module, audio
        connection, routing, envelope, scene, undo/redo, save and reload, opening the
        demo blocks, and pulling audio through the AudioHost re-blocking path.

        Returns the number of failed checks.
    */
    int runSelfTest (IntegraSession&, ProjectModel&, ProjectController&, AudioHost&, juce::String& log);
}
