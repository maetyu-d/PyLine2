#include "PythonRenderer.h"

#ifndef PYTHON_EXECUTABLE_PATH
#define PYTHON_EXECUTABLE_PATH "/usr/bin/python3"
#endif

PythonRenderer::PythonRenderer()
    : pool(2)
{
}

PythonRenderer::~PythonRenderer()
{
    pool.removeAllJobs(true, 2000);
}

void PythonRenderer::renderMarker(Marker& marker,
                                  double sampleRate,
                                  double durationSeconds,
                                  double tempoBpm,
                                  RenderCallback callback)
{
    pool.addJob(new RenderJob(marker, sampleRate, durationSeconds, tempoBpm, std::move(callback)), true);
}

PythonRenderer::RenderJob::RenderJob(Marker& marker,
                                    double sr,
                                    double dur,
                                    double tempo,
                                    RenderCallback cb)
    : ThreadPoolJob("PythonRenderJob"),
      markerRef(marker),
      sampleRate(sr),
      durationSeconds(dur),
      tempoBpm(tempo),
      callback(std::move(cb))
{
}

ThreadPoolJob::JobStatus PythonRenderer::RenderJob::runJob()
{
    if (! markerRef.pythonFile.existsAsFile())
    {
        if (callback)
            callback(false, "Python file does not exist");
        return jobHasFinished;
    }

    auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    auto fileName = "render_" + juce::String::toHexString(juce::Random::getSystemRandom().nextInt()) + ".wav";
    markerRef.renderedWavFile = tempDir.getChildFile(fileName);

    juce::StringArray args;
    args.add(juce::String(PYTHON_EXECUTABLE_PATH));
    args.add(markerRef.pythonFile.getFullPathName());
    args.add(markerRef.renderedWavFile.getFullPathName());
    args.add(juce::String(sampleRate, 3));
    args.add(juce::String(durationSeconds, 3));

    juce::ChildProcess proc;
    bool started = proc.start(args);

    if (! started)
    {
        if (callback)
            callback(false, "Failed to start python process");
        return jobHasFinished;
    }

    proc.waitForProcessToFinish(120000); // 2 minutes timeout

    if (! markerRef.renderedWavFile.existsAsFile())
    {
        if (callback)
            callback(false, "Python did not produce a WAV file");
        return jobHasFinished;
    }

    juce::AudioFormatManager fmt;
    fmt.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(fmt.createReaderFor(markerRef.renderedWavFile));

    if (! reader)
    {
        if (callback)
            callback(false, "Failed to read rendered WAV");
        return jobHasFinished;
    }

    auto buffer = std::make_unique<juce::AudioBuffer<float>>((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read(buffer.get(), 0, (int) reader->lengthInSamples, 0, true, true);
    markerRef.renderedSampleRate = reader->sampleRate;
    markerRef.renderedBuffer = std::move(buffer);
    markerRef.lastRenderedTempoBpm = tempoBpm;
    markerRef.lastRenderedDurationSeconds = durationSeconds;
    markerRef.lastRenderedPythonPath = markerRef.pythonFile.getFullPathName();
    {
        const int points = 200;
        markerRef.waveform.clear();
        markerRef.waveform.resize(points, 0.0f);
        if (markerRef.renderedBuffer && markerRef.renderedBuffer->getNumSamples() > 0)
        {
            int total = markerRef.renderedBuffer->getNumSamples();
            int channels = markerRef.renderedBuffer->getNumChannels();
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
                        v = juce::jmax(v, std::abs(markerRef.renderedBuffer->getSample(ch, s)));
                    peak = juce::jmax(peak, v);
                }
                markerRef.waveform[i] = juce::jlimit(0.0f, 1.0f, peak);
            }
        }
    }

    if (callback)
        callback(true, "Render complete");

    return jobHasFinished;
}
