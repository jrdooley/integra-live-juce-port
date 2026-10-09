#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "Core/AppContext.h"
namespace integra
{
    class MainComponent : public juce::Component
    {
    public:
        explicit MainComponent (AppContext& c) : context (c) { setSize (1280, 800); }
        void requestQuit (std::function<void()> f) { f(); }
        void openFile (const juce::File& f) { context.controller.openProject (f); }
        void showView (const juce::String&) {}
    private:
        AppContext& context;
    };
}
