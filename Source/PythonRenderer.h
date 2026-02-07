#pragma once

#include <JuceHeader.h>
#include "TimelineModel.h"

class PythonRenderer
{
public:
    using RenderCallback = std::function<void (bool success, const juce::String& message)>;

    PythonRenderer();
    ~PythonRenderer();

    void renderMarker(Marker& marker,
                      double sampleRate,
                      double durationSeconds,
                      double tempoBpm,
                      RenderCallback callback);

private:
    class RenderJob : public juce::ThreadPoolJob
    {
    public:
        RenderJob(Marker& marker,
                  double sampleRate,
                  double durationSeconds,
                  double tempoBpm,
                  RenderCallback callback);

        JobStatus runJob() override;

    private:
        Marker& markerRef;
        double sampleRate;
        double durationSeconds;
        double tempoBpm;
        RenderCallback callback;
    };

    juce::ThreadPool pool;
};
