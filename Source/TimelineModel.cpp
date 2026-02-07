#include "TimelineModel.h"

TimelineModel::TimelineModel()
{
    addTimeline(120.0, 8.0);
    addTimeline(90.0, 10.0);
    addTimeline(140.0, 6.0);

    // Example of different time signatures
    if (timelines.size() > 1)
    {
        timelines.getUnchecked(1)->beatsPerBar = 3;
        timelines.getUnchecked(1)->beatUnit = 4;
    }
    if (timelines.size() > 2)
    {
        timelines.getUnchecked(2)->beatsPerBar = 7;
        timelines.getUnchecked(2)->beatUnit = 8;
    }
}

int TimelineModel::getTimelineCount() const
{
    return timelines.size();
}

Timeline& TimelineModel::getTimeline(int index)
{
    return *timelines.getUnchecked(index);
}

const Timeline& TimelineModel::getTimeline(int index) const
{
    return *timelines.getUnchecked(index);
}

void TimelineModel::addTimeline(double tempoBpm, double durationSeconds)
{
    auto* t = new Timeline();
    t->tempoBpm = tempoBpm;
    t->durationSeconds = durationSeconds;
    t->viewStartSeconds = 0.0;
    t->viewDurationSeconds = durationSeconds;
    t->volume = 1.0;
    t->pan = 0.0;
    t->zoomY = 1.0;
    t->automationZoomY = 1.0;
    timelines.add(t);
}

std::unique_ptr<Timeline> TimelineModel::removeTimeline(int index)
{
    if (index < 0 || index >= timelines.size())
        return nullptr;
    return std::unique_ptr<Timeline>(timelines.removeAndReturn(index));
}

void TimelineModel::insertTimeline(int index, std::unique_ptr<Timeline> timeline)
{
    if (! timeline)
        return;
    index = juce::jlimit(0, timelines.size(), index);
    timelines.insert(index, timeline.release());
}

void TimelineModel::clearTimelines()
{
    timelines.clear(true);
}
