#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

namespace catnip
{
    struct Onset
    {
        juce::int64 sample = 0;
        float strength = 0.0f;
    };

    struct Analysis
    {
        std::vector<float> peaks;          // normalised 0..1, one per display bar (whole file)
        std::vector<Onset> onsets;         // strongest first
        std::vector<Onset> onsetsByTime;   // same transients, in time order (markers, snapping)
        double bpm = 0.0;                  // detected tempo, 0 = not found
        juce::int64 firstDownbeat = 0;     // sample where bar 1 starts
    };

    /** Peaks, transients, tempo and first downbeat of a whole file. */
    Analysis analyse (const juce::AudioBuffer<float>& buffer, double sampleRate, int numPeaks);

    /** count+1 boundaries from the strongest transients inside [0, length), gaps filled by halving. */
    std::vector<juce::int64> transientBounds (const std::vector<Onset>& onsets, juce::int64 length, int count);

    /** count+1 equally spaced boundaries over [0, length). */
    std::vector<juce::int64> gridBounds (juce::int64 length, int count);

    /** Chops on a musical grid: every chop starts on a grid line downbeat + k * samplesPerChop (k >= 0),
        only inside [windowStart, windowEnd), at most maxCount chops. Empty if none fits. */
    std::vector<juce::int64> beatGridBounds (juce::int64 downbeat, double samplesPerChop,
                                             juce::int64 windowStart, juce::int64 windowEnd, int maxCount);

    /** Closest transient to `position` within [position - before, position + after], or -1. */
    juce::int64 nearestOnset (const std::vector<Onset>& byTime, juce::int64 position, juce::int64 before, juce::int64 after);

    /** Moves each inner boundary back to the nearest rising zero crossing (never later), to avoid clicks. */
    void snapToZeroCrossings (std::vector<juce::int64>& bounds, const juce::AudioBuffer<float>& buffer, int searchRadius = 64);
}
