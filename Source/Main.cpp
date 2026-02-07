#include <JuceHeader.h>
#include <cmath>
#include <algorithm>
#include <optional>
#include <unordered_map>
#include "TimelineModel.h"
#include "PythonRenderer.h"

static double getLoopedLocalTime(double timeSeconds, const Timeline& timeline)
{
    if (timeline.durationSeconds <= 0.0)
        return 0.0;

    if (timeline.repeatMarkers.size() == 1)
    {
        double loopStart = timeline.repeatMarkers.getUnchecked(0);
        if (loopStart > 0.0 && timeSeconds >= loopStart)
        {
            double localTime = std::fmod(timeSeconds, loopStart);
            if (localTime < 0.0)
                localTime += loopStart;
            return localTime;
        }
        return juce::jlimit(0.0, timeline.durationSeconds, timeSeconds);
    }
    else if (timeline.repeatMarkers.size() >= 2)
    {
        double loopStart = timeline.repeatMarkers.getUnchecked(0);
        double loopEnd = timeline.repeatMarkers.getUnchecked(1);
        if (loopEnd > loopStart && timeSeconds >= loopStart)
        {
            double loopLen = loopEnd - loopStart;
            double localTime = loopStart + std::fmod(timeSeconds - loopStart, loopLen);
            if (localTime < loopStart)
                localTime += loopLen;
            return localTime;
        }
        return juce::jlimit(0.0, timeline.durationSeconds, timeSeconds);
    }

    double localTime = std::fmod(timeSeconds, timeline.durationSeconds);
    if (localTime < 0.0)
        localTime += timeline.durationSeconds;
    return localTime;
}

static bool isAlvaSynthFile(const juce::File& file)
{
    return file.getFileName().startsWithIgnoreCase("alva_");
}

static double getRenderDurationSecondsForMarker(const Timeline& timeline, const Marker& marker)
{
    double tempo = juce::jmax(1.0, timeline.tempoBpm);
    double beatUnit = juce::jmax(1, timeline.beatUnit);
    double beatsPerBar = juce::jmax(1, timeline.beatsPerBar);
    double beatSeconds = (60.0 / tempo) * (4.0 / beatUnit);
    double barSeconds = beatSeconds * beatsPerBar;
    if (marker.renderBars > 0)
        return juce::jmax(0.01, barSeconds * (double) marker.renderBars);
    if (isAlvaSynthFile(marker.pythonFile))
        return juce::jmax(0.01, barSeconds);
    return timeline.durationSeconds;
}

class TimelineView : public juce::Component
{
public:
    enum class GridMode
    {
        Seconds = 0,
        BBT = 1
    };

    enum class AutomationLane
    {
        None = 0,
        Volume,
        Pan
    };

    TimelineView(TimelineModel& modelIn,
                 PythonRenderer& rendererIn,
                 std::function<void()> onMarkersChangedIn,
                 std::function<void(int)> onTimelineSelectedIn,
                 std::function<double()> getPlayheadSecondsIn,
                 std::function<void(int, double)> onScissorsCutIn,
                 std::function<void()> onBeginEditIn,
                 std::function<void()> onSelectionChangedIn)
        : model(modelIn),
          renderer(rendererIn),
          onMarkersChanged(std::move(onMarkersChangedIn)),
          onTimelineSelected(std::move(onTimelineSelectedIn)),
          getPlayheadSeconds(std::move(getPlayheadSecondsIn)),
          onScissorsCut(std::move(onScissorsCutIn)),
          onBeginEdit(std::move(onBeginEditIn)),
          onSelectionChanged(std::move(onSelectionChangedIn))
    {
    }

