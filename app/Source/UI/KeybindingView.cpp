#include "KeybindingView.h"
#include "IntegraLookAndFeel.h"

namespace integra
{
    KeybindingView::KeybindingView (AppContext& c) : context (c)
    {
        context.model.addChangeListener (this);
        changeListenerCallback (nullptr);
    }

    void KeybindingView::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        // the current scene changes without a model rebuild
        context.watcher.removeAll (this);
        if (auto player = context.model.get().player.path; player.isNotEmpty())
            context.watcher.watch (player + ".scene", this, [this] (const juce::String&, CCommandSource) { repaint(); });
        repaint();
    }

    juce::String KeybindingView::getCurrentScene() const
    {
        auto player = context.model.get().player.path;
        return player.isEmpty() ? juce::String() : context.session.getValue (player + ".scene").toString();
    }

    KeybindingView::~KeybindingView()
    {
        context.watcher.removeAll (this);
        context.model.removeChangeListener (this);
    }

    juce::String KeybindingView::getSceneForKey (juce::juce_wchar key) const
    {
        const auto k = juce::String::charToString (key).toUpperCase();
        for (auto& s : context.model.get().scenes)
            if (s.userData.keybinding.toUpperCase() == k)
                return s.path;
        return {};
    }

    void KeybindingView::resized()
    {
        // KeybindingView.repositionKeys
        constexpr float edgeMargin = 10.0f, internalMargin = 3.0f, rowXoffset = 0.334f;
        const int numRows = (int) std::size (keyRows);

        float longestRow = 0.0f, xOffset = 0.0f;
        for (auto* row : keyRows)
        {
            longestRow = juce::jmax (longestRow, (float) std::strlen (row) + xOffset);
            xOffset += rowXoffset;
        }

        const float maxKeyWidth = ((float) getWidth() - edgeMargin * 2.0f) / (longestRow + 2.0f) - internalMargin;
        const float maxKeyHeight = ((float) getHeight() - edgeMargin * 2.0f) / (float) numRows - internalMargin;
        const float keySize = juce::jmax (4.0f, juce::jmin (maxKeyWidth, maxKeyHeight));
        const float keySpace = keySize + internalMargin;
        const float keyboardWidth = longestRow * keySpace - internalMargin;
        const float keyboardHeight = (float) numRows * keySpace - internalMargin;

        float rowStartX = ((float) getWidth() - keyboardWidth) / 2.0f;
        float rowY = ((float) getHeight() - keyboardHeight) / 2.0f;
        const float keyboardStartX = rowStartX - keySpace;
        const float keyboardEndX = rowStartX + keyboardWidth + keySpace;

        keys.clear();
        fillers.clear();
        for (auto* row : keyRows)
        {
            const int length = (int) std::strlen (row);
            for (int i = 0; i < length; ++i)
                keys[(juce::juce_wchar) row[i]] = { rowStartX + (float) i * keySpace, rowY, keySize, keySize };

            // the keys around the scene keys, to make it look like a keyboard
            const float rowEnd = rowStartX + (float) length * keySpace - internalMargin;
            if (rowStartX - internalMargin - keyboardStartX > 2.0f)
                fillers.push_back ({ keyboardStartX, rowY, rowStartX - internalMargin - keyboardStartX, keySize });
            if (keyboardEndX - rowEnd - internalMargin > 2.0f)
                fillers.push_back ({ rowEnd + internalMargin, rowY, keyboardEndX - rowEnd - internalMargin, keySize });

            rowStartX += rowXoffset * keySpace;
            rowY += keySize + internalMargin;
        }
    }

    juce::Rectangle<float> KeybindingView::getKeyBounds (juce::juce_wchar key) const
    {
        auto it = keys.find ((juce::juce_wchar) juce::CharacterFunctions::toUpperCase (key));
        return it != keys.end() ? it->second : juce::Rectangle<float>();
    }

    juce::juce_wchar KeybindingView::keyAt (juce::Point<float> p, bool inner) const
    {
        for (auto& [key, r] : keys)
            if ((inner ? r.reduced (r.getWidth() * 0.1f, r.getHeight() * 0.1f) : r).contains (p))   // shouldAcceptDrag's hitDeflation
                return key;
        return 0;
    }

    bool KeybindingView::canDropOn (juce::juce_wchar key) const
    {
        return key != 0 && key != draggedKey && getSceneForKey (key).isEmpty();
    }

    void KeybindingView::paint (juce::Graphics& g)
    {
        auto& t = Theme::get();
        g.fillAll (t.panelAlt);

        auto& p = context.model.get();
        const auto current = getCurrentScene();

        for (auto& f : fillers)
        {
            g.setColour (t.controlBackground.withAlpha (0.5f));
            g.fillRoundedRectangle (f, 4.0f);
        }

        for (auto& [key, r] : keys)
        {
            auto scenePath = getSceneForKey (key);
            juce::String sceneName;
            if (auto* s = p.findScene (scenePath))
                sceneName = s->name;

            // while dragging, the scene shows on the key it would move to
            if (dragging && key == draggedKey)
                sceneName = {};
            if (dragging && key == dragOverKey)
                if (auto* s = p.findScene (getSceneForKey (draggedKey)))
                    sceneName = s->name;

            const bool highlighted = sceneName.isNotEmpty()
                                     && lastElement (dragging && key == dragOverKey ? getSceneForKey (draggedKey) : scenePath) == current;

            g.setColour (sceneName.isNotEmpty() ? t.panel : t.controlBackground);
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (highlighted ? t.accent : t.outline);
            g.drawRoundedRectangle (r.reduced (highlighted ? 1.5f : 0.5f), 4.0f, highlighted ? 3.0f : 1.0f);

            g.setColour (t.textDim);
            g.setFont (juce::FontOptions (juce::jlimit (9.0f, 14.0f, r.getHeight() * 0.22f), juce::Font::bold));
            g.drawText (juce::String::charToString (key), r.reduced (5.0f, 3.0f), juce::Justification::topLeft);

            if (sceneName.isNotEmpty())
            {
                g.setColour (t.text);
                g.setFont (juce::FontOptions (juce::jlimit (9.0f, 13.0f, r.getHeight() * 0.2f)));
                g.drawFittedText (sceneName, r.reduced (4.0f).withTrimmedTop (r.getHeight() * 0.25f).toNearestInt(),
                                  juce::Justification::centred, 3, 0.8f);
            }
        }
    }

    void KeybindingView::mouseDown (const juce::MouseEvent& e)
    {
        // KeybindingView.onMouseDownKeyIcon: select the key's scene, maybe start a drag
        dragging = false;
        dragOverKey = 0;
        draggedKey = keyAt (e.position);
        if (draggedKey == 0)
            return;

        auto scene = getSceneForKey (draggedKey);
        if (scene.isEmpty())
        {
            draggedKey = 0;
            return;
        }
        context.controller.activateScene (scene);
        repaint();
    }

    void KeybindingView::mouseDrag (const juce::MouseEvent& e)
    {
        if (draggedKey == 0)
            return;
        if (! dragging && e.getDistanceFromDragStart() < 3)   // _dragStartDistance
            return;

        dragging = true;
        auto over = keyAt (e.position, true);
        dragOverKey = canDropOn (over) ? over : 0;
        repaint();
    }

    void KeybindingView::mouseUp (const juce::MouseEvent&)
    {
        if (dragging && dragOverKey != 0)
            context.controller.setSceneKeybinding (getSceneForKey (draggedKey), juce::String::charToString (dragOverKey));
        dragging = false;
        draggedKey = dragOverKey = 0;
        repaint();
    }

    juce::String KeybindingView::getTooltip()
    {
        // the info of the scene under the mouse, or how the view works
        auto key = keyAt (getMouseXYRelative().toFloat());
        if (auto* s = context.model.get().findScene (getSceneForKey (key)))
            return s->name + (s->info.isNotEmpty() ? "\n\n" + s->info : juce::String());
        return "Scene Shortcuts: press a key (or click it) to select its scene; drag a scene's key onto a free key to change the shortcut. "
               "Keys are assigned in the scene properties.";
    }
}
