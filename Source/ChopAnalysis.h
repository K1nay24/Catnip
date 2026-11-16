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
        std::vector<float> peaks;    // normalised 0..1, one per display bar
        std::vector<Onset> onsets;   // sorted by strength, strongest first
    };

    /** Peaks for the display and transient candidates for auto-chopping. */
    Analysis analyse (const juce::AudioBuffer<float>& buffer, int numPeaks);

    /** count+1 boundaries (first = 0, last = length) built from the strongest transients,
        with gaps filled by halving the longest chop until there are enough. */
    std::vector<juce::int64> transientBounds (const std::vector<Onset>& onsets, juce::int64 length, int count);

    /** count+1 equally spaced boundaries. */
    std::vector<juce::int64> gridBounds (juce::int64 length, int count);

    /** Moves each inner boundary back to the nearest rising zero crossing (never later), to avoid clicks. */
    void snapToZeroCrossings (std::vector<juce::int64>& bounds, const juce::AudioBuffer<float>& buffer, int searchRadius = 64);
}
