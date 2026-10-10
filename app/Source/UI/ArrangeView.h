#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <map>
#include <optional>
#include <vector>
#include "Core/AppContext.h"
#include "Core/EndpointInfo.h"
#include "BlockLibrary.h"

namespace integra
{
    /** Shared horizontal mapping between ticks and pixels for the ruler and the lanes. */
    struct TimelineMapping
    {
        double scrollTicks = 0.0;       // tick at the left edge of the lanes
        double pixelsPerTick = 0.4;     // Flex TimelineState.zoom
        int laneX = 0;                  // x of tick-space origin in the lanes' coordinates
        std::vector<int> snapLines;     // ticks drawn as snap lines during a drag (Flex SetTimeDomainSnaplines)

        float tickToX (double tick) const   { return (float) (laneX + (tick - scrollTicks) * pixelsPerTick); }
        double xToTick (float x) const      { return scrollTicks + (x - laneX) / pixelsPerTick; }
    };

    //==============================================================================
    /** Time ruler, scene bar and playhead (Flex: Timeline.as, SceneBar.as). */
    class TimelineRuler  : public juce::Component,
                           private juce::Timer
    {
    public:
        TimelineRuler (AppContext&, TimelineMapping&);

        void paint (juce::Graphics&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;

        int playPosition = 0;
        std::function<void (const juce::String& scenePath)> onSceneSelected;
        /** Scroll the timeline by this many ticks (auto-scroll while dragging at the edges). */
        std::function<void (double deltaTicks)> onAutoScroll;
        /** Called while a drag changes the snap lines, so the lanes can redraw them. */
        std::function<void()> onSnapLinesChanged;

        static constexpr int sceneBarHeight = 22;
        static constexpr float sceneResizeAreaWidth = 10.0f;   // SceneBar._resizeAreaWidth

    private:
        enum class Zone { none, move, resizeStart, resizeEnd };
        enum class Drag { none, playhead, newScene, moveScene, resizeSceneStart, resizeSceneEnd } drag = Drag::none;

        juce::Rectangle<float> sceneBounds (int start, int end) const;
        const model::Scene* sceneAt (juce::Point<float>, Zone&) const;
        juce::Range<int> displayedRange (const model::Scene&) const;
        void updateDrag (float mouseX);
        void finishDrag();
        void reschedule (int zoneStart, int zoneEnd, bool movingRight);
        void timerCallback() override;

        struct Snap { int value; bool snapped; int distance; };
        Snap snap (int ticks) const;

        AppContext& context;
        TimelineMapping& mapping;

        juce::String dragScene;
        float dragPixelOffset = 0.0f, dragClickX = 0.0f, lastMouseX = 0.0f;
        juce::Range<int> newScene;                          // scene being created
        std::map<juce::String, juce::Range<int>> repositioned;  // dragged and nudged scenes
    };

    //==============================================================================
    /** Track headers and block lanes (Flex: ArrangeView.as, ArrangeViewTrack.as, BlockView.as, EnvelopeView.as). */
    class TrackLanes  : public juce::Component,
                        public juce::DragAndDropTarget,
                        public juce::FileDragAndDropTarget
    {
    public:
        TrackLanes (AppContext&, TimelineMapping&);

        // Block Library drags and .integra files dropped on a track (ArrangeViewTrack.onDragDrop)
        bool isInterestedInDragSource (const SourceDetails&) override;
        void itemDragEnter (const SourceDetails& d) override        { updateDropOutline (d.localPosition); }
        void itemDragMove (const SourceDetails& d) override         { updateDropOutline (d.localPosition); }
        void itemDragExit (const SourceDetails&) override           { updateDropOutline (std::nullopt); }
        void itemDropped (const SourceDetails&) override;
        bool isInterestedInFileDrag (const juce::StringArray&) override;
        void fileDragEnter (const juce::StringArray&, int x, int y) override   { updateDropOutline (juce::Point<int> (x, y)); }
        void fileDragMove (const juce::StringArray&, int x, int y) override    { updateDropOutline (juce::Point<int> (x, y)); }
        void fileDragExit (const juce::StringArray&) override                  { updateDropOutline (std::nullopt); }
        void filesDropped (const juce::StringArray&, int x, int y) override;

        /** Imports a block file centred on a point of a track, as a drop does. */
        juce::Result importBlockAt (const juce::File&, juce::Point<int>);
        juce::Rectangle<float> getDropOutline() const noexcept      { return dropOutline; }

        void paint (juce::Graphics&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        bool keyPressed (const juce::KeyPress&) override;

        int getTotalHeight() const;
        /** Where a control point of a block envelope is drawn (for tests). */
        std::optional<juce::Point<float>> getControlPointPosition (const juce::String& controlPointPath) const;
        void deleteSelection();
        const TimelineMapping& getMapping() const noexcept  { return mapping; }

        int playPosition = 0;
        std::function<void (const juce::String& blockPath)> onOpenBlock;
        std::function<void()> onSelectionChanged;
        std::function<void()> onSnapLinesChanged;

        static constexpr int headerWidth = 170;
        static constexpr int addTrackHeight = 34;

        /** Envelope being edited inside the selected block ("" = none). */
        juce::String editedEnvelope;

        /** Curve mode of the selected block (Flex: BlockView curvature mode button, stored in
            the block's userData): dragging an envelope segment bends it, double-clicking a
            segment makes it straight again. */
        bool isCurveMode() const;
        void toggleCurveMode();

        /** Where the curve-mode button of the selected block is drawn (empty if none). */
        juce::Rectangle<float> getCurveButtonBounds() const;

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

        enum class Drag { none, moveBlock, resizeStart, resizeEnd, controlPoint, curve } drag = Drag::none;
        juce::String dragBlock, dragPoint;
        int dragStartTick = 0;
        float dragPixelOffset = 0.0f;
        juce::String dragTargetTrack;
        std::map<juce::String, juce::Range<int>> draggedBlocks;   // dragged and nudged blocks
        juce::String dragCurvePoint;                              // point whose following segment is being bent
        float dragCurvature = 0.0f;
        const model::ControlPoint* segmentStartAt (const model::Envelope&, int tick) const;
        float curvatureFromMouse (juce::Point<float>) const;
        juce::Range<int> dragPointLimits;                         // allowed ticks for the dragged control point
        int dragPointTick = 0;
        float dragPointValue = 0.0f;

        static constexpr float blockResizeAreaWidth = 10.0f;   // BlockView resize areas
        static constexpr int minimumBlockLength = 10;          // Block._minimumBlockLength (ticks)

        void updateDropOutline (std::optional<juce::Point<int>>);
        juce::Rectangle<float> dropOutline;

        juce::Range<int> displayedRange (const model::Block&) const;
        void updateBlockDrag (const juce::MouseEvent&);
        void rescheduleBlocks (const model::Track&, bool movingRight);
        struct Snap { int value; bool snapped; int distance; };
        Snap snapBlock (int ticks) const;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackLanes)
    };

    //==============================================================================
    class ArrangeView  : public juce::Component,
                         public juce::DragAndDropContainer,
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
        BlockLibrary& getBlockLibrary() noexcept        { return blockLibrary; }

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
        BlockLibrary blockLibrary;

        /** Drags the Block Library's left edge (Flex ViewHolder changeWidthFromLeft). */
        struct WidthHandle  : public juce::Component
        {
            explicit WidthHandle (ArrangeView& v) : view (v)    { setMouseCursor (juce::MouseCursor::LeftRightResizeCursor); }
            void mouseDown (const juce::MouseEvent&) override   { startWidth = view.blockLibraryWidth; }
            void mouseDrag (const juce::MouseEvent&) override;
            void mouseUp (const juce::MouseEvent&) override;
            ArrangeView& view;
            int startWidth = 0;
        } widthHandle { *this };
        int blockLibraryWidth = BlockLibrary::defaultWidth;
        bool timelineStateDirty = false;
        juce::uint32 lastTimelineChange = 0;
    };
}
