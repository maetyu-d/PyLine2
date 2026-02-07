#pragma once

#include <JuceHeader.h>
#include <vector>

struct Marker
{
    double startTimeSeconds = 0.0;
    bool isDragging = false;
    int renderBars = 0;
    double lastRenderedTempoBpm = 0.0;
    double lastRenderedDurationSeconds = 0.0;
    juce::String lastRenderedPythonPath;
    std::vector<float> waveform;
    double fadeInSeconds = 0.0;
    double fadeOutSeconds = 0.0;
    juce::File pythonFile;
    juce::File renderedWavFile;
    std::unique_ptr<juce::AudioBuffer<float>> renderedBuffer;
    double renderedSampleRate = 0.0;
};

struct AutomationPoint
{
    int id = 0;
    double timeSeconds = 0.0;
    double value = 0.0;
};

struct Timeline
{
    double tempoBpm = 120.0;
    double durationSeconds = 8.0;
    int beatsPerBar = 4;
    int beatUnit = 4;
    double viewStartSeconds = 0.0;
    double viewDurationSeconds = 0.0;
    double volume = 1.0;
    double pan = 0.0;
    int nextAutomationId = 1;
    std::vector<AutomationPoint> volumeAutomation;
    std::vector<AutomationPoint> panAutomation;
    bool automationExpanded = false;
    double zoomY = 1.0;
    double automationZoomY = 1.0;
    juce::Array<double> repeatMarkers;
    juce::OwnedArray<Marker> markers;
};

class TimelineModel
{
public:
    TimelineModel();

    int getTimelineCount() const;
    Timeline& getTimeline(int index);
    const Timeline& getTimeline(int index) const;

    void addTimeline(double tempoBpm, double durationSeconds);
    std::unique_ptr<Timeline> removeTimeline(int index);
    void insertTimeline(int index, std::unique_ptr<Timeline> timeline);
    void clearTimelines();

private:
    juce::OwnedArray<Timeline> timelines;
};