    void paint(juce::Graphics& g) override
    {
        juce::Colour bg(0xff0f1418);
        g.fillAll(bg);

        auto area = getLocalBounds();
        int timelineCount = model.getTimelineCount();
        if (timelineCount == 0)
            return;

        int y = area.getY();
        for (int i = 0; i < timelineCount; ++i)
        {
            auto& timeline = model.getTimeline(i);
            int rowHeight = getRowHeightForTimeline(timeline);
            auto row = juce::Rectangle<int>(area.getX(), y, area.getWidth(), rowHeight);
            y += rowHeight;
            auto markerArea = getMarkerArea(row, timeline);

            g.setColour(juce::Colour(0xff1b242b));
            g.fillRect(row.reduced(3, 3));

            drawHeader(g, row, timeline, i);
            drawGrid(g, markerArea, timeline);

            if (i == selectedTimelineIndex)
            {
                g.setColour(juce::Colour(0xff6aa9ff));
                g.drawRect(row.reduced(3, 3), 2);
            }

            if (i == dragOverTimelineIndex)
            {
                g.setColour(juce::Colour(0xff3a6079).withAlpha(0.25f));
                g.fillRect(row.reduced(4, 4));
            }

            drawPlayhead(g, markerArea, timeline);

            drawAutomationLanes(g, row, timeline);

            std::unordered_map<const Marker*, juce::Colour> waveformColours;
            {
                struct RangeInfo
                {
                    Marker* marker = nullptr;
                    double start = 0.0;
                    double end = 0.0;
                    int z = 0;
                };
                std::vector<RangeInfo> ranges;
                ranges.reserve((size_t) timeline.markers.size());
                int zIndex = 0;
                for (auto* marker : timeline.markers)
                {
                    if (marker == nullptr || ! marker->renderedBuffer || marker->renderedSampleRate <= 0.0)
                    {
                        ++zIndex;
                        continue;
                    }
                    double duration = (double) marker->renderedBuffer->getNumSamples() / marker->renderedSampleRate;
                    if (duration <= 0.0)
                    {
                        ++zIndex;
                        continue;
                    }
                    ranges.push_back({ marker, marker->startTimeSeconds, marker->startTimeSeconds + duration, zIndex });
                    ++zIndex;
                }

                for (size_t iRange = 0; iRange < ranges.size(); ++iRange)
                {
                    const auto& r = ranges[iRange];
                    bool overlapsAny = false;
                    int overlapIndex = 0;
                    for (size_t j = 0; j < ranges.size(); ++j)
                    {
                        if (iRange == j)
                            continue;
                        const auto& other = ranges[j];
                        bool overlaps = r.start < other.end && r.end > other.start;
                        if (overlaps)
                        {
                            overlapsAny = true;
                            if (other.z < r.z)
                                ++overlapIndex;
                        }
                    }

                    auto colour = juce::Colours::red;
                    if (overlapsAny)
                        colour = (overlapIndex % 2 == 0) ? juce::Colours::red : juce::Colours::orange;
                    waveformColours[r.marker] = colour;
                }
            }

            g.setColour(juce::Colour(0xfff08a52));
            int markerCount = timeline.markers.size();
            const int markerYOffsetStep = 12;
            int markerYOffsetScaled = (int) std::round(markerYOffsetStep * juce::jmax(1.0, timeline.zoomY));
            for (int markerIndex = 0; markerIndex < markerCount; ++markerIndex)
            {
                auto* marker = timeline.markers.getUnchecked(markerIndex);
                if (marker == nullptr)
                    continue;
                if (! isTimeInView(marker->startTimeSeconds, timeline))
                    continue;
                auto x = timeToX(marker->startTimeSeconds, timeline, markerArea);
                int yOffset = (markerCount > 1) ? (markerIndex * markerYOffsetScaled) : 0;
                int y = markerArea.getCentreY() + yOffset;

                auto it = waveformColours.find(marker);
                auto colour = (it != waveformColours.end()) ? it->second : juce::Colours::red;
                drawMarkerWaveform(g, markerArea, timeline, *marker, colour, yOffset);
                juce::Path p;
                p.addTriangle((float) x, (float) y - 10.0f, (float) x - 6.0f, (float) y + 8.0f, (float) x + 6.0f, (float) y + 8.0f);
                if (isMarkerSelected(marker))
                {
                    g.setColour(juce::Colour(0xfff0cf4a));
                    g.fillPath(p);
                    g.setColour(juce::Colour(0xfff08a52));
                }
                else
                {
                    g.fillPath(p);
                }

                g.setColour(juce::Colour(0xffe8edf2));
                auto label = formatTime(marker->startTimeSeconds, timeline);
                int labelY = juce::jmax(markerArea.getY() + 2, y - 24);
                g.drawText(label, x - 30, labelY, 60, 16, juce::Justification::centred);
                g.setColour(juce::Colour(0xfff08a52));
            }

            drawRepeatMarkers(g, markerArea, timeline);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        int timelineIndex = getTimelineIndexForY(e.position.y);
        if (timelineIndex < 0)
            return;

        if (scissorsEnabled)
            setSelectedTimelinePreserveSelection(timelineIndex);
        else
            setSelectedTimeline(timelineIndex);

        auto row = getRowForTimelineIndex(timelineIndex);
        auto selectOnlyWidth = 24.0f;
        auto headerHeight = 24.0f;
        if (e.position.y <= (float) row.getY() + headerHeight)
        {
            auto& timeline = model.getTimeline(timelineIndex);
            auto toggle = getAutomationToggleRect(row);
            if (toggle.contains((int) e.position.x, (int) e.position.y))
            {
                timeline.automationExpanded = ! timeline.automationExpanded;
                repaint();
            }
            return;
        }
        if (e.position.x <= (float) row.getX() + selectOnlyWidth)
            return;

        if (e.mods.isRightButtonDown())
        {
            beginEditOnce();
            auto& timeline = model.getTimeline(timelineIndex);
            auto markerArea = getMarkerArea(row, timeline);
            double timeSeconds = xToTime(e.position.x, timeline, markerArea);
            if (snapEnabled)
                timeSeconds = snapTimeSeconds(timeSeconds, timeline);
            addRepeatMarker(timeline, timeSeconds);
            selectedRepeatMarkerIndex = -1;
            selectedRepeatMarkerTimelineIndex = -1;
            repaint();
            return;
        }

        if (scissorsEnabled)
        {
            auto& timeline = model.getTimeline(timelineIndex);
            auto markerArea = getMarkerArea(row, timeline);
            double timeSeconds = xToTime(e.position.x, timeline, markerArea);
            if (snapEnabled)
                timeSeconds = snapTimeSeconds(timeSeconds, timeline);
            if (onScissorsCut)
                onScissorsCut(timelineIndex, timeSeconds);
            return;
        }

        {
            int idx = findRepeatMarkerIndex(e.position, timelineIndex);
            if (idx >= 0)
            {
                beginEditOnce();
                selectedRepeatMarkerIndex = idx;
                selectedRepeatMarkerTimelineIndex = timelineIndex;
                draggingRepeatMarker = true;
                return;
            }
        }

        if (handleAutomationMouseDown(e.position, timelineIndex))
            return;

        if (auto hit = findFadeTabHit(e.position, timelineIndex))
        {
            beginEditOnce();
            draggingFadeMarker = hit->marker;
            draggingFadeIsIn = hit->isFadeIn;
            draggingFadeTimelineIndex = timelineIndex;
            return;
        }

        if (auto* hit = findMarkerHit(e.position))
        {
            if (e.mods.isShiftDown())
            {
                toggleMarkerSelection(hit, timelineIndex);
                return;
            }
            beginEditOnce();
            dragTimelineIndex = timelineIndex;
            draggingMarker = hit;
            draggingMarker->isDragging = true;
            setSingleMarkerSelection(hit, timelineIndex);
            beginDragSelection(e.position, timelineIndex);
            return;
        }

        auto& timeline = model.getTimeline(timelineIndex);
        auto markerArea = getMarkerArea(row, timeline);
        double timeSeconds = xToTime(e.position.x, timeline, markerArea);
        if (snapEnabled)
            timeSeconds = snapTimeSeconds(timeSeconds, timeline);

        fileChooser = std::make_unique<juce::FileChooser>("Select a Python synth file", juce::File{}, "*.py");
        fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                 [this, timelineIndex, timeSeconds](const juce::FileChooser& chooser)
                                 {
                                     auto file = chooser.getResult();
                                     if (! file.existsAsFile())
                                         return;

                                     auto& timeline = model.getTimeline(timelineIndex);
                                     auto* marker = new Marker();
                                     marker->startTimeSeconds = juce::jlimit(0.0, timeline.durationSeconds, timeSeconds);
                                     marker->pythonFile = file;

                                     timeline.markers.add(marker);

                                     double renderDuration = getRenderDurationSecondsForMarker(timeline, *marker);
                                     renderer.renderMarker(*marker, sampleRateForRender, renderDuration, timeline.tempoBpm,
                                                          [this](bool, const juce::String&)
                                                          {
                                                              if (onMarkersChanged)
                                                                  onMarkersChanged();
                                                          });

                                     repaint();
                                 });
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        int timelineIndex = getTimelineIndexForY(e.position.y);
        if (timelineIndex < 0)
            return;

        if (auto* hit = findMarkerHit(e.position))
        {
            if (e.mods.isShiftDown() && hit->pythonFile.existsAsFile())
                hit->pythonFile.startAsProcess();
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (draggingFadeMarker != nullptr && draggingFadeTimelineIndex >= 0)
        {
            auto& timeline = model.getTimeline(draggingFadeTimelineIndex);
            auto row = getRowForTimelineIndex(draggingFadeTimelineIndex);
            auto markerArea = getMarkerArea(row, timeline);
            double timeSeconds = xToTime(e.position.x, timeline, markerArea);
            if (snapEnabled)
                timeSeconds = snapTimeSeconds(timeSeconds, timeline);

            if (draggingFadeMarker->renderedBuffer && draggingFadeMarker->renderedSampleRate > 0.0)
            {
                double duration = (double) draggingFadeMarker->renderedBuffer->getNumSamples() / draggingFadeMarker->renderedSampleRate;
                double start = draggingFadeMarker->startTimeSeconds;
                double end = start + duration;
                timeSeconds = juce::jlimit(start, end, timeSeconds);
                if (draggingFadeIsIn)
                    draggingFadeMarker->fadeInSeconds = juce::jlimit(0.0, duration, timeSeconds - start);
                else
                    draggingFadeMarker->fadeOutSeconds = juce::jlimit(0.0, duration, end - timeSeconds);

                // clamp overlapping fades
                if (draggingFadeMarker->fadeInSeconds + draggingFadeMarker->fadeOutSeconds > duration)
                {
                    double excess = (draggingFadeMarker->fadeInSeconds + draggingFadeMarker->fadeOutSeconds) - duration;
                    if (draggingFadeIsIn)
                        draggingFadeMarker->fadeInSeconds = juce::jmax(0.0, draggingFadeMarker->fadeInSeconds - excess);
                    else
                        draggingFadeMarker->fadeOutSeconds = juce::jmax(0.0, draggingFadeMarker->fadeOutSeconds - excess);
                }
            }

            repaint();
            return;
        }

        if (draggingRepeatMarker && selectedRepeatMarkerTimelineIndex >= 0)
        {
            int idx = selectedRepeatMarkerTimelineIndex;
            if (idx >= 0 && idx < model.getTimelineCount())
            {
                auto& timeline = model.getTimeline(idx);
                auto row = getRowForTimelineIndex(idx);
                auto markerArea = getMarkerArea(row, timeline);
                double timeSeconds = xToTime(e.position.x, timeline, markerArea);
                if (snapEnabled)
                    timeSeconds = snapTimeSeconds(timeSeconds, timeline);
                timeSeconds = juce::jlimit(0.0, timeline.durationSeconds, timeSeconds);

                if (selectedRepeatMarkerIndex >= 0 && selectedRepeatMarkerIndex < timeline.repeatMarkers.size())
                {
                    timeline.repeatMarkers.set(selectedRepeatMarkerIndex, timeSeconds);
                    timeline.repeatMarkers.sort();
                    selectedRepeatMarkerIndex = timeline.repeatMarkers.indexOf(timeSeconds);
                    repaint();
                }
            }
            return;
        }

        if (draggingAutomationLane != AutomationLane::None)
        {
            handleAutomationMouseDrag(e.position);
            return;
        }

        if (draggingMarker == nullptr || dragTimelineIndex < 0)
            return;

        int targetTimelineIndex = getTimelineIndexForY(e.position.y);
        dragOverTimelineIndex = targetTimelineIndex;
        if (targetTimelineIndex >= 0 && targetTimelineIndex != dragTimelineIndex)
        {
            moveSelectedMarkersToTimeline(dragTimelineIndex, targetTimelineIndex);
            dragTimelineIndex = targetTimelineIndex;
            setSelectedTimeline(targetTimelineIndex);
            auto rowForNew = getRowForTimelineIndex(dragTimelineIndex);
            auto markerArea = getMarkerArea(rowForNew, model.getTimeline(dragTimelineIndex));
            dragAnchorTimeSeconds = xToTime(e.position.x, model.getTimeline(dragTimelineIndex), markerArea);
        }

        auto& timeline = model.getTimeline(dragTimelineIndex);
        auto row = getRowForTimelineIndex(dragTimelineIndex);
        auto markerArea = getMarkerArea(row, timeline);
        double timeSeconds = xToTime(e.position.x, timeline, markerArea);
        if (snapEnabled)
            timeSeconds = snapTimeSeconds(timeSeconds, timeline);

        if (draggingGroup && ! dragStartTimes.empty())
        {
            double delta = timeSeconds - dragAnchorTimeSeconds;
            for (auto& entry : dragStartTimes)
            {
                auto* marker = entry.marker;
                if (marker == nullptr)
                    continue;
                double newTime = entry.startTimeSeconds + delta;
                marker->startTimeSeconds = juce::jlimit(0.0, timeline.durationSeconds, newTime);
            }
        }
        else
        {
            draggingMarker->startTimeSeconds = timeSeconds;
        }
        repaint();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        if (draggingFadeMarker != nullptr)
        {
            draggingFadeMarker = nullptr;
            draggingFadeIsIn = false;
            draggingFadeTimelineIndex = -1;
            editInProgress = false;
            repaint();
            return;
        }

        if (draggingRepeatMarker)
        {
            draggingRepeatMarker = false;
            editInProgress = false;
            repaint();
            return;
        }

        if (draggingAutomationLane != AutomationLane::None)
        {
            draggingAutomationLane = AutomationLane::None;
            draggingAutomationPointId = -1;
            draggingAutomationTimelineIndex = -1;
            editInProgress = false;
            repaint();
            return;
        }

        if (draggingMarker != nullptr)
        {
            draggingMarker->isDragging = false;
            draggingMarker = nullptr;
            dragTimelineIndex = -1;
            draggingGroup = false;
            dragStartTimes.clear();
            dragOverTimelineIndex = -1;
            editInProgress = false;
            repaint();
        }
    }

    void setRenderSampleRate(double sr)
    {
        sampleRateForRender = sr;
    }

    void setGridMode(GridMode mode)
    {
        gridMode = mode;
        repaint();
    }

    void setSelectedTimeline(int index)
    {
        selectedTimelineIndex = index;
        selectedMarkers.clear();
        lastSelectedMarker = nullptr;
        lastSelectedMarkerTimelineIndex = -1;
        if (onTimelineSelected)
            onTimelineSelected(index);
        if (onSelectionChanged)
            onSelectionChanged();
        repaint();
    }

    void setSelectedTimelinePreserveSelection(int index)
    {
        selectedTimelineIndex = index;
        if (onTimelineSelected)
            onTimelineSelected(index);
        if (onSelectionChanged)
            onSelectionChanged();
        repaint();
    }

    Marker* getSelectedMarker() const
    {
        return lastSelectedMarker;
    }

    int getSelectedMarkerTimelineIndex() const
    {
        return lastSelectedMarkerTimelineIndex;
    }

    juce::Array<Marker*> getSelectedMarkers() const
    {
        return selectedMarkers;
    }

    void clearMarkerSelection()
    {
        selectedMarkers.clear();
        lastSelectedMarker = nullptr;
        lastSelectedMarkerTimelineIndex = -1;
        if (onSelectionChanged)
            onSelectionChanged();
        repaint();
    }

    void setSnapEnabled(bool enabled)
    {
        snapEnabled = enabled;
        repaint();
    }

    void setSnapResolutionIndex(int index)
    {
        snapResolutionIndex = index;
    }

    void setScissorsEnabled(bool enabled)
    {
        scissorsEnabled = enabled;
        setMouseCursor(scissorsEnabled ? juce::MouseCursor::CrosshairCursor
                                       : juce::MouseCursor::NormalCursor);
        repaint();
    }

    bool hasSelectedRepeatMarkerPublic() const
    {
        return selectedRepeatMarkerTimelineIndex >= 0 && selectedRepeatMarkerIndex >= 0;
    }

    void deleteSelectedRepeatMarkerPublic()
    {
        if (! hasSelectedRepeatMarkerPublic())
            return;
        if (selectedRepeatMarkerTimelineIndex < 0 || selectedRepeatMarkerTimelineIndex >= model.getTimelineCount())
            return;
        auto& timeline = model.getTimeline(selectedRepeatMarkerTimelineIndex);
        if (selectedRepeatMarkerIndex < 0 || selectedRepeatMarkerIndex >= timeline.repeatMarkers.size())
            return;
        timeline.repeatMarkers.remove(selectedRepeatMarkerIndex);
        selectedRepeatMarkerIndex = -1;
        selectedRepeatMarkerTimelineIndex = -1;
        repaint();
    }

    void mouseMove(const juce::MouseEvent&) override
    {
        setMouseCursor(scissorsEnabled ? juce::MouseCursor::CrosshairCursor
                                       : juce::MouseCursor::NormalCursor);
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }

    int getContentHeightPublic() const
    {
        return getContentHeight();
    }

    bool hasSelectedAutomationPointPublic() const
    {
        return hasSelectedAutomationPoint();
    }

    void deleteSelectedAutomationPointPublic()
    {
        deleteSelectedAutomationPoint();
    }

    void rerenderTimelineMarkersIfNeeded(int timelineIndex)
    {
        if (timelineIndex < 0 || timelineIndex >= model.getTimelineCount())
            return;

        auto& timeline = model.getTimeline(timelineIndex);
        for (auto* marker : timeline.markers)
        {
            if (marker == nullptr)
                continue;

            bool needsRerender = false;
            if (marker->lastRenderedPythonPath != marker->pythonFile.getFullPathName())
                needsRerender = true;
            if (! juce::approximatelyEqual(marker->lastRenderedTempoBpm, timeline.tempoBpm))
                needsRerender = true;
            double desiredDuration = getRenderDurationSecondsForMarker(timeline, *marker);
            if (! juce::approximatelyEqual(marker->lastRenderedDurationSeconds, desiredDuration))
                needsRerender = true;

            if (needsRerender)
            {
                double renderDuration = getRenderDurationSecondsForMarker(timeline, *marker);
                renderer.renderMarker(*marker, sampleRateForRender, renderDuration, timeline.tempoBpm,
                                      [this](bool, const juce::String&)
                                      {
                                          if (onMarkersChanged)
                                              onMarkersChanged();
                                      });
            }
        }
    }

private:
    TimelineModel& model;
    PythonRenderer& renderer;
    std::function<void()> onMarkersChanged;
    std::function<void(int)> onTimelineSelected;
    std::function<double()> getPlayheadSeconds;
    std::function<void(int, double)> onScissorsCut;
    std::function<void()> onBeginEdit;
    std::function<void()> onSelectionChanged;
    double sampleRateForRender = 44100.0;
    std::unique_ptr<juce::FileChooser> fileChooser;
    GridMode gridMode = GridMode::Seconds;
    bool snapEnabled = false;
    int snapResolutionIndex = 0;
    bool scissorsEnabled = false;
    bool editInProgress = false;
    int selectedTimelineIndex = 0;
    int selectedRepeatMarkerIndex = -1;
    int selectedRepeatMarkerTimelineIndex = -1;
    bool draggingRepeatMarker = false;
    Marker* draggingMarker = nullptr;
    int dragTimelineIndex = -1;
    juce::Array<Marker*> selectedMarkers;
    Marker* lastSelectedMarker = nullptr;
    int lastSelectedMarkerTimelineIndex = -1;
    bool draggingGroup = false;
    double dragAnchorTimeSeconds = 0.0;
    int dragOverTimelineIndex = -1;
    AutomationLane draggingAutomationLane = AutomationLane::None;
    int draggingAutomationPointId = -1;
    int draggingAutomationTimelineIndex = -1;
    AutomationLane selectedAutomationLane = AutomationLane::None;
    int selectedAutomationPointId = -1;
    int selectedAutomationTimelineIndex = -1;
    struct DragStart
    {
        Marker* marker = nullptr;
        double startTimeSeconds = 0.0;
    };
    std::vector<DragStart> dragStartTimes;
    struct FadeHit
    {
        Marker* marker = nullptr;
        bool isFadeIn = true;
    };
    Marker* draggingFadeMarker = nullptr;
    bool draggingFadeIsIn = false;
    int draggingFadeTimelineIndex = -1;

    void beginEditOnce()
    {
        if (editInProgress)
            return;
        editInProgress = true;
        if (onBeginEdit)
            onBeginEdit();
    }

    int getTimelineIndexForY(float y) const
    {
        int timelineCount = model.getTimelineCount();
        if (timelineCount == 0)
            return -1;

        int yy = 0;
        for (int i = 0; i < timelineCount; ++i)
        {
            int h = getRowHeightForTimeline(model.getTimeline(i));
            if (y >= yy && y < yy + h)
                return i;
            yy += h;
        }
        return -1;
    }

    juce::Rectangle<int> getRowForTimelineIndex(int index) const
    {
        auto area = getLocalBounds();
        int timelineCount = model.getTimelineCount();
        if (timelineCount == 0)
            return {};

        int yy = area.getY();
        for (int i = 0; i < timelineCount; ++i)
        {
            int h = getRowHeightForTimeline(model.getTimeline(i));
            auto row = juce::Rectangle<int>(area.getX(), yy, area.getWidth(), h);
            if (i == index)
                return row;
            yy += h;
        }
        return {};
    }

    int getBaseRowHeight() const
    {
        return 120;
    }

    int getRowHeightForTimeline(const Timeline& timeline) const
    {
        double zoom = juce::jlimit(0.5, 2.0, timeline.zoomY);
        int h = (int) std::round(getBaseRowHeight() * zoom);
        return juce::jlimit(80, 260, h);
    }

    int getContentHeight() const
    {
        int total = 0;
        for (int i = 0; i < model.getTimelineCount(); ++i)
            total += getRowHeightForTimeline(model.getTimeline(i));
        return juce::jmax(getBaseRowHeight(), total);
    }

    struct ViewWindow
    {
        double start = 0.0;
        double length = 0.0;
    };

    struct AutomationLaneRects
    {
        juce::Rectangle<int> volume;
        juce::Rectangle<int> pan;
    };

    juce::Rectangle<int> getMarkerArea(juce::Rectangle<int> row, const Timeline& timeline) const
    {
        auto headerHeight = 24;
        auto automationHeight = getAutomationHeight(timeline);
        row.removeFromTop(headerHeight);
        row.removeFromBottom(automationHeight);
        return row;
    }

    AutomationLaneRects getAutomationLaneRects(juce::Rectangle<int> row, const Timeline& timeline) const
    {
        auto automationHeight = getAutomationHeight(timeline);
        auto area = row.removeFromBottom(automationHeight).reduced(8, 6);
        auto vol = area.removeFromTop(area.getHeight() / 2);
        auto pan = area;
        return { vol, pan };
    }

    juce::Rectangle<int> getAutomationPointsRect(juce::Rectangle<int> lane) const
    {
        return lane.withTrimmedLeft(30);
    }

    int getAutomationHeight(const Timeline& timeline) const
    {
        int base = timeline.automationExpanded ? 60 : 24;
        double zoom = juce::jlimit(0.5, 2.0, timeline.automationZoomY);
        int h = (int) std::round(base * zoom);
        return juce::jlimit(20, 140, h);
    }

    juce::Rectangle<int> getAutomationToggleRect(juce::Rectangle<int> row) const
    {
        auto size = 18;
        return juce::Rectangle<int>(row.getX() + 8, row.getY() + 3, size, size);
    }

    ViewWindow getViewWindow(const Timeline& timeline) const
    {
        double length = timeline.viewDurationSeconds;
        if (length <= 0.0 || length > timeline.durationSeconds)
            length = timeline.durationSeconds;

        double maxStart = juce::jmax(0.0, timeline.durationSeconds - length);
        double start = juce::jlimit(0.0, maxStart, timeline.viewStartSeconds);
        return { start, length };
    }

    bool isTimeInView(double timeSeconds, const Timeline& timeline) const
    {
        auto view = getViewWindow(timeline);
        if (view.length <= 0.0)
            return false;
        return timeSeconds >= view.start && timeSeconds <= (view.start + view.length);
    }

    int timeToX(double timeSeconds, const Timeline& timeline, juce::Rectangle<int> row) const
    {
        auto view = getViewWindow(timeline);
        double ratio = view.length > 0.0 ? (timeSeconds - view.start) / view.length : 0.0;
        ratio = juce::jlimit(0.0, 1.0, ratio);
        return row.getX() + (int) (ratio * (double) row.getWidth());
    }

    double xToTime(float x, const Timeline& timeline, juce::Rectangle<int> row) const
    {
        auto view = getViewWindow(timeline);
        double ratio = (x - (float) row.getX()) / (double) row.getWidth();
        ratio = juce::jlimit(0.0, 1.0, ratio);
        return view.start + ratio * view.length;
    }

    void drawGrid(juce::Graphics& g, juce::Rectangle<int> row, const Timeline& timeline)
    {
        auto content = row.reduced(6, 6);
        g.setColour(juce::Colour(0xff2b333a));
        g.drawRect(content);

        auto view = getViewWindow(timeline);
        if (view.length <= 0.0)
            return;

        if (gridMode == GridMode::Seconds)
        {
            const double major = 1.0;
            const double minor = 0.25;
            const double start = view.start;
            const double end = view.start + view.length;

            double t = std::floor(start / minor) * minor;
            if (t < 0.0)
                t = 0.0;
            for (; t <= end + 0.0001; t += minor)
            {
                bool isMajor = std::fmod(t, major) < 0.0001 || std::fmod(major - std::fmod(t, major), major) < 0.0001;
                auto x = timeToX(t, timeline, content);
                g.setColour(isMajor ? juce::Colour(0xff3a4650) : juce::Colour(0xff262c33));
                g.drawLine((float) x, (float) content.getY(), (float) x, (float) content.getBottom());
            }
        }
        else
        {
            const double beatsPerBar = (double) juce::jmax(1, timeline.beatsPerBar);
            const double beatUnit = (double) juce::jmax(1, timeline.beatUnit);
            const double beatSeconds = (60.0 / timeline.tempoBpm) * (4.0 / beatUnit);
            const double barSeconds = beatSeconds * beatsPerBar;
            const double start = view.start;
            const double end = view.start + view.length;

            double t = std::floor(start / beatSeconds) * beatSeconds;
            if (t < 0.0)
                t = 0.0;
            for (; t <= end + 0.0001; t += beatSeconds)
            {
                bool isBar = std::fmod(t, barSeconds) < 0.0001 || std::fmod(barSeconds - std::fmod(t, barSeconds), barSeconds) < 0.0001;
                auto x = timeToX(t, timeline, content);
                g.setColour(isBar ? juce::Colour(0xff3a4650) : juce::Colour(0xff262c33));
                g.drawLine((float) x, (float) content.getY(), (float) x, (float) content.getBottom());
            }
        }
    }

    void drawAutomationLanes(juce::Graphics& g, juce::Rectangle<int> row, const Timeline& timeline)
    {
        auto lanes = getAutomationLaneRects(row, timeline);
        auto volPoints = getAutomationPointsRect(lanes.volume);
        auto panPoints = getAutomationPointsRect(lanes.pan);

        g.setColour(juce::Colour(0xff2b333a));
        g.drawRect(lanes.volume);
        g.drawRect(lanes.pan);

        g.setColour(juce::Colour(0xffaab4bf));
        g.drawText("VOL", lanes.volume.removeFromLeft(30), juce::Justification::centredLeft);
        g.drawText("PAN", lanes.pan.removeFromLeft(30), juce::Justification::centredLeft);

        drawAutomationPoints(g, volPoints, timeline.volumeAutomation, timeline, 0.0, 1.0, juce::Colour(0xff7bd389));
        drawAutomationPoints(g, panPoints, timeline.panAutomation, timeline, -1.0, 1.0, juce::Colour(0xff78aef5));
    }

    void addRepeatMarker(Timeline& timeline, double timeSeconds)
    {
        timeSeconds = juce::jlimit(0.0, timeline.durationSeconds, timeSeconds);
        if (timeline.repeatMarkers.size() >= 2)
            timeline.repeatMarkers.clear();
        timeline.repeatMarkers.add(timeSeconds);
        timeline.repeatMarkers.sort();
    }

    int findRepeatMarkerIndex(juce::Point<float> pos, int timelineIndex)
    {
        if (timelineIndex < 0 || timelineIndex >= model.getTimelineCount())
            return -1;
        auto& timeline = model.getTimeline(timelineIndex);
        auto row = getRowForTimelineIndex(timelineIndex);
        auto markerArea = getMarkerArea(row, timeline);

        for (int i = 0; i < timeline.repeatMarkers.size(); ++i)
        {
            double t = timeline.repeatMarkers.getUnchecked(i);
            if (! isTimeInView(t, timeline))
                continue;
            int x = timeToX(t, timeline, markerArea);
            auto hit = juce::Rectangle<int>(x - 5, markerArea.getY(), 10, markerArea.getHeight());
            if (hit.contains((int) pos.x, (int) pos.y))
                return i;
        }
        return -1;
    }

    void drawMarkerWaveform(juce::Graphics& g,
                            juce::Rectangle<int> area,
                            const Timeline& timeline,
                            const Marker& marker,
                            juce::Colour colour,
                            int yOffset)
    {
        if (marker.waveform.empty() || marker.renderedSampleRate <= 0.0 || ! marker.renderedBuffer)
            return;

        double duration = (double) marker.renderedBuffer->getNumSamples() / marker.renderedSampleRate;
        if (duration <= 0.0)
            return;

        double start = marker.startTimeSeconds;
        double end = start + duration;
        if (end < timeline.viewStartSeconds || start > (timeline.viewStartSeconds + timeline.viewDurationSeconds))
            return;

        int points = (int) marker.waveform.size();
        if (points < 2)
            return;

        auto areaShifted = area.translated(0, yOffset);
        juce::Path path;
        juce::Path fill;
        bool started = false;
        for (int i = 0; i < points; ++i)
        {
            double t = start + (duration * i / (points - 1));
            if (! isTimeInView(t, timeline))
                continue;
            int x = timeToX(t, timeline, area);
            float amp = marker.waveform[i];
            int midY = areaShifted.getCentreY();
            int y = (int) (midY - amp * (areaShifted.getHeight() / 2.5f));
            if (! started)
            {
                path.startNewSubPath((float) x, (float) y);
                fill.startNewSubPath((float) x, (float) midY);
                fill.lineTo((float) x, (float) y);
                started = true;
            }
            else
            {
                path.lineTo((float) x, (float) y);
                fill.lineTo((float) x, (float) y);
            }
        }
        if (started)
        {
            fill.lineTo((float) timeToX(start + duration, timeline, area), (float) areaShifted.getCentreY());
            g.setColour(colour.withAlpha(0.5f));
            g.fillPath(fill);
            g.setColour(colour.withAlpha(0.9f));
            g.strokePath(path, juce::PathStrokeType(1.5f));
        }

        drawFadeRamps(g, areaShifted, timeline, marker, duration);
        drawFadeTabs(g, areaShifted, timeline, marker, duration);
    }

    void drawFadeRamps(juce::Graphics& g, juce::Rectangle<int> area, const Timeline& timeline, const Marker& marker, double duration)
    {
        if (duration <= 0.0)
            return;

        double start = marker.startTimeSeconds;
        double end = start + duration;
        int midY = area.getCentreY();
        int topY = area.getY() + 2;
        int botY = area.getBottom() - 2;

        g.setColour(juce::Colours::cyan.withAlpha(0.35f));

        if (marker.fadeInSeconds > 0.0)
        {
            double fiEnd = juce::jmin(start + marker.fadeInSeconds, end);
            if (isTimeInView(start, timeline) || isTimeInView(fiEnd, timeline))
            {
                int x0 = timeToX(start, timeline, area);
                int x1 = timeToX(fiEnd, timeline, area);
                juce::Path ramp;
                ramp.startNewSubPath((float) x0, (float) midY);
                ramp.lineTo((float) x1, (float) topY);
                ramp.lineTo((float) x1, (float) botY);
                ramp.lineTo((float) x0, (float) midY);
                g.fillPath(ramp);
            }
        }

        if (marker.fadeOutSeconds > 0.0)
        {
            double foStart = juce::jmax(end - marker.fadeOutSeconds, start);
            if (isTimeInView(foStart, timeline) || isTimeInView(end, timeline))
            {
                int x0 = timeToX(foStart, timeline, area);
                int x1 = timeToX(end, timeline, area);
                juce::Path ramp;
                ramp.startNewSubPath((float) x0, (float) topY);
                ramp.lineTo((float) x1, (float) midY);
                ramp.lineTo((float) x1, (float) midY);
                ramp.lineTo((float) x0, (float) botY);
                ramp.lineTo((float) x0, (float) topY);
                g.fillPath(ramp);
            }
        }
    }

    void drawFadeTabs(juce::Graphics& g, juce::Rectangle<int> area, const Timeline& timeline, const Marker& marker, double duration)
    {
        double start = marker.startTimeSeconds;
        double end = start + duration;
        if (! isTimeInView(start, timeline) && ! isTimeInView(end, timeline))
            return;

        int xStart = timeToX(start, timeline, area);
        int xEnd = timeToX(end, timeline, area);

        g.setColour(juce::Colours::cyan.withAlpha(0.9f));
        auto tabH = 8;
        auto tabW = 6;
        g.fillRect(xStart - tabW / 2, area.getY() + 1, tabW, tabH);
        g.fillRect(xEnd - tabW / 2, area.getY() + 1, tabW, tabH);
    }

    void drawRepeatMarkers(juce::Graphics& g, juce::Rectangle<int> area, const Timeline& timeline)
    {
        if (timeline.repeatMarkers.isEmpty())
            return;

        if (timeline.repeatMarkers.size() >= 2)
        {
            double loopStart = timeline.repeatMarkers.getUnchecked(0);
            double loopEnd = timeline.repeatMarkers.getUnchecked(1);
            if (loopEnd > loopStart && isTimeInView(loopStart, timeline) && isTimeInView(loopEnd, timeline))
            {
                int x0 = timeToX(loopStart, timeline, area);
                int x1 = timeToX(loopEnd, timeline, area);
                int y = area.getY() + 2;
                g.setColour(juce::Colour(0xffe66aa5).withAlpha(0.7f));
                g.drawLine((float) x0, (float) y, (float) x1, (float) y, 2.0f);
            }
        }

        g.setColour(juce::Colours::magenta.withAlpha(0.8f));
        for (int i = 0; i < timeline.repeatMarkers.size(); ++i)
        {
            double t = timeline.repeatMarkers.getUnchecked(i);
            if (! isTimeInView(t, timeline))
                continue;
            int x = timeToX(t, timeline, area);
            if (selectedRepeatMarkerIndex == i && selectedRepeatMarkerTimelineIndex == selectedTimelineIndex)
            {
                g.setColour(juce::Colours::yellow);
                g.drawLine((float) x, (float) area.getY(), (float) x, (float) area.getBottom(), 3.0f);
                g.setColour(juce::Colours::magenta.withAlpha(0.8f));
                auto label = formatTime(t, timeline);
                g.setColour(juce::Colours::white);
                g.drawText(label, x + 6, area.getY() + 2, 60, 16, juce::Justification::left);
                g.setColour(juce::Colours::magenta.withAlpha(0.8f));
            }
            else
            {
                g.drawLine((float) x, (float) area.getY(), (float) x, (float) area.getBottom(), 2.0f);
            }
        }
    }

    void drawHeader(juce::Graphics& g, juce::Rectangle<int> row, const Timeline& timeline, int index)
    {
        auto header = row.withHeight(24);
        g.setColour(juce::Colour(0xff11171d).withAlpha(0.7f));
        g.fillRect(header);

        drawAutomationToggle(g, header, timeline);

        g.setColour(juce::Colour(0xffe8edf2));
        g.setFont(juce::Font("Avenir Next", 12.5f, juce::Font::bold));
        auto textArea = header.reduced(6, 0);
        textArea.setX(textArea.getX() + 28);
        g.drawText("AUTO  |  Tempo: " + juce::String(timeline.tempoBpm)
                       + " BPM  Time Sig: " + juce::String(timeline.beatsPerBar) + "/" + juce::String(timeline.beatUnit)
                       + "  Duration: " + juce::String(timeline.durationSeconds) + "s  |  Track " + juce::String(index + 1),
                   textArea,
                   juce::Justification::centredLeft);
    }

    void drawAutomationToggle(juce::Graphics& g, juce::Rectangle<int> header, const Timeline& timeline)
    {
        auto rect = getAutomationToggleRect(header);
        g.setColour(juce::Colour(0xfff08a52));
        g.fillRoundedRectangle(rect.toFloat(), 3.0f);
        g.setColour(juce::Colour(0xff0f1418));
        g.drawRoundedRectangle(rect.toFloat(), 3.0f, 1.2f);

        int cx = rect.getCentreX();
        int cy = rect.getCentreY();
        g.drawLine((float) (cx - 4), (float) cy, (float) (cx + 4), (float) cy, 2.2f);
        if (! timeline.automationExpanded)
            g.drawLine((float) cx, (float) (cy - 4), (float) cx, (float) (cy + 4), 2.2f);
    }

    void drawAutomationPoints(juce::Graphics& g,
                              juce::Rectangle<int> lane,
                              const std::vector<AutomationPoint>& points,
                              const Timeline& timeline,
                              double minVal,
                              double maxVal,
                              juce::Colour colour)
    {
        if (lane.getWidth() <= 0 || lane.getHeight() <= 0)
            return;

        g.setColour(colour);
        if (points.size() >= 2)
        {
            juce::Path path;
            bool started = false;
            for (const auto& p : points)
            {
                if (! isTimeInView(p.timeSeconds, timeline))
                    continue;
                auto x = timeToX(p.timeSeconds, timeline, lane);
                auto y = valueToY(p.value, lane, minVal, maxVal);
                if (! started)
                {
                    path.startNewSubPath((float) x, (float) y);
                    started = true;
                }
                else
                {
                    path.lineTo((float) x, (float) y);
                }
            }
            g.strokePath(path, juce::PathStrokeType(1.2f));
        }

        for (const auto& p : points)
        {
            if (! isTimeInView(p.timeSeconds, timeline))
                continue;
            auto x = timeToX(p.timeSeconds, timeline, lane);
            auto y = valueToY(p.value, lane, minVal, maxVal);
            bool isSelected = (p.id == selectedAutomationPointId);
            if (isSelected)
            {
                g.setColour(juce::Colour(0xfff0cf4a));
                g.fillEllipse((float) x - 4.5f, (float) y - 4.5f, 9.0f, 9.0f);
                g.setColour(colour);
            }
            g.fillEllipse((float) x - 3.5f, (float) y - 3.5f, 7.0f, 7.0f);
            g.setColour(juce::Colour(0xff0f1418).withAlpha(0.6f));
            g.drawEllipse((float) x - 4.5f, (float) y - 4.5f, 9.0f, 9.0f, 1.0f);
            g.setColour(colour);

            if (isSelected)
            {
                auto timeText = formatTime(p.timeSeconds, timeline);
                auto valueText = juce::String(p.value, 2);
                auto text = timeText + "  " + valueText;
                g.setColour(juce::Colour(0xffe8edf2));
                g.drawText(text, x + 6, y - 14, 90, 18, juce::Justification::left);
            }
        }
    }

    int valueToY(double value, juce::Rectangle<int> lane, double minVal, double maxVal) const
    {
        double t = (value - minVal) / (maxVal - minVal);
        t = juce::jlimit(0.0, 1.0, t);
        return lane.getBottom() - (int) std::round(t * (double) lane.getHeight());
    }

    double yToValue(float y, juce::Rectangle<int> lane, double minVal, double maxVal) const
    {
        double t = ((double) lane.getBottom() - (double) y) / (double) lane.getHeight();
        t = juce::jlimit(0.0, 1.0, t);
        return minVal + t * (maxVal - minVal);
    }

    void drawPlayhead(juce::Graphics& g, juce::Rectangle<int> row, const Timeline& timeline)
    {
        if (timeline.durationSeconds <= 0.0 || ! getPlayheadSeconds)
            return;

        double playhead = getPlayheadSeconds();
        double localTime = getLoopedLocalTime(playhead, timeline);

        if (! isTimeInView(localTime, timeline))
            return;

        auto x = timeToX(localTime, timeline, row);
        g.setColour(juce::Colour(0xff7bd389));
        g.drawLine((float) x, (float) row.getY(), (float) x, (float) row.getBottom(), 1.5f);
    }

    bool isMarkerSelected(Marker* marker) const
    {
        return selectedMarkers.contains(marker);
    }

    void setSingleMarkerSelection(Marker* marker, int timelineIndex)
    {
        selectedMarkers.clear();
        if (marker != nullptr)
            selectedMarkers.add(marker);
        lastSelectedMarker = marker;
        lastSelectedMarkerTimelineIndex = timelineIndex;
        if (onSelectionChanged)
            onSelectionChanged();
        repaint();
    }

    void toggleMarkerSelection(Marker* marker, int timelineIndex)
    {
        if (marker == nullptr)
            return;

        if (selectedMarkers.contains(marker))
            selectedMarkers.removeFirstMatchingValue(marker);
        else
            selectedMarkers.add(marker);

        lastSelectedMarker = marker;
        lastSelectedMarkerTimelineIndex = timelineIndex;
        if (onSelectionChanged)
            onSelectionChanged();
        repaint();
    }

    void beginDragSelection(juce::Point<float> mousePos, int timelineIndex)
    {
        draggingGroup = selectedMarkers.size() > 1 && selectedMarkers.contains(draggingMarker);
        dragStartTimes.clear();

        auto& timeline = model.getTimeline(timelineIndex);
        auto row = getRowForTimelineIndex(timelineIndex);
        auto markerArea = getMarkerArea(row, timeline);
        dragAnchorTimeSeconds = xToTime(mousePos.x, timeline, markerArea);

        if (draggingGroup)
        {
            for (auto* marker : selectedMarkers)
            {
                if (marker == nullptr)
                    continue;
                dragStartTimes.push_back({ marker, marker->startTimeSeconds });
            }
        }
        else if (draggingMarker != nullptr)
        {
            dragStartTimes.push_back({ draggingMarker, draggingMarker->startTimeSeconds });
        }
    }

    void moveSelectedMarkersToTimeline(int fromIndex, int toIndex)
    {
        if (fromIndex < 0 || toIndex < 0 || fromIndex == toIndex)
            return;
        if (fromIndex >= model.getTimelineCount() || toIndex >= model.getTimelineCount())
            return;

        auto& fromTimeline = model.getTimeline(fromIndex);
        auto& toTimeline = model.getTimeline(toIndex);

        for (auto* marker : selectedMarkers)
        {
            if (marker == nullptr)
                continue;
            if (fromTimeline.markers.contains(marker))
            {
                fromTimeline.markers.removeObject(marker, false);
                toTimeline.markers.add(marker);
            }
        }

        lastSelectedMarker = selectedMarkers.isEmpty() ? nullptr : selectedMarkers.getLast();
        lastSelectedMarkerTimelineIndex = toIndex;
    }

    Marker* findMarkerHit(juce::Point<float> pos)
    {
        int timelineIndex = getTimelineIndexForY(pos.y);
        if (timelineIndex < 0)
            return nullptr;

        auto& timeline = model.getTimeline(timelineIndex);
        auto row = getRowForTimelineIndex(timelineIndex);
        auto markerArea = getMarkerArea(row, timeline);

        int markerCount = timeline.markers.size();
        const int markerYOffsetStep = 12;
        int markerYOffsetScaled = (int) std::round(markerYOffsetStep * juce::jmax(1.0, timeline.zoomY));
        double sizeScale = juce::jlimit(1.0, 2.5, timeline.zoomY);
        int hitWidth = (int) std::round(16.0 * sizeScale);
        int hitHeight = (int) std::round(24.0 * sizeScale);

        for (int markerIndex = 0; markerIndex < markerCount; ++markerIndex)
        {
            auto* marker = timeline.markers.getUnchecked(markerIndex);
            if (marker == nullptr)
                continue;
            if (! isTimeInView(marker->startTimeSeconds, timeline))
                continue;
            auto x = timeToX(marker->startTimeSeconds, timeline, markerArea);
            int yOffset = (markerCount > 1) ? (markerIndex * markerYOffsetScaled) : 0;
            int centerY = markerArea.getCentreY() + yOffset;
            auto hit = juce::Rectangle<int>(x - hitWidth / 2, centerY - hitHeight / 2, hitWidth, hitHeight);
            if (hit.contains((int) pos.x, (int) pos.y))
                return marker;
        }
        return nullptr;
    }

    std::optional<FadeHit> findFadeTabHit(juce::Point<float> pos, int timelineIndex)
    {
        if (timelineIndex < 0 || timelineIndex >= model.getTimelineCount())
            return std::nullopt;
        auto& timeline = model.getTimeline(timelineIndex);
        auto row = getRowForTimelineIndex(timelineIndex);
        auto markerArea = getMarkerArea(row, timeline);

        int markerCount = timeline.markers.size();
        const int markerYOffsetStep = 12;
        int markerYOffsetScaled = (int) std::round(markerYOffsetStep * juce::jmax(1.0, timeline.zoomY));
        double sizeScale = juce::jlimit(1.0, 2.5, timeline.zoomY);
        int tabWidth = (int) std::round(8.0 * sizeScale);
        int tabHeight = (int) std::round(12.0 * sizeScale);

        for (int markerIndex = 0; markerIndex < markerCount; ++markerIndex)
        {
            auto* marker = timeline.markers.getUnchecked(markerIndex);
            if (marker == nullptr || ! marker->renderedBuffer || marker->renderedSampleRate <= 0.0)
                continue;
            double duration = (double) marker->renderedBuffer->getNumSamples() / marker->renderedSampleRate;
            if (duration <= 0.0)
                continue;
            double start = marker->startTimeSeconds;
            double end = start + duration;
            int xStart = timeToX(start, timeline, markerArea);
            int xEnd = timeToX(end, timeline, markerArea);
            int yOffset = (markerCount > 1) ? (markerIndex * markerYOffsetScaled) : 0;
            auto areaShifted = markerArea.translated(0, yOffset);
            juce::Rectangle<int> tabStart(xStart - tabWidth / 2, areaShifted.getY(), tabWidth, tabHeight);
            juce::Rectangle<int> tabEnd(xEnd - tabWidth / 2, areaShifted.getY(), tabWidth, tabHeight);
            if (tabStart.contains((int) pos.x, (int) pos.y))
                return FadeHit{ marker, true };
            if (tabEnd.contains((int) pos.x, (int) pos.y))
                return FadeHit{ marker, false };
        }
        return std::nullopt;
    }

    bool handleAutomationMouseDown(juce::Point<float> pos, int timelineIndex)
    {
        beginEditOnce();
        auto row = getRowForTimelineIndex(timelineIndex);
        auto& timeline = model.getTimeline(timelineIndex);
        auto lanes = getAutomationLaneRects(row, timeline);
        auto volPoints = getAutomationPointsRect(lanes.volume);
        auto panPoints = getAutomationPointsRect(lanes.pan);

        if (volPoints.contains((int) pos.x, (int) pos.y))
        {
            return startAutomationDrag(timeline, timelineIndex, AutomationLane::Volume, volPoints, pos);
        }
        if (panPoints.contains((int) pos.x, (int) pos.y))
        {
            return startAutomationDrag(timeline, timelineIndex, AutomationLane::Pan, panPoints, pos);
        }
        return false;
    }

    bool startAutomationDrag(Timeline& timeline,
                             int timelineIndex,
                             AutomationLane lane,
                             juce::Rectangle<int> rect,
                             juce::Point<float> pos)
    {
        auto& points = (lane == AutomationLane::Volume) ? timeline.volumeAutomation : timeline.panAutomation;
        auto hitId = findAutomationPointId(points, rect, timeline, pos, (lane == AutomationLane::Volume) ? 0.0 : -1.0, 1.0);

        if (hitId >= 0)
        {
            draggingAutomationLane = lane;
            draggingAutomationPointId = hitId;
            draggingAutomationTimelineIndex = timelineIndex;
            selectedAutomationLane = lane;
            selectedAutomationPointId = hitId;
            selectedAutomationTimelineIndex = timelineIndex;
            return true;
        }

        AutomationPoint p;
        p.id = timeline.nextAutomationId++;
        p.timeSeconds = xToTime(pos.x, timeline, rect);
        if (snapEnabled)
            p.timeSeconds = snapTimeSeconds(p.timeSeconds, timeline);
        p.value = yToValue(pos.y, rect, (lane == AutomationLane::Volume) ? 0.0 : -1.0, 1.0);
        points.push_back(p);
        sortAutomation(points);

        draggingAutomationLane = lane;
        draggingAutomationPointId = p.id;
        draggingAutomationTimelineIndex = timelineIndex;
        selectedAutomationLane = lane;
        selectedAutomationPointId = p.id;
        selectedAutomationTimelineIndex = timelineIndex;
        return true;
    }

    void handleAutomationMouseDrag(juce::Point<float> pos)
    {
        if (draggingAutomationLane == AutomationLane::None || draggingAutomationTimelineIndex < 0)
            return;

        auto& timeline = model.getTimeline(draggingAutomationTimelineIndex);
        auto row = getRowForTimelineIndex(draggingAutomationTimelineIndex);
        auto lanes = getAutomationLaneRects(row, timeline);
        auto volPoints = getAutomationPointsRect(lanes.volume);
        auto panPoints = getAutomationPointsRect(lanes.pan);

        auto& points = (draggingAutomationLane == AutomationLane::Volume) ? timeline.volumeAutomation : timeline.panAutomation;
        auto rect = (draggingAutomationLane == AutomationLane::Volume) ? volPoints : panPoints;
        auto* p = findAutomationPointById(points, draggingAutomationPointId);
        if (p == nullptr)
            return;

        p->timeSeconds = xToTime(pos.x, timeline, rect);
        if (snapEnabled)
            p->timeSeconds = snapTimeSeconds(p->timeSeconds, timeline);
        p->timeSeconds = juce::jlimit(0.0, timeline.durationSeconds, p->timeSeconds);
        p->value = yToValue(pos.y, rect, (draggingAutomationLane == AutomationLane::Volume) ? 0.0 : -1.0, 1.0);
        sortAutomation(points);
        repaint();
    }

    int findAutomationPointId(const std::vector<AutomationPoint>& points,
                              juce::Rectangle<int> rect,
                              const Timeline& timeline,
                              juce::Point<float> pos,
                              double minVal,
                              double maxVal) const
    {
        for (const auto& p : points)
        {
            if (! isTimeInView(p.timeSeconds, timeline))
                continue;
            auto x = timeToX(p.timeSeconds, timeline, rect);
            auto y = valueToY(p.value, rect, minVal, maxVal);
            auto hit = juce::Rectangle<int>(x - 10, y - 10, 20, 20);
            if (hit.contains((int) pos.x, (int) pos.y))
                return p.id;
        }
        return -1;
    }

    AutomationPoint* findAutomationPointById(std::vector<AutomationPoint>& points, int id)
    {
        for (auto& p : points)
        {
            if (p.id == id)
                return &p;
        }
        return nullptr;
    }

    void sortAutomation(std::vector<AutomationPoint>& points)
    {
        std::sort(points.begin(), points.end(),
                  [](const AutomationPoint& a, const AutomationPoint& b)
                  {
                      return a.timeSeconds < b.timeSeconds;
                  });
    }

    bool hasSelectedAutomationPoint() const
    {
        return selectedAutomationLane != AutomationLane::None
            && selectedAutomationPointId >= 0
            && selectedAutomationTimelineIndex >= 0;
    }

    void deleteSelectedAutomationPoint()
    {
        if (! hasSelectedAutomationPoint())
            return;

        if (selectedAutomationTimelineIndex < 0 || selectedAutomationTimelineIndex >= model.getTimelineCount())
            return;

        auto& timeline = model.getTimeline(selectedAutomationTimelineIndex);
        auto& points = (selectedAutomationLane == AutomationLane::Volume) ? timeline.volumeAutomation : timeline.panAutomation;
        points.erase(std::remove_if(points.begin(), points.end(),
                                    [this](const AutomationPoint& p)
                                    {
                                        return p.id == selectedAutomationPointId;
                                    }),
                     points.end());

        selectedAutomationLane = AutomationLane::None;
        selectedAutomationPointId = -1;
        selectedAutomationTimelineIndex = -1;
        repaint();
    }

    juce::String formatTime(double seconds, const Timeline& timeline) const
    {
        if (gridMode == GridMode::BBT)
        {
            const double beatUnit = (double) juce::jmax(1, timeline.beatUnit);
            const double beatSeconds = (60.0 / timeline.tempoBpm) * (4.0 / beatUnit);
            const int beatsPerBar = juce::jmax(1, timeline.beatsPerBar);
            int totalBeats = (int) std::floor(seconds / beatSeconds);
            int bar = (totalBeats / beatsPerBar) + 1;
            int beat = (totalBeats % beatsPerBar) + 1;
            return juce::String(bar) + ":" + juce::String(beat);
        }

        return juce::String(seconds, 2) + "s";
    }

    double snapTimeSeconds(double timeSeconds, const Timeline& timeline) const
    {
        if (timeline.durationSeconds <= 0.0)
            return timeSeconds;

        const double grid = (gridMode == GridMode::BBT)
            ? getBeatGridSeconds(timeline)
            : getSecondsGridSeconds();

        if (grid <= 0.0)
            return timeSeconds;

        double snapped = std::round(timeSeconds / grid) * grid;
        return juce::jlimit(0.0, timeline.durationSeconds, snapped);
    }

    double getSecondsGridSeconds() const
    {
        // index 0..10 maps to 4.0, 2.0, 1.0, 0.5, 0.25, 0.125, 0.0625, 0.03125, 1/3, 1/6, 1/12
        switch (snapResolutionIndex)
        {
            case 0: return 4.0;
            case 1: return 2.0;
            case 2: return 1.0;
            case 3: return 0.5;
            case 4: return 0.25;
            case 5: return 0.125;
            case 6: return 0.0625;
            case 7: return 0.03125;
            case 8: return 1.0 / 3.0;
            case 9: return 1.0 / 6.0;
            case 10: return 1.0 / 12.0;
            default: return 0.25;
        }
    }

    double getBeatGridSeconds(const Timeline& timeline) const
    {
        const double beatUnit = (double) juce::jmax(1, timeline.beatUnit);
        const double beatSeconds = (60.0 / timeline.tempoBpm) * (4.0 / beatUnit);

        switch (snapResolutionIndex)
        {
            case 0: return beatSeconds * 4.0;
            case 1: return beatSeconds * 2.0;
            case 2: return beatSeconds;
            case 3: return beatSeconds / 2.0;
            case 4: return beatSeconds / 4.0;
            case 5: return beatSeconds / 8.0;
            case 6: return beatSeconds / 16.0;
            case 7: return beatSeconds / 32.0;
            case 8: return beatSeconds / 3.0;
            case 9: return beatSeconds / 6.0;
            case 10: return beatSeconds / 12.0;
            default: return beatSeconds / 4.0;
        }
    }
};

struct MarkerSnapshot
{
    int timelineIndex = -1;
    double startTimeSeconds = 0.0;
    int renderBars = 0;
    juce::File pythonFile;
    double renderedSampleRate = 0.0;
    double lastRenderedTempoBpm = 0.0;
    double lastRenderedDurationSeconds = 0.0;
    juce::String lastRenderedPythonPath;
    std::vector<float> waveform;
    double fadeInSeconds = 0.0;
    double fadeOutSeconds = 0.0;
    std::unique_ptr<juce::AudioBuffer<float>> renderedBuffer;
};

struct TimelineSnapshot
{
    double tempoBpm = 120.0;
    double durationSeconds = 8.0;
    int beatsPerBar = 4;
    int beatUnit = 4;
    double viewStartSeconds = 0.0;
    double viewDurationSeconds = 8.0;
    double volume = 1.0;
    double pan = 0.0;
    int nextAutomationId = 1;
    std::vector<AutomationPoint> volumeAutomation;
    std::vector<AutomationPoint> panAutomation;
    bool automationExpanded = false;
    double zoomY = 1.0;
    double automationZoomY = 1.0;
    juce::Array<double> repeatMarkers;
    std::vector<MarkerSnapshot> markers;
};

struct ModelSnapshot
{
    std::vector<TimelineSnapshot> timelines;
};

struct AppLookAndFeel : public juce::LookAndFeel_V4
{
    AppLookAndFeel()
    {
        setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a323a));
        setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3a4a56));
        setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe8edf2));
        setColour(juce::TextButton::textColourOnId, juce::Colour(0xffe8edf2));
        setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff20262c));
        setColour(juce::ComboBox::textColourId, juce::Colour(0xffe8edf2));
        setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff2f3942));
        setColour(juce::Label::textColourId, juce::Colour(0xffe8edf2));
        setColour(juce::Label::outlineColourId, juce::Colour(0xff2f3942));
        setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff20262c));
        setColour(juce::TextEditor::textColourId, juce::Colour(0xffe8edf2));
        setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff2f3942));
        setColour(juce::Slider::trackColourId, juce::Colour(0xff2b343d));
        setColour(juce::Slider::thumbColourId, juce::Colour(0xff6aa9ff));
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff20262c));
        setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffe8edf2));
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xff2f3942));
    }

    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override
    {
        return juce::Font("Avenir Next", (float) buttonHeight * 0.45f, juce::Font::bold);
    }

    juce::Font getLabelFont(juce::Label& label) override
    {
        float h = label.getHeight() > 0 ? (float) label.getHeight() * 0.55f : 14.0f;
        return juce::Font("Avenir Next", h, juce::Font::plain);
    }

    juce::Font getComboBoxFont(juce::ComboBox&) override
    {
        return juce::Font("Avenir Next", 14.0f, juce::Font::plain);
    }
};

