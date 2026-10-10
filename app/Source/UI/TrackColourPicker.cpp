#include "TrackColourPicker.h"

namespace integra
{
    namespace
    {
        TrackColourPicker* currentPicker = nullptr;
        constexpr float radius = TrackColourPicker::diameter / 2.0f;
        constexpr float rimWidth = 3.0f;
    }

    TrackColourPicker* TrackColourPicker::getCurrent() noexcept   { return currentPicker; }

    TrackColourPicker* TrackColourPicker::show (AppContext& context, const juce::String& trackPath, juce::Component& anchor, juce::Point<int> position)
    {
        if (currentPicker != nullptr)
            currentPicker->close();
        auto* top = anchor.getTopLevelComponent();
        if (top == nullptr || context.model.get().findTrack (trackPath) == nullptr)
            return nullptr;

        auto* picker = new TrackColourPicker (context, trackPath);
        auto at = top->getLocalPoint (&anchor, position);
        at.x = juce::jlimit (0, juce::jmax (0, top->getWidth() - diameter), at.x);
        at.y = juce::jlimit (0, juce::jmax (0, top->getHeight() - diameter), at.y);
        picker->setBounds (at.x, at.y, diameter, diameter);
        top->addAndMakeVisible (picker);
        picker->toFront (false);
        currentPicker = picker;
        return picker;
    }

    TrackColourPicker::TrackColourPicker (AppContext& c, const juce::String& path)
        : context (c), trackPath (path)
    {
        if (auto* track = context.model.get().findTrack (trackPath))
            original = track->userData.getColour();

        // TrackColorPicker.createBitmap, and the crosshair on the nearest colour
        wheel = juce::Image (juce::Image::ARGB, diameter, diameter, true);
        int bestDistance = std::numeric_limits<int>::max();
        for (int y = 0; y < diameter; ++y)
            for (int x = 0; x < diameter; ++x)
            {
                const float dx = x + 0.5f - radius, dy = y + 0.5f - radius;
                const float distance = std::sqrt (dx * dx + dy * dy);
                if (distance > radius - rimWidth)
                    continue;
                auto colour = wheelColour (std::atan2 (dy, dx), distance);
                wheel.setPixelAt (x, y, colour);
                const int difference = std::abs (colour.getRed() - original.getRed()) + std::abs (colour.getGreen() - original.getGreen())
                                     + std::abs (colour.getBlue() - original.getBlue());
                if (difference < bestDistance)
                {
                    bestDistance = difference;
                    crosshair = { x, y };
                }
            }

        juce::Desktop::getInstance().addGlobalMouseListener (&outsideClicks);
    }

    TrackColourPicker::~TrackColourPicker()
    {
        juce::Desktop::getInstance().removeGlobalMouseListener (&outsideClicks);
        if (currentPicker == this)
            currentPicker = nullptr;
    }

    juce::Colour TrackColourPicker::wheelColour (float bearing, float distance)
    {
        // hue from the bearing; saturation grows towards the rim; lighter at the top right,
        // darker at the bottom left
        const float saturation = juce::jlimit (0.0f, 1.0f, distance / (radius - rimWidth)) * 0.5f + 0.5f;
        const float brightness = 0.5f + std::sin (bearing + juce::MathConstants<float>::pi / 8.0f) * 0.15f;
        float rgb[3];
        for (int i = 0; i < 3; ++i)
        {
            float c = std::cos (bearing + i * juce::MathConstants<float>::twoPi / 3.0f) * 0.5f + 0.5f;
            c = 0.5f + (c - 0.5f) * saturation;
            c = brightness > 0.5f ? c + (1.0f - c) * (brightness - 0.5f) * 2.0f : c * brightness * 2.0f;
            rgb[i] = juce::jlimit (0.0f, 1.0f, c);
        }
        return juce::Colour::fromFloatRGBA (rgb[0], rgb[1], rgb[2], 1.0f);
    }

    juce::Point<float> TrackColourPicker::clampToWheel (juce::Point<float> p) const
    {
        const juce::Point<float> centre (radius, radius);
        auto offset = p - centre;
        const float limit = radius - rimWidth - 1.0f;
        if (offset.getDistanceFromOrigin() > limit)
            offset = offset * (limit / offset.getDistanceFromOrigin());
        return centre + offset;
    }

    juce::Colour TrackColourPicker::colourAt (juce::Point<float> p) const
    {
        p = clampToWheel (p);
        return wheelColour (std::atan2 (p.y - radius, p.x - radius), p.getDistanceFrom ({ radius, radius }));
    }

    void TrackColourPicker::paint (juce::Graphics& g)
    {
        g.drawImageAt (wheel, 0, 0);
        g.setColour (juce::Colour (0xff808080));
        g.drawEllipse (getLocalBounds().toFloat().reduced (rimWidth / 2.0f), rimWidth);

        // crosshair: 2 px, black at alpha 0.2, gap 2, length 12
        g.setColour (juce::Colours::black.withAlpha (0.2f));
        const float x = (float) crosshair.x, y = (float) crosshair.y;
        for (auto sign : { -1.0f, 1.0f })
        {
            g.drawLine (x + sign * 2.0f, y, x + sign * 12.0f, y, 2.0f);
            g.drawLine (x, y + sign * 2.0f, x, y + sign * 12.0f, 2.0f);
        }
    }

    bool TrackColourPicker::hitTest (int x, int y)
    {
        return juce::Point<float> ((float) x, (float) y).getDistanceFrom ({ radius, radius }) <= radius;
    }

    void TrackColourPicker::choose (juce::Point<float> p)
    {
        p = clampToWheel (p);
        crosshair = p.toInt();
        context.controller.previewTrackColour (trackPath, colourAt (p));
        repaint();
    }

    void TrackColourPicker::mouseDown (const juce::MouseEvent& e)
    {
        choosing = true;
        choose (e.position);
    }

    void TrackColourPicker::mouseDrag (const juce::MouseEvent& e)
    {
        if (choosing)
            choose (e.position);
    }

    void TrackColourPicker::mouseUp (const juce::MouseEvent& e)
    {
        if (! choosing)
            return;
        choosing = false;

        // one undo step: back to the original colour without one, then set the chosen one
        const auto chosen = colourAt (e.position);
        context.controller.previewTrackColour (trackPath, original);
        context.controller.setTrackColour (trackPath, chosen);
        close();
    }

    void TrackColourPicker::globalMouseDown (const juce::MouseEvent& e)
    {
        auto* target = e.eventComponent;
        if (target != this && ! isParentOf (target))
            close();
    }

    void TrackColourPicker::close()
    {
        if (closing)
            return;
        closing = true;
        setVisible (false);
        if (currentPicker == this)
            currentPicker = nullptr;
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<TrackColourPicker> (this)] { delete safe.getComponent(); });
    }
}
