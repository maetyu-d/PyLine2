# PyLine 2

![](https://github.com/maetyu-d/PyLine2/blob/main/Screenshot%202026-02-07%20at%2017.55.24.png)

PyLine 2 is a timeline-based sequencer with some DAW-like features, built in JUICE for OS. It aims to combine some of the possibilities of live coding/procedurally generated audio (particularly in terms of flexibility and variation) with many of the compositional, editing, and mixing strengths of working in a DAW. Essentially, markers that point to .py synthesis or sound-generation files can be added to DAW-style tracks, each capable of having its own tempo, time signature, and duration. When a marker is added to a track, the audio output of the corresponding .py file is rendered to a buffer, effectively creating an audio object positioned in time. From this point, the audio can be played back, edited, modified, and otherwise manipulated within the app. At the same time, shift-double-clicking a .py marker opens the .py file for editing, with any changes triggering a re-render of that specific audio object.




