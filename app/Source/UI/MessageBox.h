#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace integra
{
    /** A message box that never blocks the caller.

        The app is built with JUCE_MODAL_LOOPS_PERMITTED (the GUI test pumps the message
        loop), and with that JUCE runs AlertWindow::showMessageBoxAsync *synchronously*
        when no callback is given: a nested modal loop inside the caller. Called from a
        timer callback that nested loop re-fired the timer (an "Open" that failed showed
        its error again and again). Passing a callback keeps the box truly asynchronous. */
    inline void showMessage (juce::MessageBoxIconType icon, const juce::String& title, const juce::String& message)
    {
        juce::AlertWindow::showMessageBoxAsync (icon, title, message, {}, nullptr,
                                                juce::ModalCallbackFunction::create ([] (int) {}));
    }
}
