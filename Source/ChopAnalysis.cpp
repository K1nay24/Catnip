#include "ChopAnalysis.h"
#include <algorithm>
#include <cmath>

namespace catnip
{
    static float monoSample (const juce::AudioBuffer<float>& b, juce::int64 i)
    {
        const int chs = b.getNumChannels();
        if (chs == 0) return 0.0f;
        float s = 0.0f;
        for (int c = 0; c < chs; ++c)
            s += b.getSample (c, (int) i);
        return s / (float) chs;
    }

    // Moves a coarse onset (frame resolution) to just before the first sample that is loud
    // compared with the hit that follows, so chops start tight without clipping the attack.
    static juce::int64 refineOnset (const juce::AudioBuffer<float>& buffer, juce::int64 coarse, int hop)
    {
        const juce::int64 len = buffer.getNumSamples();
        const auto from = std::max<juce::int64> (0, coarse - hop);
        const auto to = std::min<juce::int64> (len, coarse + 3 * hop);
        float peak = 0.0f;
        for (auto i = from; i < to; ++i)
            peak = std::max (peak, std::abs (monoSample (buffer, i)));
        if (peak <= 0.0f)
            return coarse;

        const float threshold = peak * 0.1f;
        for (auto i = from; i < to; ++i)
            if (std::abs (monoSample (buffer, i)) >= threshold)
                return std::max<juce::int64> (0, i - 32);   // ~0.7 ms pre-roll
        return coarse;
    }

    Analysis analyse (const juce::AudioBuffer<float>& buffer, int numPeaks)
    {
        Analysis a;
        const juce::int64 len = buffer.getNumSamples();
        if (len <= 0 || numPeaks <= 0)
            return a;

        // display peaks
        a.peaks.assign ((size_t) numPeaks, 0.0f);
        float maxPeak = 0.0f;
        for (int p = 0; p < numPeaks; ++p)
        {
            const auto start = (len * p) / numPeaks;
            const auto end = std::max (start + 1, (len * (p + 1)) / numPeaks);
            float m = 0.0f;
            for (auto i = start; i < end && i < len; i += 2)
                m = std::max (m, std::abs (monoSample (buffer, i)));
            a.peaks[(size_t) p] = m;
            maxPeak = std::max (maxPeak, m);
        }
        if (maxPeak > 0.0f)
            for (auto& v : a.peaks) v /= maxPeak;

        // energy envelope + positive spectral-free flux
        const int hop = 512;
        const auto frames = (int) (len / hop);
        if (frames < 8)
            return a;

        std::vector<float> env ((size_t) frames, 0.0f);
        for (int f = 0; f < frames; ++f)
        {
            double sum = 0.0;
            for (int j = 0; j < hop; j += 2)
            {
                const float x = monoSample (buffer, (juce::int64) f * hop + j);
                sum += (double) x * x;
            }
            env[(size_t) f] = (float) std::sqrt (sum / (hop / 2));
        }

        std::vector<float> flux ((size_t) frames, 0.0f);
        double mean = 0.0;
        for (int f = 1; f < frames; ++f)
        {
            flux[(size_t) f] = std::max (0.0f, env[(size_t) f] - env[(size_t) f - 1]);
            mean += flux[(size_t) f];
        }
        mean /= frames;

        for (int f = 3; f < frames - 3; ++f)
        {
            bool isMax = true;
            for (int w = -3; w <= 3 && isMax; ++w)
                if (flux[(size_t) (f + w)] > flux[(size_t) f])
                    isMax = false;

            if (isMax && flux[(size_t) f] > mean * 1.3)
                a.onsets.push_back ({ refineOnset (buffer, (juce::int64) f * hop - hop / 2, hop), flux[(size_t) f] });
        }

        std::sort (a.onsets.begin(), a.onsets.end(), [] (const Onset& x, const Onset& y) { return x.strength > y.strength; });
        if (a.onsets.size() > 400)
            a.onsets.resize (400);
        return a;
    }

    static void fillGaps (std::vector<juce::int64>& b, int count)
    {
        while ((int) b.size() < count + 1)
        {
            size_t gi = 0;
            juce::int64 gw = 0;
            for (size_t q = 0; q + 1 < b.size(); ++q)
                if (b[q + 1] - b[q] > gw) { gw = b[q + 1] - b[q]; gi = q; }
            b.insert (b.begin() + (std::ptrdiff_t) gi + 1, b[gi] + gw / 2);
        }
    }

    std::vector<juce::int64> transientBounds (const std::vector<Onset>& onsets, juce::int64 length, int count)
    {
        std::vector<juce::int64> picked;
        const auto minGap = std::max<juce::int64> (256, length / (count * 3));
        const auto edge = length / 100;

        for (const auto& o : onsets)
        {
            if ((int) picked.size() >= count - 1) break;
            if (o.sample < edge || o.sample > length - edge) continue;

            bool ok = true;
            for (auto p : picked)
                if (std::llabs (p - o.sample) < minGap) { ok = false; break; }
            if (ok) picked.push_back (o.sample);
        }

        std::sort (picked.begin(), picked.end());
        std::vector<juce::int64> b { 0 };
        b.insert (b.end(), picked.begin(), picked.end());
        b.push_back (length);
        fillGaps (b, count);
        return b;
    }

    std::vector<juce::int64> gridBounds (juce::int64 length, int count)
    {
        std::vector<juce::int64> b;
        for (int i = 0; i <= count; ++i)
            b.push_back ((length * i) / count);
        return b;
    }

    void snapToZeroCrossings (std::vector<juce::int64>& bounds, const juce::AudioBuffer<float>& buffer, int searchRadius)
    {
        const juce::int64 len = buffer.getNumSamples();
        for (size_t k = 1; k + 1 < bounds.size(); ++k)
        {
            const auto centre = bounds[k];
            juce::int64 best = centre, bestDist = searchRadius + 1;
            const auto lo = std::max<juce::int64> (bounds[k - 1] + 1, centre - searchRadius);
            const auto hi = std::min<juce::int64> (bounds[k + 1] - 1, centre);
            for (auto i = std::max<juce::int64> (1, lo); i <= hi && i < len; ++i)
            {
                if (monoSample (buffer, i - 1) <= 0.0f && monoSample (buffer, i) > 0.0f)
                {
                    const auto d = std::llabs (i - centre);
                    if (d < bestDist) { bestDist = d; best = i; }
                }
            }
            bounds[k] = best;
        }
    }
}