class MainComponent : public juce::AudioAppComponent, private juce::Timer
{
public:
    MainComponent()
        : timelineView(model, renderer, [] {}, [this](int index) { selectTimeline(index); }, [this] { return playheadSeconds.load(); },
                      [this](int idx, double timeSec) { cutSelectedMarkersAt(idx, timeSec); },
                      [this] { pushUndoState(); },
                      [this] { updateInspectorFromModel(); })
    {
        setLookAndFeel(&appLookAndFeel);
        setWantsKeyboardFocus(true);
        grabKeyboardFocus();

        addAndMakeVisible(playButton);
        playButton.setButtonText("Play");
        playButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2b4b3f));
        playButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff376252));
        playButton.onClick = [this]
        {
            playing = ! playing;
            playButton.setButtonText(playing ? "Pause" : "Play");
        };

        addAndMakeVisible(stopButton);
        stopButton.setButtonText("Stop");
        stopButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a2a2a));
        stopButton.onClick = [this]
        {
            playing = false;
            playheadSeconds.store(0.0);
            playButton.setButtonText("Play");
        };

        addAndMakeVisible(loadButton);
        loadButton.setButtonText("Load");
        loadButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a323a));
        loadButton.onClick = [this]
        {
            loadProject();
        };

        addAndMakeVisible(saveButton);
        saveButton.setButtonText("Save");
        saveButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a323a));
        saveButton.onClick = [this]
        {
            saveProject();
        };

        addAndMakeVisible(gridModeBox);
        gridModeBox.addItem("Seconds", 1);
        gridModeBox.addItem("BBT", 2);
        gridModeBox.setSelectedId(1);
        gridModeBox.onChange = [this]
        {
            auto mode = gridModeBox.getSelectedId() == 2
                ? TimelineView::GridMode::BBT
                : TimelineView::GridMode::Seconds;
            timelineView.setGridMode(mode);
            updateSnapResolutionLabels(mode);
        };

        addAndMakeVisible(addTrackButton);
        addTrackButton.setButtonText("+ Track");
        addTrackButton.onClick = [this]
        {
            pushUndoState();
            model.addTimeline(120.0, 8.0);
            auto& timeline = model.getTimeline(model.getTimelineCount() - 1);
            timeline.viewDurationSeconds = timeline.durationSeconds;
            timeline.viewStartSeconds = 0.0;
            selectTimeline(model.getTimelineCount() - 1);
            updateTimelineViewSize();
            resized();
            timelineView.repaint();
        };

        addAndMakeVisible(snapToggle);
        snapToggle.setButtonText("Snap");
        snapToggle.onClick = [this]
        {
            timelineView.setSnapEnabled(snapToggle.getToggleState());
        };

        addAndMakeVisible(scissorsToggle);
        scissorsToggle.setButtonText("Scissors");
        scissorsToggle.onClick = [this]
        {
            timelineView.setScissorsEnabled(scissorsToggle.getToggleState());
        };

        addAndMakeVisible(snapResBox);
        snapResBox.addItem("4", 1);
        snapResBox.addItem("2", 2);
        snapResBox.addItem("1", 3);
        snapResBox.addItem("1/2", 4);
        snapResBox.addItem("1/4", 5);
        snapResBox.addItem("1/8", 6);
        snapResBox.addItem("1/16", 7);
        snapResBox.addItem("1/32", 8);
        snapResBox.addItem("1/3", 9);
        snapResBox.addItem("1/6", 10);
        snapResBox.addItem("1/12", 11);
        snapResBox.setSelectedId(5);
        timelineView.setSnapResolutionIndex(4);
        snapResBox.onChange = [this]
        {
            timelineView.setSnapResolutionIndex(snapResBox.getSelectedId() - 1);
        };
        updateSnapResolutionLabels(TimelineView::GridMode::Seconds);

        addAndMakeVisible(inspectorTitle);
        inspectorTitle.setText("Inspector", juce::dontSendNotification);
        inspectorTitle.setJustificationType(juce::Justification::centredLeft);
        inspectorTitle.setFont(juce::Font("Avenir Next", 18.0f, juce::Font::bold));

        addAndMakeVisible(validationStatus);
        validationStatus.setText("Synths: not checked", juce::dontSendNotification);
        validationStatus.setJustificationType(juce::Justification::centredLeft);
        validationStatus.setFont(juce::Font("Avenir Next", 12.0f, juce::Font::plain));

        addAndMakeVisible(validationResults);
        validationResults.setJustificationType(juce::Justification::topLeft);
        validationResults.setFont(juce::Font("Avenir Next", 11.0f, juce::Font::plain));
        validationResults.setColour(juce::Label::textColourId, juce::Colour(0xfff08a52));
        validationResults.setText("", juce::dontSendNotification);

        addAndMakeVisible(timelineInfoLabel);
        timelineInfoLabel.setText("Timeline: 1", juce::dontSendNotification);

        addAndMakeVisible(timeSigLabel);
        timeSigLabel.setText("Time Signature", juce::dontSendNotification);

        addAndMakeVisible(tempoLabel);
        tempoLabel.setText("Tempo (BPM)", juce::dontSendNotification);

        addAndMakeVisible(tempoEditor);
        tempoEditor.setInputRestrictions(6, "0123456789.");
        tempoEditor.onReturnKey = [this] { applyTempoFromInspector(); };
        tempoEditor.onFocusLost = [this] { applyTempoFromInspector(); };

        addAndMakeVisible(durationLabel);
        durationLabel.setText("Duration (s)", juce::dontSendNotification);

        addAndMakeVisible(durationEditor);
        durationEditor.setInputRestrictions(8, "0123456789.");
        durationEditor.onReturnKey = [this] { applyDurationFromInspector(); };
        durationEditor.onFocusLost = [this] { applyDurationFromInspector(); };

        addAndMakeVisible(markerRenderBarsLabel);
        markerRenderBarsLabel.setText("Marker Render Bars", juce::dontSendNotification);

        addAndMakeVisible(markerRenderBarsEditor);
        markerRenderBarsEditor.setInputRestrictions(3, "0123456789");
        markerRenderBarsEditor.onReturnKey = [this] { applyMarkerRenderBarsFromInspector(); };
        markerRenderBarsEditor.onFocusLost = [this] { applyMarkerRenderBarsFromInspector(); };
        markerRenderBarsEditor.setTextToShowWhenEmpty("A", juce::Colour(0xff74808c));

        addAndMakeVisible(volumeLabel);
        volumeLabel.setText("Volume", juce::dontSendNotification);

        addAndMakeVisible(volumeSlider);
        volumeSlider.setRange(0.0, 1.0, 0.001);
        volumeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        volumeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
        volumeSlider.onValueChange = [this] { applyVolumeFromInspector(); };

        addAndMakeVisible(panLabel);
        panLabel.setText("Pan", juce::dontSendNotification);

        addAndMakeVisible(panSlider);
        panSlider.setRange(-1.0, 1.0, 0.001);
        panSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        panSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
        panSlider.onValueChange = [this] { applyPanFromInspector(); };

        addAndMakeVisible(zoomLabel);
        zoomLabel.setText("Zoom (x)", juce::dontSendNotification);

        addAndMakeVisible(zoomSlider);
        zoomSlider.setRange(1.0, 16.0, 0.01);
        zoomSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        zoomSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
        zoomSlider.onValueChange = [this] { applyZoomFromInspector(); };

        addAndMakeVisible(scrollLabel);
        scrollLabel.setText("Scroll (s)", juce::dontSendNotification);

        addAndMakeVisible(scrollSlider);
        scrollSlider.setRange(0.0, 0.0, 0.01);
        scrollSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        scrollSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
        scrollSlider.onValueChange = [this] { applyScrollFromInspector(); };

        addAndMakeVisible(zoomYLabel);
        zoomYLabel.setText("Zoom (Y)", juce::dontSendNotification);

        addAndMakeVisible(zoomYSlider);
        zoomYSlider.setRange(0.5, 2.0, 0.01);
        zoomYSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        zoomYSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
        zoomYSlider.onValueChange = [this] { applyZoomYFromInspector(); };

        addAndMakeVisible(automationZoomYLabel);
        automationZoomYLabel.setText("Automation Zoom (Y)", juce::dontSendNotification);

        addAndMakeVisible(automationZoomYSlider);
        automationZoomYSlider.setRange(0.5, 2.0, 0.01);
        automationZoomYSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        automationZoomYSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
        automationZoomYSlider.onValueChange = [this] { applyAutomationZoomYFromInspector(); };

        addAndMakeVisible(beatsPerBarEditor);
        beatsPerBarEditor.setInputRestrictions(2, "0123456789");
        beatsPerBarEditor.onReturnKey = [this] { applyTimeSignatureFromInspector(); };
        beatsPerBarEditor.onFocusLost = [this] { applyTimeSignatureFromInspector(); };

        addAndMakeVisible(beatUnitBox);
        beatUnitBox.addItem("1", 1);
        beatUnitBox.addItem("2", 2);
        beatUnitBox.addItem("4", 3);
        beatUnitBox.addItem("8", 4);
        beatUnitBox.addItem("16", 5);
        beatUnitBox.onChange = [this] { applyTimeSignatureFromInspector(); };

        timelineViewport.setViewedComponent(&timelineView, false);
        timelineViewport.setScrollBarsShown(true, true);
        timelineViewport.setScrollBarThickness(12);
        timelineViewport.getVerticalScrollBar().setAutoHide(false);
        timelineViewport.getHorizontalScrollBar().setAutoHide(false);
        addAndMakeVisible(timelineViewport);

        setSize(1100, 700);
        setAudioChannels(0, 2);

        selectTimeline(0);
        updateTimelineViewSize();
        startTimerHz(30);
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds();
        juce::Colour bgTop(0xff0e1114);
        juce::Colour bgBottom(0xff13171b);
        g.setGradientFill(juce::ColourGradient(bgTop, 0, 0, bgBottom, 0, (float) bounds.getBottom(), false));
        g.fillAll();

        auto topBar = bounds.removeFromTop(48);
        g.setColour(juce::Colour(0xff1a2026));
        g.fillRect(topBar);
        g.setColour(juce::Colour(0xff2b343d));
        g.drawLine(0.0f, (float) topBar.getBottom(), (float) getWidth(), (float) topBar.getBottom());

        auto inspector = bounds.removeFromRight(220);
        g.setColour(juce::Colour(0xff12171c));
        g.fillRect(inspector);
        g.setColour(juce::Colour(0xff2b343d));
        g.drawLine((float) inspector.getX(), 0.0f, (float) inspector.getX(), (float) getHeight());
    }

    ~MainComponent() override
    {
        shutdownAudio();
        setLookAndFeel(nullptr);
    }

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override
    {
        juce::ignoreUnused(samplesPerBlockExpected);
        deviceSampleRate = sampleRate;
        timelineView.setRenderSampleRate(sampleRate);
    }

    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override
    {
        bufferToFill.clearActiveBufferRegion();

        if (! playing)
            return;

        auto numSamples = bufferToFill.numSamples;
        auto outBuffer = bufferToFill.buffer;

        double baseTime = playheadSeconds.load();
        for (int i = 0; i < numSamples; ++i)
        {
            double time = baseTime + ((double) i / deviceSampleRate);
            float left = 0.0f;
            float right = 0.0f;

            for (int t = 0; t < model.getTimelineCount(); ++t)
            {
                auto& timeline = model.getTimeline(t);
                if (timeline.durationSeconds <= 0.0)
                    continue;

                double localTime = getLoopedLocalTime(time, timeline);

                double vol = evalAutomation(timeline.volumeAutomation, localTime, timeline.volume);
                double pan = evalAutomation(timeline.panAutomation, localTime, timeline.pan);
                pan = juce::jlimit(-1.0, 1.0, pan);
                vol = juce::jlimit(0.0, 1.0, vol);
                double leftGain = vol * std::sqrt(0.5 * (1.0 - pan));
                double rightGain = vol * std::sqrt(0.5 * (1.0 + pan));

                for (auto& marker : timeline.markers)
                {
                    if (marker == nullptr || ! marker->renderedBuffer)
                        continue;

                    double relTime = localTime - marker->startTimeSeconds;
                    if (relTime < 0.0)
                        continue;

                    int sourceSample = (int) (relTime * marker->renderedSampleRate);
                    if (sourceSample < 0 || sourceSample >= marker->renderedBuffer->getNumSamples())
                        continue;

                    int ch = marker->renderedBuffer->getNumChannels();
                    float sampleL = marker->renderedBuffer->getSample(0, sourceSample);
                    float sampleR = (ch > 1) ? marker->renderedBuffer->getSample(1, sourceSample) : sampleL;

                    float fade = 1.0f;
                    int total = marker->renderedBuffer->getNumSamples();
                    int fadeInSamp = (int) std::round(marker->fadeInSeconds * marker->renderedSampleRate);
                    int fadeOutSamp = (int) std::round(marker->fadeOutSeconds * marker->renderedSampleRate);
                    if (fadeInSamp > 0 && sourceSample < fadeInSamp)
                        fade = (float) sourceSample / (float) fadeInSamp;
                    if (fadeOutSamp > 0 && sourceSample > total - fadeOutSamp)
                        fade = juce::jmin(fade, (float) (total - sourceSample) / (float) fadeOutSamp);

                    left += sampleL * (float) leftGain * fade;
                    right += sampleR * (float) rightGain * fade;
                }
            }

            outBuffer->addSample(0, bufferToFill.startSample + i, left);
            if (outBuffer->getNumChannels() > 1)
                outBuffer->addSample(1, bufferToFill.startSample + i, right);
        }

        playheadSeconds.store(baseTime + ((double) numSamples / deviceSampleRate));
    }

    void releaseResources() override
    {
    }

    void timerCallback() override
    {
        timelineView.repaint();
        handleAutosave();
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::backspaceKey || key == juce::KeyPress::deleteKey)
        {
            if (timelineView.hasSelectedAutomationPointPublic())
            {
                pushUndoState();
                timelineView.deleteSelectedAutomationPointPublic();
            }
            else if (timelineView.hasSelectedRepeatMarkerPublic())
            {
                pushUndoState();
                timelineView.deleteSelectedRepeatMarkerPublic();
            }
            else if (timelineView.getSelectedMarkers().isEmpty())
            {
                pushUndoState();
                deleteSelectedTimeline();
            }
            else
            {
                pushUndoState();
                deleteSelectedMarkers();
            }
            return true;
        }

        if (key.getModifiers().isCommandDown() && (key.getKeyCode() == 'z' || key.getKeyCode() == 'Z'))
        {
            undoSnapshot();
            return true;
        }

        if (key.getModifiers().isCommandDown() && (key.getKeyCode() == 'c' || key.getKeyCode() == 'C'))
        {
            copySelectedMarker();
            return true;
        }

        if (key.getModifiers().isCommandDown() && (key.getKeyCode() == 'v' || key.getKeyCode() == 'V'))
        {
            pasteCopiedMarkerAtPlayhead();
            return true;
        }

        if (key.getKeyCode() == juce::KeyPress::spaceKey)
        {
            playing = ! playing;
            playButton.setButtonText(playing ? "Pause" : "Play");
            return true;
        }

        if (key.getModifiers().isShiftDown() && (key.getKeyCode() == 'v' || key.getKeyCode() == 'V'))
        {
            runSynthValidation();
            return true;
        }

        if (key.getKeyCode() == 's' || key.getKeyCode() == 'S')
        {
            scissorsToggle.setToggleState(! scissorsToggle.getToggleState(), juce::sendNotification);
            timelineView.setScissorsEnabled(scissorsToggle.getToggleState());
            return true;
        }

        return false;
    }

    void selectTimeline(int index)
    {
        if (model.getTimelineCount() == 0)
        {
            selectedTimelineIndex = 0;
            updateInspectorFromModel();
            timelineView.repaint();
            return;
        }

        selectedTimelineIndex = juce::jlimit(0, model.getTimelineCount() - 1, index);
        updateInspectorFromModel();
    }

    void updateInspectorFromModel()
    {
        if (model.getTimelineCount() == 0)
            return;

        auto& timeline = model.getTimeline(selectedTimelineIndex);
        updatingInspector = true;
        timelineInfoLabel.setText("Timeline: " + juce::String(selectedTimelineIndex + 1),
                                  juce::dontSendNotification);
        beatsPerBarEditor.setText(juce::String(timeline.beatsPerBar), juce::dontSendNotification);
        beatUnitBox.setSelectedId(beatUnitToComboId(timeline.beatUnit), juce::dontSendNotification);
        tempoEditor.setText(juce::String(timeline.tempoBpm, 2), juce::dontSendNotification);
        durationEditor.setText(juce::String(timeline.durationSeconds, 2), juce::dontSendNotification);
        volumeSlider.setValue(timeline.volume, juce::dontSendNotification);
        panSlider.setValue(timeline.pan, juce::dontSendNotification);
        zoomSlider.setValue(getZoomFactorForTimeline(timeline), juce::dontSendNotification);
        zoomYSlider.setValue(timeline.zoomY, juce::dontSendNotification);
        automationZoomYSlider.setValue(timeline.automationZoomY, juce::dontSendNotification);
        updateScrollSliderRange(timeline);
        if (auto* marker = timelineView.getSelectedMarker())
        {
            markerRenderBarsEditor.setEnabled(true);
            if (marker->renderBars <= 0)
                markerRenderBarsEditor.setText("", juce::dontSendNotification);
            else
                markerRenderBarsEditor.setText(juce::String(marker->renderBars), juce::dontSendNotification);
        }
        else
        {
            markerRenderBarsEditor.setEnabled(false);
            markerRenderBarsEditor.setText("", juce::dontSendNotification);
        }
        updatingInspector = false;
        timelineView.repaint();
    }

    void applyTimeSignatureFromInspector()
    {
        if (model.getTimelineCount() == 0)
            return;

        auto& timeline = model.getTimeline(selectedTimelineIndex);
        int beatsPerBar = beatsPerBarEditor.getText().getIntValue();
        beatsPerBar = juce::jlimit(1, 32, beatsPerBar);
        timeline.beatsPerBar = beatsPerBar;

        int beatUnit = comboIdToBeatUnit(beatUnitBox.getSelectedId());
        if (beatUnit > 0)
            timeline.beatUnit = beatUnit;

        updateInspectorFromModel();
        timelineView.rerenderTimelineMarkersIfNeeded(selectedTimelineIndex);
    }

    void applyMarkerRenderBarsFromInspector()
    {
        if (model.getTimelineCount() == 0)
            return;
        if (updatingInspector)
            return;
        auto* marker = timelineView.getSelectedMarker();
        if (marker == nullptr)
            return;

        int bars = markerRenderBarsEditor.getText().getIntValue();
        if (bars <= 0)
            bars = 0;
        else
            bars = juce::jlimit(1, 999, bars);

        if (marker->renderBars == bars)
            return;

        pushUndoState();
        marker->renderBars = bars;
        timelineView.rerenderTimelineMarkersIfNeeded(selectedTimelineIndex);
        updateInspectorFromModel();
    }

    void deleteSelectedTimeline()
    {
        if (model.getTimelineCount() == 0)
            return;

        pushUndoState();
        int index = selectedTimelineIndex;
        auto removed = model.removeTimeline(index);
        if (! removed)
            return;

        if (model.getTimelineCount() == 0)
        {
            selectedTimelineIndex = 0;
        }
        else
        {
            int nextIndex = juce::jmin(index, model.getTimelineCount() - 1);
            selectTimeline(nextIndex);
        }

        resized();
        updateTimelineViewSize();
        timelineView.repaint();
    }

    void deleteSelectedMarkers()
    {
        auto selected = timelineView.getSelectedMarkers();
        if (selected.isEmpty())
            return;

        pushUndoState();
        for (int t = 0; t < model.getTimelineCount(); ++t)
        {
            auto& timeline = model.getTimeline(t);
            for (auto* marker : selected)
            {
                if (marker == nullptr)
                    continue;
                if (timeline.markers.contains(marker))
                {
                    timeline.markers.removeObject(marker, true);
                }
            }
        }

        timelineView.clearMarkerSelection();
        timelineView.repaint();
    }

    MarkerSnapshot makeMarkerSnapshot(Marker* marker, int timelineIndex) const
    {
        MarkerSnapshot snap;
        snap.timelineIndex = timelineIndex;
        if (marker == nullptr)
            return snap;
        snap.startTimeSeconds = marker->startTimeSeconds;
        snap.renderBars = marker->renderBars;
        snap.pythonFile = marker->pythonFile;
        snap.renderedSampleRate = marker->renderedSampleRate;
        snap.lastRenderedTempoBpm = marker->lastRenderedTempoBpm;
        snap.lastRenderedDurationSeconds = marker->lastRenderedDurationSeconds;
        snap.lastRenderedPythonPath = marker->lastRenderedPythonPath;
        snap.waveform = marker->waveform;
        snap.fadeInSeconds = marker->fadeInSeconds;
        snap.fadeOutSeconds = marker->fadeOutSeconds;
        if (marker->renderedBuffer)
        {
            snap.renderedBuffer = std::make_unique<juce::AudioBuffer<float>>(
                marker->renderedBuffer->getNumChannels(),
                marker->renderedBuffer->getNumSamples());
            snap.renderedBuffer->makeCopyOf(*marker->renderedBuffer);
        }
        return snap;
    }

    ModelSnapshot snapshotModel() const
    {
        ModelSnapshot snap;
        for (int i = 0; i < model.getTimelineCount(); ++i)
        {
            const auto& t = model.getTimeline(i);
            TimelineSnapshot ts;
            ts.tempoBpm = t.tempoBpm;
            ts.durationSeconds = t.durationSeconds;
            ts.beatsPerBar = t.beatsPerBar;
            ts.beatUnit = t.beatUnit;
            ts.viewStartSeconds = t.viewStartSeconds;
            ts.viewDurationSeconds = t.viewDurationSeconds;
            ts.volume = t.volume;
            ts.pan = t.pan;
            ts.nextAutomationId = t.nextAutomationId;
            ts.volumeAutomation = t.volumeAutomation;
            ts.panAutomation = t.panAutomation;
            ts.automationExpanded = t.automationExpanded;
            ts.zoomY = t.zoomY;
            ts.automationZoomY = t.automationZoomY;
            ts.repeatMarkers = t.repeatMarkers;
            for (auto* m : t.markers)
                ts.markers.push_back(makeMarkerSnapshot(m, i));
            snap.timelines.push_back(std::move(ts));
        }
        return snap;
    }

    void restoreModel(const ModelSnapshot& snap)
    {
        model.clearTimelines();
        for (const auto& ts : snap.timelines)
        {
            model.addTimeline(ts.tempoBpm, ts.durationSeconds);
            auto& t = model.getTimeline(model.getTimelineCount() - 1);
            t.beatsPerBar = ts.beatsPerBar;
            t.beatUnit = ts.beatUnit;
            t.viewStartSeconds = ts.viewStartSeconds;
            t.viewDurationSeconds = ts.viewDurationSeconds;
            t.volume = ts.volume;
            t.pan = ts.pan;
            t.nextAutomationId = ts.nextAutomationId;
            t.volumeAutomation = ts.volumeAutomation;
            t.panAutomation = ts.panAutomation;
            t.automationExpanded = ts.automationExpanded;
            t.zoomY = ts.zoomY;
            t.automationZoomY = ts.automationZoomY;
            t.repeatMarkers = ts.repeatMarkers;

            for (const auto& ms : ts.markers)
            {
                auto* m = new Marker();
                m->startTimeSeconds = ms.startTimeSeconds;
                m->renderBars = ms.renderBars;
                m->pythonFile = ms.pythonFile;
                m->renderedSampleRate = ms.renderedSampleRate;
                m->lastRenderedTempoBpm = ms.lastRenderedTempoBpm;
                m->lastRenderedDurationSeconds = ms.lastRenderedDurationSeconds;
                m->lastRenderedPythonPath = ms.lastRenderedPythonPath;
                m->waveform = ms.waveform;
                m->fadeInSeconds = ms.fadeInSeconds;
                m->fadeOutSeconds = ms.fadeOutSeconds;
                if (ms.renderedBuffer)
                {
                    m->renderedBuffer = std::make_unique<juce::AudioBuffer<float>>(
                        ms.renderedBuffer->getNumChannels(),
                        ms.renderedBuffer->getNumSamples());
                    m->renderedBuffer->makeCopyOf(*ms.renderedBuffer);
                }
                t.markers.add(m);
            }
        }

        updateTimelineViewSize();
        if (model.getTimelineCount() > 0)
            selectTimeline(juce::jlimit(0, model.getTimelineCount() - 1, selectedTimelineIndex));
        else
            selectTimeline(0);
        timelineView.repaint();
    }

    void pushUndoState()
    {
        if (isRestoring)
            return;
        markProjectDirty();
        undoSnapshots.push_back(snapshotModel());
        if (undoSnapshots.size() > 10)
            undoSnapshots.erase(undoSnapshots.begin());
    }

    void undoSnapshot()
    {
        if (undoSnapshots.empty())
            return;
        isRestoring = true;
        auto snap = std::move(undoSnapshots.back());
        undoSnapshots.pop_back();
        restoreModel(snap);
        isRestoring = false;
    }

    void cutSelectedMarkersAt(int timelineIndex, double timeSeconds)
    {
        if (timelineIndex < 0 || timelineIndex >= model.getTimelineCount())
            return;

        pushUndoState();

        auto& timeline = model.getTimeline(timelineIndex);
        auto selected = timelineView.getSelectedMarkers();
        juce::Array<Marker*> targets;
        for (auto* m : selected)
        {
            if (m != nullptr && timeline.markers.contains(m))
                targets.add(m);
        }

        if (targets.isEmpty())
        {
            // If nothing selected on this track, cut any markers that intersect the cut point.
            for (auto* m : timeline.markers)
            {
                if (m == nullptr || ! m->renderedBuffer || m->renderedSampleRate <= 0.0)
                    continue;
                double dur = (double) m->renderedBuffer->getNumSamples() / m->renderedSampleRate;
                if (dur <= 0.0)
                    continue;
                double start = m->startTimeSeconds;
                double end = start + dur;
                if (timeSeconds >= start && timeSeconds <= end)
                    targets.add(m);
            }
        }

        if (targets.isEmpty())
            return;

        for (auto* marker : targets)
        {
            if (marker == nullptr)
                continue;
            if (! marker->renderedBuffer || marker->renderedSampleRate <= 0.0)
                continue;

            double relTime = timeSeconds - marker->startTimeSeconds;
            if (relTime <= 0.0)
            {
                marker->renderedBuffer->setSize(marker->renderedBuffer->getNumChannels(), 0, true, true, true);
                recomputeWaveform(*marker);
                continue;
            }

            int cutSample = (int) std::round(relTime * marker->renderedSampleRate);
            int total = marker->renderedBuffer->getNumSamples();
            if (cutSample >= total)
                continue;

            cutSample = juce::jlimit(0, total, cutSample);
            auto channels = marker->renderedBuffer->getNumChannels();
            auto newBuf = std::make_unique<juce::AudioBuffer<float>>(channels, cutSample);
            for (int ch = 0; ch < channels; ++ch)
                newBuf->copyFrom(ch, 0, *marker->renderedBuffer, ch, 0, cutSample);
            marker->renderedBuffer = std::move(newBuf);
            {
                double duration = (double) cutSample / marker->renderedSampleRate;
                marker->fadeInSeconds = juce::jlimit(0.0, duration, marker->fadeInSeconds);
                marker->fadeOutSeconds = juce::jlimit(0.0, duration, marker->fadeOutSeconds);
            }
            recomputeWaveform(*marker);
        }

        timelineView.repaint();
    }

    void recomputeWaveform(Marker& marker)
    {
        const int points = 200;
        marker.waveform.clear();
        marker.waveform.resize(points, 0.0f);
        if (! marker.renderedBuffer || marker.renderedBuffer->getNumSamples() <= 0)
            return;

        int total = marker.renderedBuffer->getNumSamples();
        int channels = marker.renderedBuffer->getNumChannels();
        for (int i = 0; i < points; ++i)
        {
            int start = (int) ((long long) i * total / points);
            int end = (int) ((long long) (i + 1) * total / points);
            if (end <= start)
                end = start + 1;
            end = juce::jmin(end, total);
            float peak = 0.0f;
            for (int s = start; s < end; ++s)
            {
                float v = 0.0f;
                for (int ch = 0; ch < channels; ++ch)
                    v = juce::jmax(v, std::abs(marker.renderedBuffer->getSample(ch, s)));
                peak = juce::jmax(peak, v);
            }
            marker.waveform[i] = juce::jlimit(0.0f, 1.0f, peak);
        }
    }


    void copySelectedMarker()
    {
        auto selected = timelineView.getSelectedMarkers();
        if (selected.isEmpty())
            return;

        int timelineIndex = timelineView.getSelectedMarkerTimelineIndex();
        if (timelineIndex < 0)
            return;

        copiedMarkers.clear();
        double anchor = 0.0;
        if (auto* anchorMarker = timelineView.getSelectedMarker())
            anchor = anchorMarker->startTimeSeconds;

        for (auto* marker : selected)
        {
            if (marker == nullptr)
                continue;
            CopiedMarkerData data;
            data.timelineIndex = timelineIndex;
            data.pythonFile = marker->pythonFile;
            data.renderedSampleRate = marker->renderedSampleRate;
            data.lastRenderedTempoBpm = marker->lastRenderedTempoBpm;
            data.lastRenderedDurationSeconds = marker->lastRenderedDurationSeconds;
            data.lastRenderedPythonPath = marker->lastRenderedPythonPath;
            data.offsetSeconds = marker->startTimeSeconds - anchor;
            data.waveform = marker->waveform;
            data.fadeInSeconds = marker->fadeInSeconds;
            data.fadeOutSeconds = marker->fadeOutSeconds;
            data.renderBars = marker->renderBars;

            if (marker->renderedBuffer)
            {
                data.renderedBuffer = std::make_unique<juce::AudioBuffer<float>>(
                    marker->renderedBuffer->getNumChannels(),
                    marker->renderedBuffer->getNumSamples());
                data.renderedBuffer->makeCopyOf(*marker->renderedBuffer);
            }

            copiedMarkers.push_back(std::move(data));
        }
    }

    void pasteCopiedMarkerAtPlayhead()
    {
        if (copiedMarkers.empty())
            return;

        pushUndoState();

        int timelineIndex = copiedMarkers.front().timelineIndex;
        if (timelineIndex < 0 || timelineIndex >= model.getTimelineCount())
            timelineIndex = selectedTimelineIndex;

        if (timelineIndex < 0 || timelineIndex >= model.getTimelineCount())
            return;

        auto& timeline = model.getTimeline(timelineIndex);
        double playhead = playheadSeconds.load();
        double localTime = getLoopedLocalTime(playhead, timeline);

        for (auto& copied : copiedMarkers)
        {
            auto* marker = new Marker();
            double targetTime = localTime + copied.offsetSeconds;
            marker->startTimeSeconds = juce::jlimit(0.0, timeline.durationSeconds, targetTime);
            marker->pythonFile = copied.pythonFile;
            marker->renderedSampleRate = copied.renderedSampleRate;
            marker->lastRenderedTempoBpm = timeline.tempoBpm;
            marker->lastRenderedDurationSeconds = getRenderDurationSecondsForMarker(timeline, *marker);
            marker->lastRenderedPythonPath = marker->pythonFile.getFullPathName();
            marker->waveform = copied.waveform;
            marker->fadeInSeconds = copied.fadeInSeconds;
            marker->fadeOutSeconds = copied.fadeOutSeconds;
            marker->renderBars = copied.renderBars;

            if (copied.renderedBuffer)
            {
                marker->renderedBuffer = std::make_unique<juce::AudioBuffer<float>>(
                    copied.renderedBuffer->getNumChannels(),
                    copied.renderedBuffer->getNumSamples());
                marker->renderedBuffer->makeCopyOf(*copied.renderedBuffer);
            }

            timeline.markers.add(marker);

            if (! marker->renderedBuffer)
            {
                double renderDuration = getRenderDurationSecondsForMarker(timeline, *marker);
                renderer.renderMarker(*marker, deviceSampleRate, renderDuration, timeline.tempoBpm,
                                      [this](bool, const juce::String&)
                                      {
                                          timelineView.repaint();
                                      });
            }
        }

        selectTimeline(timelineIndex);
        timelineView.repaint();
    }

    void applyZoomFromInspector()
    {
        if (model.getTimelineCount() == 0)
            return;

        auto& timeline = model.getTimeline(selectedTimelineIndex);
        double zoom = juce::jlimit(1.0, 16.0, zoomSlider.getValue());
        if (timeline.durationSeconds <= 0.0)
            return;

        timeline.viewDurationSeconds = timeline.durationSeconds / zoom;
        clampViewToDuration(timeline);
        updateInspectorFromModel();
    }

    void applyScrollFromInspector()
    {
        if (model.getTimelineCount() == 0)
            return;

        auto& timeline = model.getTimeline(selectedTimelineIndex);
        timeline.viewStartSeconds = scrollSlider.getValue();
        clampViewToDuration(timeline);
        updateInspectorFromModel();
    }

    void applyZoomYFromInspector()
    {
        if (model.getTimelineCount() == 0)
            return;

        auto& timeline = model.getTimeline(selectedTimelineIndex);
        timeline.zoomY = juce::jlimit(0.5, 2.0, zoomYSlider.getValue());
        updateTimelineViewSize();
        updateInspectorFromModel();
    }

    void applyAutomationZoomYFromInspector()
    {
        if (model.getTimelineCount() == 0)
            return;

        auto& timeline = model.getTimeline(selectedTimelineIndex);
        timeline.automationZoomY = juce::jlimit(0.5, 2.0, automationZoomYSlider.getValue());
        updateTimelineViewSize();
        updateInspectorFromModel();
    }

    void applyTempoFromInspector()
    {
        if (model.getTimelineCount() == 0)
            return;

        auto& timeline = model.getTimeline(selectedTimelineIndex);
        double tempo = tempoEditor.getText().getDoubleValue();
        tempo = juce::jlimit(1.0, 400.0, tempo);
        timeline.tempoBpm = tempo;
        updateInspectorFromModel();
        timelineView.rerenderTimelineMarkersIfNeeded(selectedTimelineIndex);
    }

    void applyDurationFromInspector()
    {
        if (model.getTimelineCount() == 0)
            return;

        auto& timeline = model.getTimeline(selectedTimelineIndex);
        double previousZoom = getZoomFactorForTimeline(timeline);
        double duration = durationEditor.getText().getDoubleValue();
        duration = juce::jlimit(0.25, 3600.0, duration);
        timeline.durationSeconds = duration;
        timeline.viewDurationSeconds = timeline.durationSeconds / previousZoom;
        clampViewToDuration(timeline);
        updateInspectorFromModel();
        timelineView.rerenderTimelineMarkersIfNeeded(selectedTimelineIndex);
    }

    void applyVolumeFromInspector()
    {
        if (model.getTimelineCount() == 0)
            return;
        auto& timeline = model.getTimeline(selectedTimelineIndex);
        timeline.volume = juce::jlimit(0.0, 1.0, volumeSlider.getValue());
        updateInspectorFromModel();
    }

    void applyPanFromInspector()
    {
        if (model.getTimelineCount() == 0)
            return;
        auto& timeline = model.getTimeline(selectedTimelineIndex);
        timeline.pan = juce::jlimit(-1.0, 1.0, panSlider.getValue());
        updateInspectorFromModel();
    }

    void updateSnapResolutionLabels(TimelineView::GridMode mode)
    {
        if (mode == TimelineView::GridMode::Seconds)
        {
            snapResBox.changeItemText(1, "4s");
            snapResBox.changeItemText(2, "2s");
            snapResBox.changeItemText(3, "1s");
            snapResBox.changeItemText(4, "1/2s");
            snapResBox.changeItemText(5, "1/4s");
            snapResBox.changeItemText(6, "1/8s");
            snapResBox.changeItemText(7, "1/16s");
            snapResBox.changeItemText(8, "1/32s");
            snapResBox.changeItemText(9, "1/3s");
            snapResBox.changeItemText(10, "1/6s");
            snapResBox.changeItemText(11, "1/12s");
        }
        else
        {
            snapResBox.changeItemText(1, "4 beats");
            snapResBox.changeItemText(2, "2 beats");
            snapResBox.changeItemText(3, "1 beat");
            snapResBox.changeItemText(4, "1/2 beat");
            snapResBox.changeItemText(5, "1/4 beat");
            snapResBox.changeItemText(6, "1/8 beat");
            snapResBox.changeItemText(7, "1/16 beat");
            snapResBox.changeItemText(8, "1/32 beat");
            snapResBox.changeItemText(9, "1/3 beat");
            snapResBox.changeItemText(10, "1/6 beat");
            snapResBox.changeItemText(11, "1/12 beat");
        }
    }

    void clampViewToDuration(Timeline& timeline)
    {
        if (timeline.viewDurationSeconds <= 0.0 || timeline.viewDurationSeconds > timeline.durationSeconds)
            timeline.viewDurationSeconds = timeline.durationSeconds;

        double maxStart = juce::jmax(0.0, timeline.durationSeconds - timeline.viewDurationSeconds);
        timeline.viewStartSeconds = juce::jlimit(0.0, maxStart, timeline.viewStartSeconds);
    }

    double getZoomFactorForTimeline(const Timeline& timeline) const
    {
        if (timeline.durationSeconds <= 0.0)
            return 1.0;
        if (timeline.viewDurationSeconds <= 0.0)
            return 1.0;
        return juce::jlimit(1.0, 16.0, timeline.durationSeconds / timeline.viewDurationSeconds);
    }

    void updateScrollSliderRange(const Timeline& timeline)
    {
        double viewLen = timeline.viewDurationSeconds;
        if (viewLen <= 0.0 || viewLen > timeline.durationSeconds)
            viewLen = timeline.durationSeconds;
        double maxStart = juce::jmax(0.0, timeline.durationSeconds - viewLen);
        scrollSlider.setRange(0.0, maxStart, 0.01);
        scrollSlider.setValue(juce::jlimit(0.0, maxStart, timeline.viewStartSeconds), juce::dontSendNotification);
    }

    double evalAutomation(const std::vector<AutomationPoint>& points, double timeSeconds, double defaultValue) const
    {
        if (points.empty())
            return defaultValue;
        if (timeSeconds <= points.front().timeSeconds)
            return points.front().value;
        if (timeSeconds >= points.back().timeSeconds)
            return points.back().value;

        for (size_t i = 1; i < points.size(); ++i)
        {
            if (timeSeconds <= points[i].timeSeconds)
            {
                const auto& a = points[i - 1];
                const auto& b = points[i];
                double span = b.timeSeconds - a.timeSeconds;
                if (span <= 0.0)
                    return b.value;
                double t = (timeSeconds - a.timeSeconds) / span;
                return a.value + t * (b.value - a.value);
            }
        }
        return points.back().value;
    }

    int beatUnitToComboId(int beatUnit) const
    {
        switch (beatUnit)
        {
            case 1: return 1;
            case 2: return 2;
            case 4: return 3;
            case 8: return 4;
            case 16: return 5;
            default: return 3;
        }
    }

    int comboIdToBeatUnit(int id) const
    {
        switch (id)
        {
            case 1: return 1;
            case 2: return 2;
            case 3: return 4;
            case 4: return 8;
            case 5: return 16;
            default: return 4;
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8);
        auto top = area.removeFromTop(40);
        playButton.setBounds(top.removeFromLeft(120));
        stopButton.setBounds(top.removeFromLeft(80));
        loadButton.setBounds(top.removeFromLeft(80));
        saveButton.setBounds(top.removeFromLeft(80));
        gridModeBox.setBounds(top.removeFromLeft(140).reduced(6, 4));
        addTrackButton.setBounds(top.removeFromLeft(100).reduced(4, 4));
        snapToggle.setBounds(top.removeFromLeft(80));
        scissorsToggle.setBounds(top.removeFromLeft(100));
        snapResBox.setBounds(top.removeFromLeft(90).reduced(6, 4));

        auto inspectorArea = area.removeFromRight(220);
        timelineViewport.setBounds(area);
        updateTimelineViewSize();

        auto pad = 8;
        inspectorArea = inspectorArea.reduced(pad);
        inspectorTitle.setBounds(inspectorArea.removeFromTop(24));
        inspectorArea.removeFromTop(8);
        timelineInfoLabel.setBounds(inspectorArea.removeFromTop(24));
        inspectorArea.removeFromTop(12);
        timeSigLabel.setBounds(inspectorArea.removeFromTop(20));
        inspectorArea.removeFromTop(6);
        auto row = inspectorArea.removeFromTop(28);
        beatsPerBarEditor.setBounds(row.removeFromLeft(60));
        beatUnitBox.setBounds(row.removeFromLeft(80).reduced(6, 0));
        inspectorArea.removeFromTop(10);
        tempoLabel.setBounds(inspectorArea.removeFromTop(20));
        inspectorArea.removeFromTop(6);
        tempoEditor.setBounds(inspectorArea.removeFromTop(28));
        inspectorArea.removeFromTop(10);
        durationLabel.setBounds(inspectorArea.removeFromTop(20));
        inspectorArea.removeFromTop(6);
        durationEditor.setBounds(inspectorArea.removeFromTop(28));
        inspectorArea.removeFromTop(12);
        markerRenderBarsLabel.setBounds(inspectorArea.removeFromTop(20));
        inspectorArea.removeFromTop(6);
        markerRenderBarsEditor.setBounds(inspectorArea.removeFromTop(28));
        inspectorArea.removeFromTop(12);
        volumeLabel.setBounds(inspectorArea.removeFromTop(20));
        inspectorArea.removeFromTop(6);
        volumeSlider.setBounds(inspectorArea.removeFromTop(28));
        inspectorArea.removeFromTop(12);
        panLabel.setBounds(inspectorArea.removeFromTop(20));
        inspectorArea.removeFromTop(6);
        panSlider.setBounds(inspectorArea.removeFromTop(28));
        inspectorArea.removeFromTop(12);
        zoomLabel.setBounds(inspectorArea.removeFromTop(20));
        inspectorArea.removeFromTop(6);
        zoomSlider.setBounds(inspectorArea.removeFromTop(28));
        inspectorArea.removeFromTop(12);
        zoomYLabel.setBounds(inspectorArea.removeFromTop(20));
        inspectorArea.removeFromTop(6);
        zoomYSlider.setBounds(inspectorArea.removeFromTop(28));
        inspectorArea.removeFromTop(12);
        automationZoomYLabel.setBounds(inspectorArea.removeFromTop(20));
        inspectorArea.removeFromTop(6);
        automationZoomYSlider.setBounds(inspectorArea.removeFromTop(28));
        inspectorArea.removeFromTop(12);
        scrollLabel.setBounds(inspectorArea.removeFromTop(20));
        inspectorArea.removeFromTop(6);
        scrollSlider.setBounds(inspectorArea.removeFromTop(28));

        inspectorArea.removeFromTop(16);
        validationStatus.setBounds(inspectorArea.removeFromTop(20));
        inspectorArea.removeFromTop(6);
        validationResults.setBounds(inspectorArea);
    }

    void updateTimelineViewSize()
    {
        int w = juce::jmax(1, timelineViewport.getWidth());
        timelineView.setBounds(0, 0, w, timelineView.getContentHeightPublic());
        timelineViewport.setScrollBarsShown(true, true);
        timelineViewport.getVerticalScrollBar().setAutoHide(false);
        timelineViewport.getHorizontalScrollBar().setAutoHide(false);
    }

    juce::var serializeModel() const
    {
        auto* root = new juce::DynamicObject();
        juce::Array<juce::var> timelinesVar;
        for (int i = 0; i < model.getTimelineCount(); ++i)
        {
            const auto& t = model.getTimeline(i);
            auto* tObj = new juce::DynamicObject();
            tObj->setProperty("tempoBpm", t.tempoBpm);
            tObj->setProperty("durationSeconds", t.durationSeconds);
            tObj->setProperty("beatsPerBar", t.beatsPerBar);
            tObj->setProperty("beatUnit", t.beatUnit);
            tObj->setProperty("viewStartSeconds", t.viewStartSeconds);
            tObj->setProperty("viewDurationSeconds", t.viewDurationSeconds);
            tObj->setProperty("volume", t.volume);
            tObj->setProperty("pan", t.pan);
            tObj->setProperty("zoomY", t.zoomY);
            tObj->setProperty("automationZoomY", t.automationZoomY);
            tObj->setProperty("automationExpanded", t.automationExpanded);

            juce::Array<juce::var> repeat;
            for (int r = 0; r < t.repeatMarkers.size(); ++r)
                repeat.add(t.repeatMarkers.getUnchecked(r));
            tObj->setProperty("repeatMarkers", repeat);

            juce::Array<juce::var> volAuto;
            for (const auto& p : t.volumeAutomation)
            {
                auto* pObj = new juce::DynamicObject();
                pObj->setProperty("id", p.id);
                pObj->setProperty("timeSeconds", p.timeSeconds);
                pObj->setProperty("value", p.value);
                volAuto.add(juce::var(pObj));
            }
            tObj->setProperty("volumeAutomation", volAuto);

            juce::Array<juce::var> panAuto;
            for (const auto& p : t.panAutomation)
            {
                auto* pObj = new juce::DynamicObject();
                pObj->setProperty("id", p.id);
                pObj->setProperty("timeSeconds", p.timeSeconds);
                pObj->setProperty("value", p.value);
                panAuto.add(juce::var(pObj));
            }
            tObj->setProperty("panAutomation", panAuto);

            juce::Array<juce::var> markersVar;
            for (auto* m : t.markers)
            {
                if (m == nullptr)
                    continue;
            auto* mObj = new juce::DynamicObject();
            mObj->setProperty("startTimeSeconds", m->startTimeSeconds);
            mObj->setProperty("pythonPath", m->pythonFile.getFullPathName());
            mObj->setProperty("fadeInSeconds", m->fadeInSeconds);
            mObj->setProperty("fadeOutSeconds", m->fadeOutSeconds);
            mObj->setProperty("renderBars", m->renderBars);
            markersVar.add(juce::var(mObj));
        }
        tObj->setProperty("markers", markersVar);

            timelinesVar.add(juce::var(tObj));
        }
        root->setProperty("timelines", timelinesVar);
        return juce::var(root);
    }

    bool deserializeModel(const juce::var& rootVar)
    {
        auto* rootObj = rootVar.getDynamicObject();
        if (rootObj == nullptr)
            return false;
        auto timelinesVar = rootObj->getProperty("timelines");
        if (! timelinesVar.isArray())
            return false;
        auto* timelineArray = timelinesVar.getArray();
        if (timelineArray == nullptr)
            return false;

        model.clearTimelines();

        for (auto& tVar : *timelineArray)
        {
            auto* tObj = tVar.getDynamicObject();
            if (tObj == nullptr)
                continue;
            double tempo = (double) tObj->getProperty("tempoBpm");
            double duration = (double) tObj->getProperty("durationSeconds");
            model.addTimeline(tempo, duration);
            auto& t = model.getTimeline(model.getTimelineCount() - 1);
            t.beatsPerBar = (int) tObj->getProperty("beatsPerBar");
            t.beatUnit = (int) tObj->getProperty("beatUnit");
            t.viewStartSeconds = (double) tObj->getProperty("viewStartSeconds");
            t.viewDurationSeconds = (double) tObj->getProperty("viewDurationSeconds");
            t.volume = (double) tObj->getProperty("volume");
            t.pan = (double) tObj->getProperty("pan");
            t.zoomY = (double) tObj->getProperty("zoomY");
            t.automationZoomY = (double) tObj->getProperty("automationZoomY");
            t.automationExpanded = (bool) tObj->getProperty("automationExpanded");

            t.repeatMarkers.clear();
            auto repeatVar = tObj->getProperty("repeatMarkers");
            if (repeatVar.isArray())
            {
                if (auto* repeatArr = repeatVar.getArray())
                {
                    for (auto& rv : *repeatArr)
                        t.repeatMarkers.add((double) rv);
                }
            }

            t.volumeAutomation.clear();
            auto volVar = tObj->getProperty("volumeAutomation");
            if (volVar.isArray())
            {
                if (auto* volArr = volVar.getArray())
                {
                    for (auto& pv : *volArr)
                    {
                        if (auto* pObj = pv.getDynamicObject())
                        {
                            AutomationPoint p;
                            p.id = (int) pObj->getProperty("id");
                            p.timeSeconds = (double) pObj->getProperty("timeSeconds");
                            p.value = (double) pObj->getProperty("value");
                            t.volumeAutomation.push_back(p);
                        }
                    }
                }
            }

            t.panAutomation.clear();
            auto panVar = tObj->getProperty("panAutomation");
            if (panVar.isArray())
            {
                if (auto* panArr = panVar.getArray())
                {
                    for (auto& pv : *panArr)
                    {
                        if (auto* pObj = pv.getDynamicObject())
                        {
                            AutomationPoint p;
                            p.id = (int) pObj->getProperty("id");
                            p.timeSeconds = (double) pObj->getProperty("timeSeconds");
                            p.value = (double) pObj->getProperty("value");
                            t.panAutomation.push_back(p);
                        }
                    }
                }
            }

            auto markersVar = tObj->getProperty("markers");
            if (markersVar.isArray())
            {
                if (auto* markersArr = markersVar.getArray())
                {
                    for (auto& mv : *markersArr)
                    {
                        auto* mObj = mv.getDynamicObject();
                        if (mObj == nullptr)
                            continue;
                        auto* marker = new Marker();
                        marker->startTimeSeconds = (double) mObj->getProperty("startTimeSeconds");
                        marker->pythonFile = juce::File(mObj->getProperty("pythonPath").toString());
                        marker->fadeInSeconds = (double) mObj->getProperty("fadeInSeconds");
                        marker->fadeOutSeconds = (double) mObj->getProperty("fadeOutSeconds");
                        marker->renderBars = (int) mObj->getProperty("renderBars");
                        t.markers.add(marker);

                        if (marker->pythonFile.existsAsFile())
                        {
                            double renderDuration = getRenderDurationSecondsForMarker(t, *marker);
                            renderer.renderMarker(*marker, deviceSampleRate, renderDuration, t.tempoBpm,
                                                  [this](bool, const juce::String&)
                                                  {
                                                      timelineView.repaint();
                                                  });
                        }
                    }
                }
            }
        }

        updateTimelineViewSize();
        selectTimeline(model.getTimelineCount() > 0 ? 0 : 0);
        resized();
        timelineView.repaint();
        return true;
    }

    void saveProject()
    {
        projectChooser = std::make_unique<juce::FileChooser>("Save Project", juce::File{}, "*.json");
        projectChooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                                    [this](const juce::FileChooser& chooser)
                                    {
                                        auto file = chooser.getResult();
                                        if (file == juce::File())
                                            return;
                                        if (file.getFileExtension().isEmpty())
                                            file = file.withFileExtension(".json");
                                        writeProjectToFile(file);
                                        projectFile = file;
                                        autosaveEnabled = true;
                                        projectDirty = false;
                                        lastAutosaveTimeSeconds = juce::Time::getMillisecondCounterHiRes() / 1000.0;
                                    });
    }

    void loadProject()
    {
        projectChooser = std::make_unique<juce::FileChooser>("Load Project", juce::File{}, "*.json");
        projectChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                    [this](const juce::FileChooser& chooser)
                                    {
                                        auto file = chooser.getResult();
                                        if (! file.existsAsFile())
                                            return;
                                        auto text = file.loadFileAsString();
                                        auto parsed = juce::JSON::parse(text);
                                        if (parsed.isVoid())
                                            return;
                                        pushUndoState();
                                        deserializeModel(parsed);
                                        projectFile = file;
                                        autosaveEnabled = false;
                                        projectDirty = false;
                                        lastAutosaveTimeSeconds = juce::Time::getMillisecondCounterHiRes() / 1000.0;
                                    });
    }

    void writeProjectToFile(const juce::File& file)
    {
        auto json = juce::JSON::toString(serializeModel(), true);
        file.replaceWithText(json);
    }

    void markProjectDirty()
    {
        projectDirty = true;
    }

    void handleAutosave()
    {
        if (! autosaveEnabled || ! projectDirty)
            return;
        if (! projectFile.existsAsFile())
            return;
        double nowSeconds = juce::Time::getMillisecondCounterHiRes() / 1000.0;
        if (nowSeconds - lastAutosaveTimeSeconds < autosaveIntervalSeconds)
            return;
        writeProjectToFile(projectFile);
        projectDirty = false;
        lastAutosaveTimeSeconds = nowSeconds;
    }

    void runSynthValidation()
    {
        const auto pyDir = juce::File::getCurrentWorkingDirectory().getChildFile("py");
        if (! pyDir.isDirectory())
        {
            validationStatus.setText("No 'py' folder found.", juce::dontSendNotification);
            return;
        }

        validationStatus.setText("Validating synths...", juce::dontSendNotification);
        validationResults.setText("", juce::dontSendNotification);

        juce::Array<juce::File> pyFiles;
        pyDir.findChildFiles(pyFiles, juce::File::findFiles, false, "*.py");
        pyFiles.removeFirstMatchingValue(pyDir.getChildFile("_render_util.py"));

        if (pyFiles.isEmpty())
        {
            validationStatus.setText("No synths found in 'py'.", juce::dontSendNotification);
            return;
        }

        juce::Array<juce::File> toCheck;
        for (auto& f : pyFiles)
        {
            auto path = f.getFullPathName();
            auto mod = f.getLastModificationTime().toMilliseconds();
            auto it = lastValidationStamp.find(path);
            if (it == lastValidationStamp.end() || mod > it->second)
                toCheck.add(f);
        }

        if (toCheck.isEmpty())
        {
            validationStatus.setText("Synths unchanged since last validate.", juce::dontSendNotification);
            validationResults.setText("", juce::dontSendNotification);
            return;
        }

        validationStatus.setText("Validating " + juce::String(toCheck.size()) + " changed synths...",
                                 juce::dontSendNotification);

        int okCount = 0;
        juce::StringArray failures;
        for (auto& f : toCheck)
        {
            auto tempOut = juce::File::getSpecialLocation(juce::File::tempDirectory)
                               .getChildFile("validate_" + f.getFileNameWithoutExtension() + ".wav");
            juce::StringArray args;
            args.add(juce::String(PYTHON_EXECUTABLE_PATH));
            args.add(f.getFullPathName());
            args.add(tempOut.getFullPathName());
            args.add("44100");
            args.add("0.1");

            juce::ChildProcess proc;
            bool started = proc.start(args);
            if (! started)
            {
                failures.add(f.getFileName() + ": failed to start");
                continue;
            }
            proc.waitForProcessToFinish(15000);
            if (! tempOut.existsAsFile())
            {
                failures.add(f.getFileName() + ": no wav");
            }
            else
            {
                ++okCount;
                lastValidationStamp[f.getFullPathName()] = f.getLastModificationTime().toMilliseconds();
            }
        }

        if (failures.isEmpty())
        {
            validationStatus.setText("Synths OK: " + juce::String(okCount), juce::dontSendNotification);
            validationResults.setText("", juce::dontSendNotification);
        }
        else
        {
            validationStatus.setText("Synths OK: " + juce::String(okCount) + "  Failed: " + juce::String(failures.size()),
                                     juce::dontSendNotification);
            juce::String text;
            for (auto& s : failures)
                text += s + "\n";
            validationResults.setText(text.trim(), juce::dontSendNotification);
        }
    }

