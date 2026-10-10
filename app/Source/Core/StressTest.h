#pragma once

#include "ProjectController.h"
#include "AudioHost.h"

namespace integra
{
    /**
        Robustness checks, run with `IntegraLive --stress-test [seconds] [project files...]`,
        meant to be run under AddressSanitizer/UndefinedBehaviorSanitizer (INTEGRA_SANITIZE)
        as well as in a normal build:

        - opens, plays, saves and reopens every demo block and each project given, and
          imports each into a new project as a block;
        - adds every user module to a block, drives each of its control endpoints to its
          minimum, maximum, default and beyond its range (and text endpoints to empty,
          long and odd strings), pulls audio, and removes it again;
        - makes a seeded series of random edits (tracks, blocks, modules, connections,
          scenes, envelopes, removals, transport moves) mixed with undo and redo,
          checking the model after each, then undoes all of it;
        - plays the demo project for `seconds` of audio (default 60) through the
          AudioHost re-blocking path, moving the playhead and switching scenes.

        Returns the number of failed checks. A crash or sanitizer report is a failure too.
    */
    int runStressTest (IntegraSession&, ProjectModel&, ProjectController&, AudioHost&,
                       double playbackSeconds, const juce::Array<juce::File>& extraProjects, juce::String& log);

    /** `IntegraLive --fingerprint-modules [module]`: per module, "fp <name> <rms> <peak>" of its
        output for a fixed input, then "fv <name>.<endpoint> <value>" for every value it
        reported back; for comparing two builds (e.g. two Pd versions). Only `only` if given. */
    juce::String fingerprintModules (IntegraSession&, ProjectModel&, ProjectController&, AudioHost&, const juce::String& only = {});
}
