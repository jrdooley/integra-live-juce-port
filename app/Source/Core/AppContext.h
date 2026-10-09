#pragma once

#include "IntegraSession.h"
#include "AudioHost.h"
#include "ProjectModel.h"
#include "ProjectController.h"
#include <juce_data_structures/juce_data_structures.h>

namespace integra
{
    /** Everything with application lifetime, in construction order. */
    struct AppContext
    {
        juce::ApplicationProperties properties;
        IntegraSession session;
        AudioHost audioHost { session };
        ProjectModel model { session };
        ProjectController controller { session, model };
    };
}