private:
    TimelineModel model;
    PythonRenderer renderer;
    AppLookAndFeel appLookAndFeel;
    TimelineView timelineView;
    juce::Viewport timelineViewport;
    juce::TextButton playButton;
    juce::TextButton stopButton;
    juce::TextButton loadButton;
    juce::TextButton saveButton;
    juce::ComboBox gridModeBox;
    juce::TextButton addTrackButton;
    juce::ToggleButton snapToggle;
    juce::ToggleButton scissorsToggle;
    juce::ComboBox snapResBox;
    juce::Label inspectorTitle;
    juce::Label timelineInfoLabel;
    juce::Label timeSigLabel;
    juce::TextEditor beatsPerBarEditor;
    juce::ComboBox beatUnitBox;
    juce::Label tempoLabel;
    juce::TextEditor tempoEditor;
    juce::Label durationLabel;
    juce::TextEditor durationEditor;
    juce::Label markerRenderBarsLabel;
    juce::TextEditor markerRenderBarsEditor;
    juce::Label volumeLabel;
    juce::Slider volumeSlider;
    juce::Label panLabel;
    juce::Slider panSlider;
    juce::Label zoomLabel;
    juce::Slider zoomSlider;
    juce::Label zoomYLabel;
    juce::Slider zoomYSlider;
    juce::Label automationZoomYLabel;
    juce::Slider automationZoomYSlider;
    juce::Label scrollLabel;
    juce::Slider scrollSlider;
    juce::Label validationStatus;
    juce::Label validationResults;

    std::atomic<bool> playing { false };
    double deviceSampleRate = 44100.0;
    std::atomic<double> playheadSeconds { 0.0 };
    int selectedTimelineIndex = 0;
    std::vector<ModelSnapshot> undoSnapshots;
    bool isRestoring = false;
    std::unique_ptr<juce::FileChooser> projectChooser;
    juce::File projectFile;
    bool autosaveEnabled = false;
    bool projectDirty = false;
    double autosaveIntervalSeconds = 30.0;
    double lastAutosaveTimeSeconds = 0.0;
    std::unordered_map<juce::String, int64_t> lastValidationStamp;
    bool updatingInspector = false;

    struct CopiedMarkerData
    {
        int timelineIndex = -1;
        juce::File pythonFile;
        std::unique_ptr<juce::AudioBuffer<float>> renderedBuffer;
        double renderedSampleRate = 0.0;
        double lastRenderedTempoBpm = 0.0;
        double lastRenderedDurationSeconds = 0.0;
        juce::String lastRenderedPythonPath;
        double offsetSeconds = 0.0;
        std::vector<float> waveform;
        double fadeInSeconds = 0.0;
        double fadeOutSeconds = 0.0;
        int renderBars = 0;
    };
    std::vector<CopiedMarkerData> copiedMarkers;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

class TimelineSynthApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return "PyLine 2.0"; }
    const juce::String getApplicationVersion() override    { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override             { return true; }

    void initialise(const juce::String&) override
    {
        mainWindow.reset(new MainWindow(getApplicationName()));
    }

    void shutdown() override
    {
        mainWindow = nullptr;
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted(const juce::String&) override
    {
    }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow(juce::String name)
            : DocumentWindow(name,
                             juce::Colours::black,
                             DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            setResizable(true, true);
            setContentOwned(new MainComponent(), true);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(TimelineSynthApplication)
