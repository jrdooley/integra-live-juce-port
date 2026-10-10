#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace integra
{
    /**
        In-place rename (Flex: the name fields of SceneBar, BlockView, ModuleGraphElement and
        the ViewHolder title): a text field over the name with the text selected, accepting
        only the characters Integra names allow (IntegraDataObject.legalObjectNameCharacterSet).
        Enter or clicking elsewhere commits, Escape cancels; an invalid name is dropped and
        the old name stays, as in Flex.

        `holder` is a member of `parent`, which keeps the editor alive while it is shown.
    */
    inline void startInlineRename (std::unique_ptr<juce::TextEditor>& holder, juce::Component& parent, juce::Rectangle<int> bounds,
                                   const juce::String& currentName, std::function<void (const juce::String&)> commit)
    {
        holder = std::make_unique<juce::TextEditor>();
        auto& editor = *holder;
        editor.setInputRestrictions (64, "0123456789_abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");
        editor.setText (currentName, false);
        editor.setFont (juce::FontOptions ((float) juce::jlimit (11, 15, bounds.getHeight() - 6)));
        editor.setIndents (3, juce::jmax (0, (bounds.getHeight() - 16) / 2));

        juce::Component::SafePointer<juce::Component> safeParent (&parent);
        auto* holderPtr = &holder;
        auto finish = [safeParent, holderPtr, commit] (bool shouldCommit)
        {
            juce::MessageManager::callAsync ([safeParent, holderPtr, commit, shouldCommit]
            {
                if (safeParent == nullptr || *holderPtr == nullptr)
                    return;
                auto finished = std::move (*holderPtr);   // gone before the model changes
                const auto text = finished->getText().trim();
                finished.reset();
                if (shouldCommit && commit != nullptr)
                    commit (text);
                safeParent->repaint();
            });
        };
        editor.onReturnKey = [finish] { finish (true); };
        editor.onEscapeKey = [finish] { finish (false); };
        editor.onFocusLost = [finish] { finish (true); };

        parent.addAndMakeVisible (editor);
        editor.setBounds (bounds);
        editor.selectAll();
        editor.grabKeyboardFocus();
    }
}
