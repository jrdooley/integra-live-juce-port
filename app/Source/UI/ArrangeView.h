#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "Core/AppContext.h"
#include "Core/EndpointInfo.h"

namespace integra
{
    /** Shared horizontal mapping between ticks and pixels for the ruler and the lanes. */
    struct TimelineMapping
    {
        double scrollTicks = 0.0;       // tick at the left edge of the lanes
        double pixelsPerTick = 0.4;     // Flex TimelineState.zoom
        int laneX = 0;                  // x of tick-space origin in the lanes' coordinates

        float tickToX (double tick) const   { return (float) (laneX + (tick - scrollTicks) * pixelsPerTick); }
        double xToTick (float x) const      { return scrollTicks + (x - laneX) / pixelsPerTick; }
    };

    //==============================================================================
    /** Time ruler, scene bar and playhead (Flex: Timeline.as, SceneBar.as). */
    class TimelineRuler  : public juce::Component
    {
    public:
        TimelineRuler (AppContext&, TimelineMapping&);

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;

        int playPosition = 0;
        std::function<void (const juce::String& scenePath)> onSceneSelected;

        static constexpr int sceneBarHeight = 22;

    private:
        juce::Rectangle<float> sceneBounds (const model::Scene&) const;
        AppContext& context;
        TimelineMapping& mapping;

        enum class Drag { none, playhead, newScene, moveScene, resizeScene } drag = Drag::none;
        juce::String dragScene;
        int dragStartTick = 0, dragOriginalStart = 0, dragOriginalLength = 0;
        juce::Range<int> pendingScene;
    };

    //==============================================================================
    /** Track headers and block lanes (Flex: ArrangeView.as, ArrangeViewTrack.as, BlockView.as, EnvelopeView.as). */
    class TrackLanes  : public juce::Component
    {
    public:
        TrackLanes (AppContext&, TimelineMapping&);

        void paint (juce::Graphics&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        bool keyPressed (const juce::KeyPress&) override;

        int getTotalHeight() const;
        void deleteSelection();
        const TimelineMapping& getMapping() const noexcept  { return mapping; }

        int playPosition = 0;
        std::function<void (const juce::String& blockPath)> onOpenBlock;
        std::function<void()> onSelectionChanged;

        static constexpr int headerWidth = 170;
        static constexpr int addTrackHeight = 34;

        /** Envelope being edited inside the selected block ("" = none). */
        juce::String editedEnvelope;

    private:
        struct TrackRow
        {
            const model::Track* track;
            juce::Rectangle<int> bounds;
        };

        std::vector<TrackRow> rows() const;
        const model::Track* trackAt (int y) const;
        const model::Block* blockAt (juce::Point<int>, const model::Track** trackOut = nullptr) const;
        juce::Rectangle<float> blockBounds (const model::Block&, juce::Rectangle<int> row) const;
        void showBlockMenu (const model::Block&);
        void showTrackMenu (const model::Track&);
        void renameNode (const juce::String& path, const juce::String& title);
        void paintEnvelopes (juce::Graphics&, const model::Block&, juce::Rectangle<float>);

        // envelope editing helpers
        const model::Envelope* currentEnvelope (const model::Block*&) const;
        juce::Point<float> pointPosition (const model::Envelope&, const model::ControlPoint&, const model::Block&, juce::Rectangle<float>, const EndpointInfo&) const;

        AppContext& context;
        TimelineMapping& mapping;

        enum class Drag { none, moveBlock, resizeStart, resizeEnd, controlPoint } drag = Drag::none;
        juce::String dragBlock, dragPoint;
        int dragStartTick = 0, originalStart = 0, originalEnd = 0;
        juce::Rectangle<float> dragPreview;
        juce::Range<int> dragStartTickPreview;
        juce::String dragTargetTrack;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackLanes)
    };

    //==============================================================================
    class ArrangeView  : public juce::Component,
                         private juce::ChangeListener,
                         private juce::Timer,
                         private juce::ScrollBar::Listener
    {
    public:
        explicit ArrangeView (AppContext&);
        ~ArrangeView() override;

        void resized() override;
        void paint (juce::Graphics&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

        std::function<void (const juce::String& blockPath)> onOpenBlock;
        std::function<void()> onSelectionChanged;

        void zoom (double factor, float aroundX);
        void deleteSelection()                          { lanes.deleteSelection(); }
        TrackLanes& getLanes() noexcept                 { return lanes; }
        TimelineRuler& getRuler() noexcept              { return ruler; }
        juce::Viewport& getViewport() noexcept          { return viewport; }

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void timerCallback() override;
        void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
        void updateScrollBar (bool userChanged = false);
        void storeTimelineState();

        AppContext& context;
        TimelineMapping mapping;
        TimelineRuler ruler;
        TrackLanes lanes;
        juce::Viewport viewport;
        juce::ScrollBar horizontalScroll { false };
        juce::TextButton zoomIn { "+" }, zoomOut { "-" };
        bool timelineStateDirty = false;
        juce::uint32 lastTimelineChange = 0;
    };
}
