// Offline check: synthesises a drum-like loop with known hit positions and verifies
// that auto-chop finds them.
#include <juce_core/juce_core.h>
#include "ChopAnalysis.h"
#include <cmath>
#include <iostream>

int main()
{
    const double sr = 44100.0;
    const int len = (int) (sr * 4.0);
    juce::AudioBuffer<float> buf (2, len);
    buf.clear();

    // 16 hits with irregular spacing
    std::vector<int> hits;
    juce::Random rnd (7);
    int pos = 0;
    for (int i = 0; i < 16; ++i)
    {
        hits.push_back (pos);
        pos += (int) (sr * (0.18 + rnd.nextDouble() * 0.12));
    }

    for (size_t h = 0; h < hits.size(); ++h)
    {
        const double freq = 60.0 + 40.0 * (double) (h % 4);
        for (int n = 0; n < (int) (sr * 0.15) && hits[h] + n < len; ++n)
        {
            const double t = n / sr;
            const float v = (float) (std::exp (-t * 30.0) * (0.6 * std::sin (2.0 * juce::MathConstants<double>::pi * freq * t)
                                                             + 0.4 * (rnd.nextFloat() * 2.0f - 1.0f)));
            for (int c = 0; c < 2; ++c) buf.addSample (c, hits[h] + n, v);
        }
    }

    const auto a = catnip::analyse (buf, 160);
    auto bounds = catnip::transientBounds (a.onsets, len, 16);
    catnip::snapToZeroCrossings (bounds, buf);

    int matched = 0;
    for (size_t k = 1; k < hits.size(); ++k)
    {
        juce::int64 best = len;
        juce::int64 signedBest = 0; for (auto b : bounds) if (std::llabs (b - hits[k]) < best) { best = std::llabs (b - hits[k]); signedBest = b - hits[k]; }
        const bool ok = best < 1024;   // within ~23 ms
        matched += ok ? 1 : 0;
        std::cout << "hit " << k << " at " << hits[k] << "  boundary offset " << signedBest << (ok ? "" : "  <-- MISS") << "\n";
    }

    std::cout << "peaks: " << a.peaks.size() << ", onsets: " << a.onsets.size()
              << ", bounds: " << bounds.size() << ", matched " << matched << "/15\n";

    const auto grid = catnip::gridBounds (len, 8);
    const bool gridOk = grid.size() == 9 && grid.front() == 0 && grid.back() == len;
    std::cout << "grid ok: " << gridOk << "\n";
    return (matched >= 14 && bounds.size() == 17 && gridOk) ? 0 : 1;
}
